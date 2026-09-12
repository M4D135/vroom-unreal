#!/usr/bin/env python3
"""Install the optional NVIDIA UE 5.8 source plugins from a local official ZIP.

No network access, engine launch or project-descriptor edits. Only DLSS and
StreamlineNGXCommon are installed; NVIDIA's frame-generation plugins are not.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
import zipfile


OFFICIAL_URL = (
    "https://developer.nvidia.com/downloads/assets/gameworks/downloads/secure/"
    "dlss/UE-DLSS-5.8/8.7.2/2026.07.21_UE5.8_DLSS4.5Plugin_v8.7.2.zip"
)
PLUGINS = ("DLSS", "StreamlineNGXCommon")
MAX_UNPACKED = 8 * 1024**3


class InstallError(ValueError):
    pass


def ignored(parts: tuple[str, ...]) -> bool:
    return any(part.casefold() == "intermediate" for part in parts) or (
        bool(parts) and parts[-1].casefold().endswith(".pdb")
    )


def safe_entries(archive: zipfile.ZipFile) -> dict[tuple[str, ...], zipfile.ZipInfo]:
    result = {}
    seen = set()
    entries = archive.infolist()
    if len(entries) > 100000 or sum(item.file_size for item in entries) > MAX_UNPACKED:
        raise InstallError("Archive exceeds the supported unpacked size or file count.")
    for entry in entries:
        name = entry.orig_filename
        parts = tuple(name.rstrip("/").split("/"))
        if (not name or name.startswith("/") or "\\" in name or "\0" in name
                or any(part in ("", ".", "..") or ":" in part for part in parts)):
            raise InstallError("Archive contains an unsafe path.")
        mode = stat.S_IFMT(entry.external_attr >> 16)
        if mode not in (0, stat.S_IFREG, stat.S_IFDIR):
            raise InstallError("Archive links and special files are not supported.")
        if entry.flag_bits & 1:
            raise InstallError("Encrypted archives are not supported.")
        key = "/".join(parts).casefold()
        if key in seen:
            raise InstallError("Archive contains duplicate or case-conflicting paths.")
        seen.add(key)
        result[parts] = entry
    files = {parts for parts, info in result.items() if not info.is_dir()}
    for parts in result:
        if any(parts[:count] in files for count in range(1, len(parts))):
            raise InstallError("An archive file is also used as a directory.")
    return result


def digest(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def tree_fingerprint(root: Path) -> dict[str, str]:
    if root.is_symlink() or not root.is_dir():
        raise InstallError(f"Existing destination is not a regular directory: {root.name}")
    result = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        base = Path(directory)
        for name in dirs + files:
            path = base / name
            if path.is_symlink():
                raise InstallError(f"Existing plugin contains a symbolic link: {root.name}")
        dirs[:] = [name for name in dirs if name.casefold() != "intermediate"]
        for name in files:
            path = base / name
            relative = path.relative_to(root)
            if ignored(relative.parts):
                continue
            if not path.is_file():
                raise InstallError(f"Existing plugin contains a special file: {root.name}")
            result[relative.as_posix()] = digest(path)
    return result


def install(archive_path: Path, project_root: Path) -> dict:
    project_root = project_root.expanduser().resolve()
    archive_path = archive_path.expanduser().resolve()
    if not (project_root / "Vroom.uproject").is_file():
        raise InstallError("Project root must contain Vroom.uproject.")
    destination = project_root / "Plugins"
    if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
        raise InstallError("Plugins must be a regular directory, not a link or file.")
    receipt_path = destination / ".vroom-nvidia.json"
    if receipt_path.is_symlink():
        raise InstallError("Installation receipt must not be a symbolic link.")

    with zipfile.ZipFile(archive_path) as archive:
        entries = safe_entries(archive)
        roots, descriptors = {}, {}
        for plugin in PLUGINS:
            candidates = [parts for parts, info in entries.items()
                          if not info.is_dir() and len(parts) >= 2
                          and parts[-2:] == (plugin, plugin + ".uplugin")]
            if len(candidates) != 1:
                raise InstallError(f"Expected one unambiguous {plugin}.uplugin descriptor.")
            descriptor_path = candidates[0]
            if entries[descriptor_path].file_size > 1024 * 1024:
                raise InstallError(f"Descriptor is too large: {plugin}")
            try:
                descriptor = json.loads(archive.read(entries[descriptor_path]).decode("utf-8-sig"))
            except (UnicodeError, json.JSONDecodeError) as error:
                raise InstallError(f"Invalid JSON descriptor: {plugin}") from error
            if not isinstance(descriptor, dict) or not re.fullmatch(
                    r"5\.8(?:\.\d+)?(?:[-+].*)?", str(descriptor.get("EngineVersion", ""))):
                raise InstallError(f"{plugin} must target Unreal Engine 5.8.")
            root = descriptor_path[:-1]
            if not any(parts[:len(root) + 1] == root + ("Source",)
                       and parts[-1].endswith(".Build.cs") for parts in entries):
                raise InstallError(f"Source build files are missing: {plugin}")
            roots[plugin], descriptors[plugin] = root, descriptor

        destination.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".nvidia-stage-", dir=destination) as staging_name:
            staging = Path(staging_name)
            for plugin, root in roots.items():
                for parts, info in entries.items():
                    if parts[:len(root)] != root or info.is_dir():
                        continue
                    relative = parts[len(root):]
                    if ignored(relative):
                        continue
                    target = staging / plugin / Path(*relative)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(info) as source, target.open("xb") as output:
                        shutil.copyfileobj(source, output, length=1024 * 1024)
                # Intermediate precompiled manifests are intentionally omitted.
                # Mark these project plugins as source builds so UBT regenerates
                # their engine modules; keep NVIDIA runtime DLLs and libraries.
                descriptors[plugin]["Installed"] = False
                (staging / plugin / (plugin + ".uplugin")).write_text(
                    json.dumps(descriptors[plugin], ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

            fingerprints = {plugin: tree_fingerprint(staging / plugin) for plugin in PLUGINS}
            missing = []
            # Check every conflict before placing either plugin.
            for plugin in PLUGINS:
                target = destination / plugin
                if target.exists() or target.is_symlink():
                    if tree_fingerprint(target) != fingerprints[plugin]:
                        raise InstallError(f"Refusing to overwrite different existing plugin: {plugin}")
                else:
                    missing.append(plugin)
            installed = []
            try:
                for plugin in missing:
                    target = destination / plugin
                    if target.exists() or target.is_symlink():
                        raise InstallError(f"Destination appeared during installation: {plugin}")
                    (staging / plugin).rename(target)
                    installed.append(plugin)
            except (InstallError, OSError):
                for plugin in installed:
                    (destination / plugin).rename(staging / plugin)
                raise

            receipt = {
                "schema_version": 1,
                "reference_source_url": OFFICIAL_URL,
                "reference_checked_on": "2026-09-08",
                "archive_sha256": digest(archive_path),
                "engine": "5.8", "source_build": True,
                "plugins": {plugin: {"version": descriptors[plugin].get("VersionName", "unknown"),
                                     "files_sha256": fingerprints[plugin]} for plugin in PLUGINS},
            }
            receipt_temp = staging / "receipt.json"
            receipt_temp.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
            receipt_temp.replace(receipt_path)
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True, help="Local official NVIDIA UE 5.8 ZIP")
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        receipt = install(args.archive, args.project_root)
    except (InstallError, OSError, zipfile.BadZipFile, RuntimeError) as error:
        print(f"DLSS installation refused: {error}", file=sys.stderr)
        return 1
    print("Installed/verified DLSS and StreamlineNGXCommon for a UE 5.8 source build.")
    print("Archive SHA-256: " + receipt["archive_sha256"])
    print("Rebuild on Windows x64; runtime support is checked in the graphics menu.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
