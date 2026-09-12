"""Build a small crossed-card grass mesh and GPU wind material for verges.

No external texture or mesh is required. Rebuild only assets tagged by this
generator; authored replacements are never silently overwritten.
"""

from pathlib import Path
import runpy
import unreal

LIBRARY = unreal.EditorAssetLibrary
MATERIALS = unreal.MaterialEditingLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
TAG = "Vroom.AssetGenerator"
GENERATOR = "bootstrap_grass_assets.py:1"
HELPERS = runpy.run_path(str(Path(__file__).with_name("bootstrap_assets.py")),
                         run_name="vroom_grass_helpers")
require = HELPERS["require"]
expression = HELPERS["expression"]
connect = HELPERS["connect"]
output = HELPERS["output"]
scalar = HELPERS["scalar"]
color = HELPERS["color"]


def owned_asset(path, asset_type, factory):
    asset = LIBRARY.load_asset(path) if LIBRARY.does_asset_exist(path) else None
    if asset:
        require(isinstance(asset, asset_type), f"Wrong asset type: {path}")
        require(LIBRARY.get_metadata_tag(asset, TAG) == GENERATOR,
                f"Refusing to overwrite a foreign asset: {path}")
    else:
        directory, name = path.rsplit("/", 1)
        asset = require(ASSET_TOOLS.create_asset(name, directory, asset_type, factory),
                        f"Cannot create {path}")
    return asset


def save(asset):
    LIBRARY.set_metadata_tag(asset, TAG, GENERATOR)
    require(LIBRARY.save_loaded_asset(asset, only_if_is_dirty=False),
            f"Cannot save {asset.get_name()}")


def custom(material, code, output_type, inputs):
    custom_inputs = []
    for name in inputs:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        custom_inputs.append(entry)
    node = expression(material, unreal.MaterialExpressionCustom,
                      code=code, output_type=output_type,
                      inputs=custom_inputs)
    for name, source in inputs.items():
        connect(source, node, name)
    return node


