"""Import Valve glove textures and create the ivory racing glove material.

Run with UnrealEditor-Cmd Vroom.uproject -run=pythonscript -script=this_file.
Source 4K images remain untouched; runtime textures are capped at 2048.
"""

from pathlib import Path
import runpy

import unreal


SOURCE = Path(__file__).resolve().parents[1] / "Content/ArtSource/Gloves"
TAG = "Vroom.AssetGenerator"
GENERATOR = "bootstrap_hand_assets.py:1"
LIBRARY = unreal.EditorAssetLibrary
MATERIALS = unreal.MaterialEditingLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

# Load graph helpers without running the scene bootstrap.
HELPERS = runpy.run_path(str(Path(__file__).with_name("bootstrap_assets.py")),
                         run_name="vroom_hand_helpers")
require = HELPERS["require"]
expression = HELPERS["expression"]
connect = HELPERS["connect"]
output = HELPERS["output"]
scalar = HELPERS["scalar"]
color = HELPERS["color"]
multiply = HELPERS["multiply"]
blend = HELPERS["blend"]


def check_owner(path):
    if LIBRARY.does_asset_exist(path):
        asset = require(LIBRARY.load_asset(path), f"Cannot load {path}")
        require(LIBRARY.get_metadata_tag(asset, TAG) == GENERATOR,
                f"Refusing to replace a foreign asset: {path}")


def import_texture(name, filename, normal=False):
    path = f"/Game/Textures/{name}"
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE / filename)
    task.destination_path = "/Game/Textures"
    task.destination_name = name
    task.factory = unreal.TextureFactory()
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = False
    ASSET_TOOLS.import_asset_tasks([task])
    texture = require(LIBRARY.load_asset(path), f"Cannot import {path}")
    require(isinstance(texture, unreal.Texture2D), f"Expected Texture2D: {path}")
    require((texture.blueprint_get_size_x(), texture.blueprint_get_size_y()) == (4096, 4096),
            f"Expected a 4096 x 4096 source: {path}")
    settings = {
        "compression_settings": (unreal.TextureCompressionSettings.TC_NORMALMAP if normal
                                 else unreal.TextureCompressionSettings.TC_DEFAULT),
        "srgb": not normal,
        "flip_green_channel": normal,
        "max_texture_size": 2048,
        "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD,
        "virtual_texture_streaming": False,
        "address_x": unreal.TextureAddress.TA_WRAP,
        "address_y": unreal.TextureAddress.TA_WRAP,
    }
    for key, value in settings.items():
        texture.set_editor_property(key, value)
    LIBRARY.set_metadata_tag(texture, TAG, GENERATOR)
    require(LIBRARY.save_asset(path, only_if_is_dirty=False), f"Cannot save {path}")
    for key, value in settings.items():
        require(texture.get_editor_property(key) == value, f"Setting did not persist: {path}.{key}")
    unreal.log(f"VROOM HAND: {path}: 4K source, max2048, normal={normal}")
    return texture


def binary(material, cls, first, second):
    node = expression(material, cls)
    connect(first, node, "A")
    connect(second, node, "B")
    return node


def saturated(material, value):
    node = expression(material, unreal.MaterialExpressionSaturate)
    connect(value, node)
    return node


def make_material(albedo, normal):
    name = "M_RacingGlove"
    path = f"/Game/Materials/{name}"
    if LIBRARY.does_asset_exist(path):
        material = LIBRARY.load_asset(path)
        MATERIALS.delete_all_material_expressions(material)
    else:
        material = require(ASSET_TOOLS.create_asset(name, "/Game/Materials", unreal.Material,
                           unreal.MaterialFactoryNew()), f"Cannot create {path}")
    material.set_editor_property("two_sided", True)
    sample = expression(material, unreal.MaterialExpressionTextureSample, texture=albedo,
                        sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    channels = []
    for channel in range(3):
        node = expression(material, unreal.MaterialExpressionComponentMask,
                          r=channel == 0, g=channel == 1, b=channel == 2, a=False)
        connect(sample, node)
        channels.append(node)
    red, green, blue = channels
    other = binary(material, unreal.MaterialExpressionMax, green, blue)
    difference = binary(material, unreal.MaterialExpressionSubtract, red, other)
    denominator = binary(material, unreal.MaterialExpressionAdd, red, other)
    denominator = binary(material, unreal.MaterialExpressionAdd, denominator, scalar(material, .002))
    dominance = binary(material, unreal.MaterialExpressionDivide, difference, denominator)
    dominance = binary(material, unreal.MaterialExpressionSubtract, dominance, scalar(material, .08))
    mask = saturated(material, multiply(material, dominance, 3.5))
    # Source red-channel shading retains the honeycomb grain and darker seams.
    detail = saturated(material, multiply(material, red, 3.0))
    detail = binary(material, unreal.MaterialExpressionAdd,
                    multiply(material, detail, .68), scalar(material, .32))
    ivory = binary(material, unreal.MaterialExpressionMultiply,
                   color(material, (.72, .68, .56)), detail)
    output(blend(material, sample, ivory, mask), unreal.MaterialProperty.MP_BASE_COLOR)
    output(scalar(material, .8), unreal.MaterialProperty.MP_ROUGHNESS)
    output(scalar(material, 0.0), unreal.MaterialProperty.MP_METALLIC)
    normal_sample = expression(material, unreal.MaterialExpressionTextureSample, texture=normal,
                               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    output(normal_sample, unreal.MaterialProperty.MP_NORMAL)
    MATERIALS.layout_material_expressions(material)
    MATERIALS.recompile_material(material)
    LIBRARY.set_metadata_tag(material, TAG, GENERATOR)
    require(LIBRARY.save_asset(path, only_if_is_dirty=False), f"Cannot save {path}")
    unreal.log(f"VROOM HAND: {path}: ivory panels, original black/details, roughness .8, metallic0")


def main():
    paths = ("/Game/Textures/T_RacingGloveColor", "/Game/Textures/T_RacingGloveNormal",
             "/Game/Materials/M_RacingGlove")
    for path in paths:
        check_owner(path)
    for name in ("ValveGloveColor.jpg", "ValveGloveNormal.png", "LICENSE"):
        require((SOURCE / name).is_file(), f"Missing source: {SOURCE / name}")
    for directory in ("/Game/Textures", "/Game/Materials"):
        if not LIBRARY.does_directory_exist(directory):
            require(LIBRARY.make_directory(directory), f"Cannot create {directory}")
    albedo = import_texture("T_RacingGloveColor", "ValveGloveColor.jpg")
    normal = import_texture("T_RacingGloveNormal", "ValveGloveNormal.png", normal=True)
    make_material(albedo, normal)
    unreal.log("VROOM HAND: bootstrap complete (2 textures, 1 material)")


if __name__ == "__main__":
    main()
