"""Import seven reference textures for inspection in Unreal Editor 5.8.
This optional script is not part of the game bootstrap or cooked content.

UnrealEditor-Cmd Vroom.uproject -run=pythonscript -script=/absolute/path/to/this.py
"""

import hashlib
import json
from pathlib import Path
import struct

import unreal


SOURCE = Path(__file__).resolve().parents[1] / "docs/reference/amiga"
DESTINATION = "/Game/DeveloperReference"
TAG = "Vroom.AssetGenerator"
GENERATOR = "bootstrap_amiga_assets.py:1"


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def main():
    library = unreal.EditorAssetLibrary
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    manifest = json.loads((SOURCE / "cockpit.json").read_text(encoding="utf-8"))
    poses = manifest["poses"]
    require(len(poses) == 7, "Expected exactly seven Amiga cockpit poses")
    pending = []
    # Validate every input and ownership before importing any texture.
    for index, pose in enumerate(poses):
        name = f"T_AmigaCockpit{index:02d}"
        target = f"{DESTINATION}/{name}"
        source = SOURCE / f"cockpit_pose_{index:02d}.png"
        payload = source.read_bytes()
        require(pose["pose"] == index and pose["texture"] == target, "Unexpected pose naming")
        require(hashlib.sha256(payload).hexdigest() == pose["sha256"], f"Source hash mismatch: {source}")
        require(payload[:8] == b"\x89PNG\r\n\x1a\n", f"Not a PNG: {source}")
        require(struct.unpack_from(">IIBB", payload, 16) == (96, 28, 8, 6), f"Expected a 96x28 RGBA8 source: {source}")
        if library.does_asset_exist(target):
            previous = library.load_asset(target)
            require(library.get_metadata_tag(previous, TAG) == GENERATOR, f"Refusing to replace foreign texture: {target}")
        pending.append((name, target, source))
    if not library.does_directory_exist(DESTINATION):
        require(library.make_directory(DESTINATION), "Cannot create texture directory")
    for name, target, source in pending:
        task = unreal.AssetImportTask()
        task.filename = str(source)
        task.destination_path = DESTINATION
        task.destination_name = name
        task.factory = unreal.TextureFactory()
        task.automated = True
        task.replace_existing = True
        task.replace_existing_settings = True
        task.save = False
        asset_tools.import_asset_tasks([task])
        texture = require(library.load_asset(target), f"Cannot import {target}")
        require(isinstance(texture, unreal.Texture2D), f"Expected Texture2D: {target}")
        require((texture.blueprint_get_size_x(), texture.blueprint_get_size_y()) == (96, 28), f"Unexpected imported dimensions: {target}")
        settings = {
            "compression_settings": unreal.TextureCompressionSettings.TC_EDITOR_ICON,
            "compression_no_alpha": False,
            "lod_group": unreal.TextureGroup.TEXTUREGROUP_UI,
            "filter": unreal.TextureFilter.TF_NEAREST,
            "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
            "srgb": True,
            "never_stream": True,
            "virtual_texture_streaming": False,
            "address_x": unreal.TextureAddress.TA_CLAMP,
            "address_y": unreal.TextureAddress.TA_CLAMP,
        }
        for setting, value in settings.items():
            texture.set_editor_property(setting, value)
        library.set_metadata_tag(texture, TAG, GENERATOR)
        library.set_metadata_tag(texture, "Vroom.AmigaOriginal", manifest["decoded_cockpit_sha256"])
        require(library.save_asset(target, only_if_is_dirty=False), f"Cannot save {target}")
        for setting, value in settings.items():
            require(texture.get_editor_property(setting) == value, f"Texture setting did not persist: {target}.{setting}")
        unreal.log(f"VROOM AMIGA: {target} verified, 96x28 RGBA, UI, nearest, sRGB, no mipmaps")
    unreal.log("VROOM AMIGA: bootstrap complete (7 native cockpit textures)")


if __name__ == "__main__":
    main()
