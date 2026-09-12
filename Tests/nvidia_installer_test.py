#!/usr/bin/env python3
"""Exercise the offline installer with synthetic files, never NVIDIA binaries."""
import importlib.util
import json
from pathlib import Path
import stat
import tempfile
import unittest
import zipfile


SPEC = importlib.util.spec_from_file_location(
    "nvidia_installer", Path(__file__).resolve().parents[1] / "Tools/install_nvidia_dlss.py")
INSTALLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALLER)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.project = self.base / "game"
        self.project.mkdir()
        (self.project / "Vroom.uproject").write_text("{}")
        self.archive = self.base / "synthetic.zip"

    def fixture(self, version="5.8.0", extra=None):
        with zipfile.ZipFile(self.archive, "w") as archive:
            for plugin in INSTALLER.PLUGINS:
                root = f"OfficialPackage/Plugins/{plugin}/"
                archive.writestr(root + plugin + ".uplugin", json.dumps(
                    {"EngineVersion": version, "Installed": True, "VersionName": "fixture"}))
                archive.writestr(root + f"Source/{plugin}/{plugin}.Build.cs", "// synthetic source")
                archive.writestr(root + "Binaries/ThirdParty/Win64/runtime.dll", b"synthetic runtime")
                archive.writestr(root + "Binaries/Win64/debug.pdb", b"omit debug")
                archive.writestr(root + "Intermediate/Build/generated.obj", b"omit intermediate")
            archive.writestr("OfficialPackage/Plugins/Streamline/Source/unused.cpp", b"not selected")
            if extra:
                extra(archive)

    def test_offline_install_preserves_runtime_and_build_sources(self):
        self.fixture()
        receipt = INSTALLER.install(self.archive, self.project)
        for plugin in INSTALLER.PLUGINS:
            root = self.project / "Plugins" / plugin
            self.assertEqual((root / "Binaries/ThirdParty/Win64/runtime.dll").read_bytes(), b"synthetic runtime")
            self.assertTrue((root / f"Source/{plugin}/{plugin}.Build.cs").exists())
            self.assertFalse(json.loads((root / (plugin + ".uplugin")).read_text())["Installed"])
            self.assertFalse((root / "Intermediate").exists())
            self.assertFalse(list(root.rglob("*.pdb")))
        self.assertFalse((self.project / "Plugins/Streamline").exists())
        self.assertEqual(len(receipt["archive_sha256"]), 64)
        self.assertEqual(INSTALLER.install(self.archive, self.project), receipt)

    def test_refuses_mismatched_engine_before_installing(self):
        self.fixture(version="5.7.0")
        with self.assertRaisesRegex(INSTALLER.InstallError, "Engine 5.8"):
            INSTALLER.install(self.archive, self.project)
        self.assertFalse((self.project / "Plugins").exists())

    def test_refuses_path_traversal_even_in_unselected_files(self):
        self.fixture(extra=lambda archive: archive.writestr("Unselected/../../escape", "bad"))
        with self.assertRaisesRegex(INSTALLER.InstallError, "unsafe path"):
            INSTALLER.install(self.archive, self.project)
        self.assertFalse((self.base / "escape").exists())
        self.assertFalse((self.project / "Plugins").exists())

    def test_refuses_archive_symlink(self):
        def add_link(archive):
            entry = zipfile.ZipInfo("Unselected/link")
            entry.create_system = 3
            entry.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(entry, "../../escape")
        self.fixture(extra=add_link)
        with self.assertRaisesRegex(INSTALLER.InstallError, "links"):
            INSTALLER.install(self.archive, self.project)

    def test_different_second_plugin_leaves_first_uninstalled(self):
        self.fixture()
        existing = self.project / "Plugins/StreamlineNGXCommon"
        existing.mkdir(parents=True)
        (existing / "user-change.txt").write_text("preserve me")
        with self.assertRaisesRegex(INSTALLER.InstallError, "overwrite different"):
            INSTALLER.install(self.archive, self.project)
        self.assertEqual((existing / "user-change.txt").read_text(), "preserve me")
        self.assertFalse((self.project / "Plugins/DLSS").exists())
        self.assertFalse(list((self.project / "Plugins").glob(".nvidia-stage-*")))

    def test_refuses_case_conflicting_paths(self):
        self.fixture(extra=lambda archive: (
            archive.writestr("Extra/FILE.txt", "one"), archive.writestr("Extra/file.txt", "two")))
        with self.assertRaisesRegex(INSTALLER.InstallError, "case-conflicting"):
            INSTALLER.install(self.archive, self.project)


if __name__ == "__main__":
    unittest.main()
