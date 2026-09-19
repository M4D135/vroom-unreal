"""Create the small, reproducible asset set used by the Vroom prototype.

Run inside Unreal Editor 5.8, after building VroomEditor:
  UnrealEditor-Cmd Vroom.uproject -run=pythonscript -script=/absolute/path/to/this.py

The named materials are regenerated on each run. The Circuit map is only
created when missing; the C++ game mode builds the actual scene at runtime.
Billboard source PNGs are versioned in ArtSource. Amiga sprite references live
under docs/reference; they are not imported into the playable cockpit.
For the cockpit materials alone, run bootstrap_cockpit_assets.py instead.
"""

import unreal
import runpy
from pathlib import Path


ASSET_LIBRARY = unreal.EditorAssetLibrary
MATERIAL_LIBRARY = unreal.MaterialEditingLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MATERIAL_DIRECTORY = "/Game/Materials"
MAP_PATH = "/Game/Maps/Circuit"
GENERATED_TAG = "Vroom.AssetGenerator"
GENERATOR_VERSION = "bootstrap_assets.py:1"


def require(result, description):
    if not result:
        raise RuntimeError(description)
    return result


def expression(material, expression_class, **properties):
    node = require(
        MATERIAL_LIBRARY.create_material_expression(material, expression_class),
        "Cannot create material expression {}".format(expression_class),
    )
    for key, value in properties.items():
        node.set_editor_property(key, value)
    return node


def scalar(material, value):
    return expression(material, unreal.MaterialExpressionConstant, r=float(value))


def color(material, rgb):
    return expression(
        material,
        unreal.MaterialExpressionConstant3Vector,
        constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0),
    )


def connect(source, destination, input_name=""):
    require(
        MATERIAL_LIBRARY.connect_material_expressions(source, "", destination, input_name),
        "Cannot connect {} to {}.{}".format(source.get_name(), destination.get_name(), input_name),
    )


def output(node, material_property):
    require(
        MATERIAL_LIBRARY.connect_material_property(node, "", material_property),
        "Cannot connect material property {}".format(material_property),
    )


def blend(material, first, second, alpha):
    node = expression(material, unreal.MaterialExpressionLinearInterpolate)
    connect(first, node, "A")
    connect(second, node, "B")
    connect(alpha, node, "Alpha")
    return node


def world_noise(material, scale):
    """World-space noise avoids stretching on the large ground primitive."""
    # MaterialExpressionNoise.cpp:109-120 uses absolute world position when its
    # Position input is unconnected. Its displayed input name is origin-dependent,
    # so relying on this explicit engine default also avoids fragile pin names.
    noise = expression(
        material,
        unreal.MaterialExpressionNoise,
        scale=scale,
        quality=1,
        levels=1,
        output_min=0.0,
        output_max=1.0,
        turbulence=False,
    )
    return noise