def build_material():
    material = owned_asset("/Game/Materials/M_VergeGrass", unreal.Material,
                           unreal.MaterialFactoryNew())
    MATERIALS.delete_all_material_expressions(material)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value", 0.35)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    uv = expression(material, unreal.MaterialExpressionTextureCoordinate)
    position = expression(material, unreal.MaterialExpressionWorldPosition)
    time = expression(material, unreal.MaterialExpressionTime)
    wind = expression(material, unreal.MaterialExpressionScalarParameter,
                      parameter_name="WindStrength", default_value=1.0)
    fade = expression(material, unreal.MaterialExpressionPerInstanceFadeAmount)
    # Seven individually curved, tapered blades across each card. Pixel
    # derivatives soften the silhouette without a texture or animated dithering.
    surface = custom(material, """
float lane = floor(UV.x * 7.0);
float seed = frac(sin(lane * 12.9898 + 1.7) * 43758.5453);
float h = saturate(UV.y);
float tip = 0.56 + seed * 0.43;
float t = saturate(h / tip);
float bend = (seed - 0.5) * t * t * 0.58;
float x = frac(UV.x * 7.0) - 0.5 - bend;
float width = (0.10 + seed * 0.09) * pow(saturate(1.0 - t), 0.72);
float edge = max(fwidth(x), 0.008);
float blade = saturate((width - abs(x)) / edge + 0.5) * (1.0 - step(tip, h));
float patch = 0.5 + 0.5 * sin(World.x * 0.006 + World.y * 0.009);
float3 base = lerp(float3(0.032, 0.070, 0.012), float3(0.060, 0.105, 0.016), patch);
float3 top = lerp(float3(0.15, 0.235, 0.035), float3(0.29, 0.30, 0.075), seed * 0.55);
float3 col = lerp(base, top, t * t) * (0.82 + seed * 0.32);
return float4(col, blade * Fade);
""", unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                     {"UV": uv, "World": position, "Fade": fade})
    rgb = expression(material, unreal.MaterialExpressionComponentMask,
                     r=True, g=True, b=True, a=False)
    alpha = expression(material, unreal.MaterialExpressionComponentMask,
                       r=False, g=False, b=False, a=True)
    connect(surface, rgb)
    connect(surface, alpha)
    output(rgb, unreal.MaterialProperty.MP_BASE_COLOR)
    output(alpha, unreal.MaterialProperty.MP_OPACITY_MASK)
    output(color(material, (0.09, 0.17, 0.025)), unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    output(scalar(material, 0.88), unreal.MaterialProperty.MP_ROUGHNESS)
    output(scalar(material, 0.18), unreal.MaterialProperty.MP_SPECULAR)
    # UV.y is physically zero at the root. Subdivided cards bend progressively;
    # absolute position supplies coherent gusts without CPU instance animation.
    offset = custom(material, """
float h = saturate(UV.y);
float phase = World.x * 0.014 + World.y * 0.011;
float wave = sin(phase + Time * 1.65) * 0.68
           + sin(phase * 1.73 - Time * 2.35) * 0.32;
float bend = Wind * h * h;
return float3(8.0 * wave, 4.5 * wave, -1.7 * abs(wave)) * bend;
""", unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                    {"UV": uv, "World": position, "Time": time, "Wind": wind})
    output(offset, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MATERIALS.recompile_material(material)
    save(material)
    return material


def build_mesh(material):
    path = "/Game/Meshes/SM_GrassTuft"
    if not LIBRARY.does_asset_exist(path):
        # UE exposes no StaticMeshFactoryNew in Python. Seed a package from
        # the built-in plane, then replace its geometry with our description.
        seed = require(LIBRARY.duplicate_asset("/Engine/BasicShapes/Plane", path),
                       "Cannot create the grass mesh package")
        save(seed)
    mesh = owned_asset(path, unreal.StaticMesh, None)
    mesh.set_editor_property("static_materials", [unreal.StaticMaterial(
        material_interface=material, material_slot_name="Grass")])
    description = unreal.StaticMesh.create_static_mesh_description(mesh)
    group = description.create_polygon_group()
    description.set_polygon_group_material_slot_name(group, "Grass")
    # A metre tall and wide at identity, with four vertical segments per card.
    # Runtime scale varies height/width; both intersecting cards share one draw.
    for axis in range(2):
        for row in range(4):
            corners = ((0, row / 4), (1, row / 4),
                       (1, (row + 1) / 4), (0, (row + 1) / 4))
            vertices = []
            for u, v in corners:
                across = (u - 0.5) * 100.0
                vertex = description.create_vertex()
                description.set_vertex_position(vertex, unreal.Vector(
                    across if axis == 0 else 0,
                    across if axis == 1 else 0, v * 100.0))
                instance = description.create_vertex_instance(vertex)
                description.set_vertex_instance_uv(instance, unreal.Vector2D(u, v), 0)
                vertices.append(instance)
            description.create_triangle(group, [vertices[0], vertices[1], vertices[2]])
            description.create_triangle(group, [vertices[0], vertices[2], vertices[3]])
    mesh.build_from_static_mesh_descriptions([description], False, False)
    # Commandlets do not instantiate every editor subsystem. These mesh-only
    # functions also work on the subsystem CDO and do not need an open editor.
    subsystem = (unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
                 or unreal.get_default_object(unreal.StaticMeshEditorSubsystem))
    settings = subsystem.get_lod_build_settings(mesh, 0)
    settings.recompute_normals = True
    settings.recompute_tangents = True
    settings.generate_lightmap_u_vs = False
    subsystem.set_lod_build_settings(mesh, 0, settings)
    # Wind moves vertices by under ten centimetres. Preserve culling bounds.
    mesh.set_editor_property("positive_bounds_extension", unreal.Vector(12, 12, 2))
    mesh.set_editor_property("negative_bounds_extension", unreal.Vector(12, 12, 2))
    save(mesh)
    require(description.get_triangle_count() == 16, "Expected 16 grass triangles")
    unreal.log("VROOM GRASS: crossed tuft mesh (16 triangles) and masked GPU wind material ready")


if __name__ == "__main__":
    build_mesh(build_material())