def curb_color(material):
    # Road V = distance / 6 metres. floor(frac(V) * 2) alternates every 3 m.
    uv = expression(material, unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    v = expression(material, unreal.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False)
    connect(uv, v)
    fraction = expression(material, unreal.MaterialExpressionFrac)
    connect(v, fraction)
    twice = expression(material, unreal.MaterialExpressionMultiply, const_b=2.0)
    connect(fraction, twice, "A")
    stripe = expression(material, unreal.MaterialExpressionFloor)
    connect(twice, stripe)
    return blend(material, color(material, (0.58, 0.012, 0.009)), color(material, (0.78, 0.75, 0.66)), stripe)


def multiply(material, first, factor):
    node = expression(material, unreal.MaterialExpressionMultiply, const_b=float(factor))
    connect(first, node, "A")
    return node


def tiled_uv(material, tiling):
    if isinstance(tiling, (int, float)):
        tiling = (tiling, tiling)
    return expression(
        material, unreal.MaterialExpressionTextureCoordinate,
        coordinate_index=0, u_tiling=float(tiling[0]), v_tiling=float(tiling[1]),
    )


def uv_noise(material, tiling, offset=None):
    """Grain follows UV0 during translation, steering and hand articulation."""
    position = expression(material, unreal.MaterialExpressionAppendVector)
    connect(tiled_uv(material, tiling), position, "A")
    connect(scalar(material, 0.0), position, "B")
    if offset is not None:
        shifted = expression(material, unreal.MaterialExpressionAdd)
        connect(position, shifted, "A")
        connect(color(material, offset), shifted, "B")
        position = shifted
    noise = world_noise(material, 1.0)
    # This overrides the node's default world position with (U, V, 0).
    # UE 5.8 MaterialExpressionNoise.cpp/GetInputName and MaterialShared.cpp:6299
    # confirm the absolute-origin pin is "World Position", not "Position".
    connect(position, noise, "World Position")
    return noise


def uv_detail_visibility(material, tiling):
    """Fade subpixel detail smoothly before its frequency reaches Nyquist."""
    uv = tiled_uv(material, tiling)
    footprints = []
    for derivative_class in (unreal.MaterialExpressionDDX, unreal.MaterialExpressionDDY):
        derivative = expression(material, derivative_class)
        connect(uv, derivative)  # Value is the first input in the UE 5.8 headers.
        length = expression(material, unreal.MaterialExpressionLength)
        connect(derivative, length)
        footprints.append(length)
    footprint = expression(material, unreal.MaterialExpressionMax)
    connect(footprints[0], footprint, "A")
    connect(footprints[1], footprint, "B")
    fade = expression(material, unreal.MaterialExpressionSubtract, const_a=2.0)
    connect(multiply(material, footprint, 4.0), fade, "B")
    visible = expression(material, unreal.MaterialExpressionSaturate)
    connect(fade, visible)
    # Full strength below .25 UV cells/pixel, zero at .5 or above. This is a
    # spatial filter, not an animated dither; it works without temporal AA.
    return visible


def uv_micro_normal(material, tiling, strength):
    """A small tangent-space normal from finite differences of UV-fixed noise."""
    base = uv_noise(material, tiling)
    visible = uv_detail_visibility(material, tiling)
    slopes = []
    for offset in ((.18, 0.0, 0.0), (0.0, .18, 0.0)):
        difference = expression(material, unreal.MaterialExpressionSubtract)
        connect(base, difference, "A")
        connect(uv_noise(material, tiling, offset), difference, "B")
        filtered = expression(material, unreal.MaterialExpressionMultiply)
        connect(multiply(material, difference, strength), filtered, "A")
        connect(visible, filtered, "B")
        slopes.append(filtered)
    xy = expression(material, unreal.MaterialExpressionAppendVector)
    connect(slopes[0], xy, "A")
    connect(slopes[1], xy, "B")
    xyz = expression(material, unreal.MaterialExpressionAppendVector)
    connect(xy, xyz, "A")
    connect(scalar(material, 1.0), xyz, "B")
    normal = expression(material, unreal.MaterialExpressionNormalize)
    connect(xyz, normal)  # VectorInput is the first input in UE 5.8.
    return normal


def uv_weave_pattern(material, tiling):
    """Alternating warp/weft cells with soft fibre highlights, all in UV0."""
    uv = tiled_uv(material, tiling)
    cells = []
    strands = []
    for channel in ("r", "g"):
        mask = expression(
            material, unreal.MaterialExpressionComponentMask,
            r=channel == "r", g=channel == "g", b=False, a=False,
        )
        connect(uv, mask)
        cell = expression(material, unreal.MaterialExpressionFloor)
        connect(mask, cell)
        cells.append(cell)
        wave = expression(material, unreal.MaterialExpressionSine, period=1.0)
        connect(multiply(material, mask, 3.0), wave)
        bias = expression(material, unreal.MaterialExpressionAdd, const_b=0.5)
        connect(multiply(material, wave, 0.5), bias, "A")
        strands.append(bias)

    # frac((floor(U) + floor(V)) / 2) * 2 yields the checker selecting which
    # fibre direction passes over the other. Sine stays smooth within each cell.
    checker_sum = expression(material, unreal.MaterialExpressionAdd)
    connect(cells[0], checker_sum, "A")
    connect(cells[1], checker_sum, "B")
    checker_fraction = expression(material, unreal.MaterialExpressionFrac)
    connect(multiply(material, checker_sum, 0.5), checker_fraction)
    checker = multiply(material, checker_fraction, 2.0)
    fibre = blend(material, strands[0], strands[1], checker)
    return blend(material, fibre, checker, scalar(material, 0.20))


def create_material(name, rgb, roughness, metallic=0.0, noise_scale=None, variation=0.0,
                    *, local_noise=None, uv_weave=None, roughness_variation=0.0,
                    emissive=None, mirror_texture_parameter=None, micro_normal=None,
                    filter_detail=False, glass_opacity=None):
    """Optional cockpit detail uses UV0; existing world-noise calls are unchanged.

    local_noise is a UV repetition count or (U, V) pair, so even rotating fingers
    retain their grain. uv_weave is the fibre cell count. emissive is linear RGB.
    micro_normal is (UV tiling, bounded slope strength). glass_opacity is the
    face-on and grazing opacity pair for a clear, undistorted translucent pane.
    """
    path = MATERIAL_DIRECTORY + "/" + name
    material = ASSET_LIBRARY.load_asset(path) if ASSET_LIBRARY.does_asset_exist(path) else None
    if material:
        if not isinstance(material, unreal.Material):
            raise RuntimeError("Asset already exists but is not a material: " + path)
        if ASSET_LIBRARY.get_metadata_tag(material, GENERATED_TAG) != GENERATOR_VERSION:
            raise RuntimeError("Refusing to overwrite an asset not generated by this script: " + path)
        MATERIAL_LIBRARY.delete_all_material_expressions(material)
    else:
        material = require(
            ASSET_TOOLS.create_asset(name, MATERIAL_DIRECTORY, unreal.Material, unreal.MaterialFactoryNew()),
            "Cannot create " + path,
        )

    # Ribbons are visible from either winding, and primitive foliage has no leaf cards.
    material.set_editor_property("two_sided", True)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    if glass_opacity is not None:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        # TLM_Surface is the inexpensive surface-lighting mode explicitly
        # documented for glass in EngineTypes.h. Keep normal depth testing.
        material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)

    base_color = color(material, rgb)
    detail = None
    if name == "M_Curb":
        base_color = curb_color(material)
    elif uv_weave is not None or local_noise is not None:
        if uv_weave is not None:
            detail = uv_weave_pattern(material, uv_weave)
        if local_noise is not None:
            grain = uv_noise(material, local_noise)
            detail = grain if detail is None else blend(material, detail, grain, scalar(material, 0.15))
        if filter_detail:
            noise_tiling = local_noise if isinstance(local_noise, (int, float)) else max(local_noise or (0,))
            # The weave has three finer strands per cell.
            strand_tiling = 3.0 * uv_weave if uv_weave is not None else 0.0
            visible = uv_detail_visibility(material, max(noise_tiling, strand_tiling))
            detail = blend(material, scalar(material, 0.5), detail, visible)
        lighter = tuple(min(1.0, channel * (1.0 + variation)) for channel in rgb)
        darker = tuple(max(0.0, channel * (1.0 - variation)) for channel in rgb)
        base_color = blend(material, color(material, darker), color(material, lighter), detail)
    elif noise_scale is not None:
        lighter = tuple(min(1.0, channel * (1.0 + variation)) for channel in rgb)
        darker = tuple(max(0.0, channel * (1.0 - variation)) for channel in rgb)
        base_color = blend(material, color(material, darker), color(material, lighter), world_noise(material, noise_scale))

    output(base_color, unreal.MaterialProperty.MP_BASE_COLOR)
    roughness_node = scalar(material, roughness)
    if detail is not None and roughness_variation:
        roughness_node = blend(
            material, scalar(material, max(0.0, roughness - roughness_variation)),
            scalar(material, min(1.0, roughness + roughness_variation)), detail,
        )
    output(roughness_node, unreal.MaterialProperty.MP_ROUGHNESS)
    output(scalar(material, metallic), unreal.MaterialProperty.MP_METALLIC)
    output(scalar(material, 0.35), unreal.MaterialProperty.MP_SPECULAR)
    if micro_normal is not None:
        material.set_editor_property("tangent_space_normal", True)
        output(uv_micro_normal(material, *micro_normal), unreal.MaterialProperty.MP_NORMAL)
    if glass_opacity is not None:
        fresnel = expression(material, unreal.MaterialExpressionFresnel,
                             exponent=5.0, base_reflect_fraction=0.0)
        opacity = blend(material, scalar(material, glass_opacity[0]),
                        scalar(material, glass_opacity[1]), fresnel)
        output(opacity, unreal.MaterialProperty.MP_OPACITY)
    if emissive is not None:
        output(color(material, emissive), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if mirror_texture_parameter is not None:
        # Runtime replaces this engine-owned white placeholder with the shared
        # linear HDR rear-view target. No image file is imported or generated.
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mirror_image = expression(
            material, unreal.MaterialExpressionTextureSampleParameter2D,
            parameter_name=mirror_texture_parameter,
            texture=require(ASSET_LIBRARY.load_asset("/Engine/EngineResources/WhiteSquareTexture"),
                            "Cannot load engine white texture for mirror parameter"),
        )
        output(mirror_image, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MATERIAL_LIBRARY.layout_material_expressions(material)
    errors = MATERIAL_LIBRARY.recompile_material(material)
    if errors:
        raise RuntimeError("Material compilation failed for {}: {}".format(name, "; ".join(str(error) for error in errors)))
    ASSET_LIBRARY.set_metadata_tag(material, GENERATED_TAG, GENERATOR_VERSION)
    require(ASSET_LIBRARY.save_asset(path, only_if_is_dirty=False), "Cannot save " + path)
    unreal.log("VROOM ASSETS: saved " + path)


def create_cockpit_materials():
    """Create only the cockpit set, retaining the same foreign-asset protection."""
    if not ASSET_LIBRARY.does_directory_exist(MATERIAL_DIRECTORY):
        require(ASSET_LIBRARY.make_directory(MATERIAL_DIRECTORY), "Cannot create " + MATERIAL_DIRECTORY)
    # UV grain has deliberately restrained contrast; small moving hands must not
    # shimmer. The brushed-metal grain stretches along V to form fine U streaks.
    create_material("M_GloveLeather", (0.73, 0.69, 0.57), 0.78,
                    local_noise=84, variation=0.045, roughness_variation=0.045,
                    micro_normal=(48, 0.20), filter_detail=True)
    create_material("M_SuitFabric", (0.72, 0.70, 0.64), 0.94,
                    uv_weave=32, local_noise=72, variation=0.03, roughness_variation=0.02,
                    micro_normal=(42, 0.14), filter_detail=True)
    create_material("M_VisorGlass", (0.28, 0.58, 0.68), 0.14,
                    glass_opacity=(0.055, 0.17))
    create_material("M_GlovePatch", (0.032, 0.036, 0.040), 0.88,
                    local_noise=64, variation=0.10, roughness_variation=0.04)
    create_material("M_Carbon", (0.019, 0.023, 0.028), 0.42,
                    uv_weave=38, local_noise=120, variation=0.18, roughness_variation=0.07)
    create_material("M_Dashboard", (0.024, 0.028, 0.032), 0.91,
                    local_noise=76, variation=0.10, roughness_variation=0.045)
    create_material("M_BrushedMetal", (0.45, 0.48, 0.51), 0.46, metallic=0.90,
                    local_noise=(110, 4), variation=0.06, roughness_variation=0.07)
    create_material("M_GaugeFace", (0.005, 0.007, 0.009), 0.90)
    create_material("M_GaugeMark", (0.78, 0.74, 0.61), 0.76,
                    emissive=(0.15, 0.14, 0.11))
    create_material("M_GaugeNeedle", (0.66, 0.017, 0.009), 0.58,
                    emissive=(0.20, 0.012, 0.005))
    create_material("M_GaugeGreen", (0.07, 0.55, 0.04), 0.70,
                    emissive=(0.03, 0.20, 0.012))
    create_material("M_GaugeAmber", (0.90, 0.48, 0.025), 0.70,
                    emissive=(0.25, 0.12, 0.005))
    create_material("M_WheelNavy", (0.015, 0.027, 0.085), 0.80,
                    local_noise=76, variation=0.05)
    create_material("M_CockpitSlate", (0.075, 0.12, 0.22), 0.72)
    create_material("M_GloveBlue", (0.12, 0.39, 0.52), 0.87,
                    local_noise=64, variation=0.06)
    create_material("M_CuffOchre", (0.27, 0.17, 0.048), 0.90)
    create_material("M_MirrorSurface", (0.0, 0.0, 0.0), 1.0,
                    mirror_texture_parameter="MirrorImage")
    unreal.log("VROOM ASSETS: cockpit material bootstrap complete (17 materials)")


def create_scenery_materials():
    """Import reproducible painted signs and create the instanced start lamps."""
    from pathlib import Path
    source = Path(__file__).resolve().parents[1] / 'Content' / 'ArtSource'
    ASSET_LIBRARY.make_directory('/Game/Textures')
    for index in range(1, 5):
        texture_path = '/Game/Textures/T_Ad{:02d}'.format(index)
        previous = ASSET_LIBRARY.load_asset(texture_path) if ASSET_LIBRARY.does_asset_exist(texture_path) else None
        if previous and ASSET_LIBRARY.get_metadata_tag(previous, GENERATED_TAG) != GENERATOR_VERSION:
            raise RuntimeError('Refusing to replace foreign texture: ' + texture_path)
        task = unreal.AssetImportTask()
        task.filename = str(source / 'ad{:02d}.png'.format(index))
        task.destination_path = '/Game/Textures'
        task.destination_name = 'T_Ad{:02d}'.format(index)
        task.automated = True
        task.replace_existing = True
        task.save = False
        ASSET_TOOLS.import_asset_tasks([task])
        texture = require(ASSET_LIBRARY.load_asset(texture_path), 'Cannot import ' + texture_path)
        texture.set_editor_property('srgb', True)
        texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
        texture.set_editor_property('max_texture_size', 2048)
        # Runtime-built sign instances have no baked texture-streaming bounds.
        # Keep these four small atlases resident so close signs retain their text.
        texture.set_editor_property('never_stream', True)
        ASSET_LIBRARY.set_metadata_tag(texture, GENERATED_TAG, GENERATOR_VERSION)
        require(ASSET_LIBRARY.save_asset(texture_path, only_if_is_dirty=False), 'Cannot save ' + texture_path)

        name = 'M_Ad{:02d}'.format(index)
        create_material(name, (.7, .7, .7), .82)
        material = ASSET_LIBRARY.load_asset(MATERIAL_DIRECTORY + '/' + name)
        sample = expression(material, unreal.MaterialExpressionTextureSample,
                            texture=texture)
        output(sample, unreal.MaterialProperty.MP_BASE_COLOR)
        output(multiply(material, sample, .055), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MATERIAL_LIBRARY.recompile_material(material)
        require(ASSET_LIBRARY.save_asset(MATERIAL_DIRECTORY + '/' + name, only_if_is_dirty=False), 'Cannot save ' + name)
    create_material('M_StartLight', (.06, .003, .002), .34)
    material = ASSET_LIBRARY.load_asset(MATERIAL_DIRECTORY + '/M_StartLight')
    lit = expression(material, unreal.MaterialExpressionPerInstanceCustomData, data_index=0, const_default_value=0.0)
    channels = [expression(material, unreal.MaterialExpressionPerInstanceCustomData,
                           data_index=i + 1, const_default_value=(.95, .025, .01)[i]) for i in range(3)]
    rg = expression(material, unreal.MaterialExpressionAppendVector)
    connect(channels[0], rg, 'A'); connect(channels[1], rg, 'B')
    rgb = expression(material, unreal.MaterialExpressionAppendVector)
    connect(rg, rgb, 'A'); connect(channels[2], rgb, 'B')
    emission = expression(material, unreal.MaterialExpressionMultiply)
    connect(multiply(material, rgb, 7.0), emission, 'A')
    connect(lit, emission, 'B')
    output(emission, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MATERIAL_LIBRARY.recompile_material(material)
    require(ASSET_LIBRARY.save_asset(MATERIAL_DIRECTORY + '/M_StartLight', only_if_is_dirty=False), 'Cannot save M_StartLight')
    unreal.log('VROOM ASSETS: scenery bootstrap complete (4 textures, 5 materials)')


def main():
    for directory in (MATERIAL_DIRECTORY, "/Game/Maps"):
        if not ASSET_LIBRARY.does_directory_exist(directory):
            require(ASSET_LIBRARY.make_directory(directory), "Cannot create " + directory)

    # Colours are linear RGB reflectances, not 8-bit sRGB values.
    # Noise scales are in inverse centimetres (Unreal world units).
    create_material("M_Asphalt", (0.065, 0.068, 0.071), 0.92, noise_scale=0.12, variation=0.12)
    create_material("M_Gravel", (0.33, 0.29, 0.20), 1.00, noise_scale=0.04, variation=0.22)
    create_material("M_Grass", (0.080, 0.145, 0.032), 1.00, noise_scale=0.0008, variation=0.18)
    create_material("M_Curb", (0.58, 0.012, 0.009), 0.80)
    create_material("M_White", (0.78, 0.76, 0.68), 0.38)
    create_material("M_Red", (0.61, 0.010, 0.006), 0.25)
    create_material("M_Metal", (0.45, 0.49, 0.52), 0.38, metallic=0.85)
    create_material("M_Dark", (0.038, 0.042, 0.047), 0.68)
    create_material("M_Black", (0.011, 0.012, 0.014), 0.95)
    create_material("M_Foliage", (0.040, 0.100, 0.025), 1.00, noise_scale=0.007, variation=0.20)
    create_material("M_Wood", (0.15, 0.080, 0.033), 0.95)
    create_material("M_HudTeal", (0.02, 0.42, 0.44), 0.38, metallic=0.22, emissive=(0.01, 0.12, 0.13))
    create_material("M_HudGold", (0.55, 0.28, 0.02), 0.45, metallic=0.35, emissive=(0.85, 0.42, 0.04))
    create_material("M_HudCyan", (0.12, 0.72, 0.78), 0.32, metallic=0.12, emissive=(0.22, 1.55, 1.75))
    create_material("M_HudUnlit", (1.0, 1.0, 1.0), 1.0)
    hud = ASSET_LIBRARY.load_asset(MATERIAL_DIRECTORY + "/M_HudUnlit")
    hud.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    MATERIAL_LIBRARY.delete_all_material_expressions(hud)
    hud_tex = expression(hud, unreal.MaterialExpressionTextureSampleParameter2D)
    hud_tex.set_editor_property("parameter_name", "Texture")
    output(hud_tex, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MATERIAL_LIBRARY.recompile_material(hud)
    require(ASSET_LIBRARY.save_asset(MATERIAL_DIRECTORY + "/M_HudUnlit", only_if_is_dirty=False), "Cannot save M_HudUnlit")
    create_cockpit_materials()
    create_scenery_materials()
    runpy.run_path(str(Path(__file__).with_name("bootstrap_hand_assets.py")), run_name="__main__")
    runpy.run_path(str(Path(__file__).with_name("bootstrap_grass_assets.py")), run_name="__main__")
    runpy.run_path(str(Path(__file__).with_name("bootstrap_car_assets.py")), run_name="__main__")
    runpy.run_path(str(Path(__file__).with_name("bootstrap_environment_assets.py")), run_name="__main__")
    runpy.run_path(str(Path(__file__).with_name("bootstrap_feedback_assets.py")), run_name="__main__")

    if not ASSET_LIBRARY.does_asset_exist(MAP_PATH):
        level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        require(level_editor.new_level(MAP_PATH, is_partitioned_world=False), "Cannot create Circuit map")
        require(level_editor.save_current_level(), "Cannot save Circuit map")
        unreal.log("VROOM ASSETS: created " + MAP_PATH)
    else:
        unreal.log("VROOM ASSETS: keeping existing " + MAP_PATH)
    unreal.log("VROOM ASSETS: bootstrap complete (34 materials, 6 textures, 1 map)")


if __name__ == "__main__":
    main()
