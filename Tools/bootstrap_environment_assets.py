"""Bake low-cost regional tree silhouettes and stone for instanced scenery.

Run with UnrealEditor-Cmd's Python commandlet; --audit is ordinary Python.
Regional tree silhouettes are procedural; pinned CC0 rock and PBR maps are
imported from Content/ArtSource/realistic. Only owned packages are rebuilt.
"""
import json
import math
from pathlib import Path
import random
import runpy
import sys

GENERATOR = 'bootstrap_environment_assets.py:1'
TAG = 'Vroom.AssetGenerator'
LIBRARY = runpy.run_path(str(Path(__file__).with_name('bootstrap_car_assets.py')), run_name='vroom_environment_geometry')
Geometry, ellipsoid, rod = (LIBRARY[n] for n in ('Geometry', 'ellipsoid', 'rod'))
LEAF, BARK, ROCK = range(3)
MATERIALS = ('M_EnvironmentFoliage', 'M_EnvironmentBark', 'M_EnvironmentRock')


def apply_instance_fade(material, helpers, opacity=None, opacity_output='', opacity_clip=.45):
    """Give HISM start/end cull distances an actual gradual material response.

    UE 5.8 MaterialTemplate.ush maps dithered mask coverage to Mask + .5.
    A .5 clip therefore gives alpha=0 no surviving card pixels and alpha=1
    full coverage. Restore the authored cutout before applying distance fade:
    filtered alpha around .5 is a solid needle/leaf, not 50% transparent.
    All mesh slots must opt into dithered LOD transitions.
    """
    import unreal
    expression, connect, output = (helpers[key] for key in ('expression', 'connect', 'output'))
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property('opacity_mask_clip_value', .5)
    material.set_editor_property('dither_opacity_mask', True)
    material.set_editor_property('dithered_lod_transition', True)
    fade = expression(material, unreal.MaterialExpressionPerInstanceFadeAmount)
    if opacity is not None:
        alpha_input = unreal.CustomInput()
        alpha_input.set_editor_property('input_name', 'Alpha')
        coverage = expression(material, unreal.MaterialExpressionCustom,
            output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1, inputs=[alpha_input],
            code=f'return saturate((Alpha - {opacity_clip:.6f}) * 40.0 + 0.5);')
        helpers['require'](unreal.MaterialEditingLibrary.connect_material_expressions(
            opacity, opacity_output, coverage, 'Alpha'), 'Cannot connect authored foliage alpha')
        product = expression(material, unreal.MaterialExpressionMultiply)
        connect(coverage, product, 'A')
        connect(fade, product, 'B')
        fade = product
    output(fade, unreal.MaterialProperty.MP_OPACITY_MASK)


def make_geometry():
    result = {}
    # Sizes are in cm: 10-m trees, pivot at the base. Distinct crown silhouettes
    # use overlapping irregular lobes instead of a single conical primitive.
    for kind, name in enumerate(('Broadleaf', 'Conifer', 'Umbrella', 'Birch')):
        g = Geometry(); rng = random.Random(6150 + kind)
        height = (930, 1100, 870, 1050)[kind]
        rod(g, (0,0,0), (9,-5,height*.77), (22,17,20,13)[kind], BARK, sides=7)
        if kind == 1:
            for level in range(6):
                z = 330 + level * 125
                radius = 255 - level * 33
                for side in range(3):
                    a = level*.63 + side*math.tau/3
                    end = (math.cos(a)*radius*.55,math.sin(a)*radius*.55,z+35)
                    rod(g, (0,0,z-90), end, 5.5, BARK, sides=5)
                    ellipsoid(g, end, (radius*.69,radius*.60,95), LEAF,longitude=7,latitude=3)
            ellipsoid(g, (5,0,1030), (63,65,120), LEAF,longitude=7,latitude=3)
        else:
            count = (9,0,8,7)[kind]
            for n in range(count):
                a = n * 2.39996
                spread = (210,0,290,112)[kind] * (.50 + .5*(n%3)/2)
                z = height - (n%3) * (95 if kind != 2 else 45)
                center = (math.cos(a)*spread, math.sin(a)*spread, z)
                rod(g, (0,0,420 if kind!=3 else 520), center, 7 if kind!=3 else 4, BARK, sides=5)
                radius = (190,0,185,115)[kind] * rng.uniform(.85,1.15)
                ellipsoid(g, center, (radius, radius*rng.uniform(.8,1.1),
                    radius*(.52 if kind == 2 else 1.18)), LEAF, longitude=8,latitude=4)
        result['SM_Environment'+name] = g
    rock = Geometry()
    ellipsoid(rock,(0,0,42),(115,83,94),ROCK,longitude=9,latitude=4)
    result['SM_EnvironmentRock'] = rock
    return result


def build_assets(meshes):
    import unreal
    h = runpy.run_path(str(Path(__file__).with_name('bootstrap_assets.py')), run_name='vroom_environment_helpers')
    require, expression, output, scalar, color, connect = (h[n] for n in ('require','expression','output','scalar','color','connect'))
    library = unreal.EditorAssetLibrary; tools = unreal.AssetToolsHelpers.get_asset_tools()
    def save(asset):
        library.set_metadata_tag(asset,TAG,GENERATOR)
        require(library.save_loaded_asset(asset,only_if_is_dirty=False),'Cannot save '+asset.get_name())
    def owned(path, kind):
        if library.does_asset_exist(path):
            asset=library.load_asset(path)
            require(isinstance(asset,kind) and library.get_metadata_tag(asset,TAG)==GENERATOR,'Foreign environment asset: '+path)
            return asset
        directory,name=path.rsplit('/',1)
        asset=(library.duplicate_asset('/Engine/BasicShapes/Plane',path) if kind==unreal.StaticMesh
               else tools.create_asset(name,directory,kind,unreal.MaterialFactoryNew()))
        require(asset,'Cannot create '+path); save(asset); return asset
    materials=[]
    for i,name in enumerate((*MATERIALS,'M_EnvironmentGround')):
        mat=owned('/Game/Materials/'+name,unreal.Material)
        unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
        mat.set_editor_property('used_with_instanced_static_meshes',True)
        if i==LEAF:
            channels=[expression(mat,unreal.MaterialExpressionPerInstanceCustomData,data_index=n,const_default_value=(.055,.13,.035)[n])for n in range(3)]
            rg=expression(mat,unreal.MaterialExpressionAppendVector);connect(channels[0],rg,'A');connect(channels[1],rg,'B')
            rgb=expression(mat,unreal.MaterialExpressionAppendVector);connect(rg,rgb,'A');connect(channels[2],rgb,'B')
        elif i==3:
            rgb=expression(mat,unreal.MaterialExpressionVertexColor)
        else:
            rgb=color(mat,((.13,.086,.052) if i==BARK else (.22,.225,.21)))
        output(rgb,unreal.MaterialProperty.MP_BASE_COLOR)
        output(scalar(mat,.95),unreal.MaterialProperty.MP_ROUGHNESS)
        output(scalar(mat,.12),unreal.MaterialProperty.MP_SPECULAR)
        unreal.MaterialEditingLibrary.recompile_material(mat);save(mat);materials.append(mat)
    subsystem=(unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
               or unreal.get_default_object(unreal.StaticMeshEditorSubsystem))
    for name,geometry in meshes.items():
        mesh=owned('/Game/Meshes/'+name,unreal.StaticMesh)
        used=sorted({mat for mat,_ in geometry.faces})
        mesh.set_editor_property('static_materials',[unreal.StaticMaterial(material_interface=materials[mat],material_slot_name=MATERIALS[mat])for mat in used])
        description=unreal.StaticMesh.create_static_mesh_description(mesh)
        groups={mat:description.create_polygon_group()for mat in used}
        for mat,group in groups.items():description.set_polygon_group_material_slot_name(group,MATERIALS[mat])
        vertices={}
        for material,face in geometry.faces:
            instances=[]
            for corner,position in enumerate(face):
                key=tuple(round(v,6)for v in position)
                if key not in vertices:
                    vertex=description.create_vertex();description.set_vertex_position(vertex,unreal.Vector(*position));vertices[key]=vertex
                instance=description.create_vertex_instance(vertices[key]);description.set_vertex_instance_uv(instance,unreal.Vector2D(float(corner==1),float(corner==2)),0);instances.append(instance)
            description.create_triangle(groups[material],instances)
        mesh.build_from_static_mesh_descriptions([description],False,False)
        settings=subsystem.get_lod_build_settings(mesh,0);settings.recompute_normals=True;settings.recompute_tangents=True;settings.generate_lightmap_u_vs=False
        subsystem.set_lod_build_settings(mesh,0,settings);save(mesh)
        require(description.get_triangle_count()==len(geometry.faces),'Triangle mismatch '+name)
        unreal.log('VROOM ENVIRONMENT: '+name+': '+str(geometry.audit()))

    build_realistic_assets(owned, save, subsystem, h)
    runpy.run_path(str(Path(__file__).with_name('bootstrap_tree_assets.py')))['build_tree_assets']()
    # The grass generator runs before this environment pass. It already wires
    # blade alpha * instance fade, but a fixed clip made the last whole rows
    # disappear at once. Keep its geometry, GPU wind and ownership unchanged.
    grass = require(library.load_asset('/Game/Materials/M_VergeGrass'), 'Missing verge grass material')
    require(library.get_metadata_tag(grass, TAG) == 'bootstrap_grass_assets.py:1', 'Foreign verge grass material')
    grass.set_editor_property('opacity_mask_clip_value', .5)
    grass.set_editor_property('dither_opacity_mask', True)
    grass.set_editor_property('dithered_lod_transition', True)
    unreal.MaterialEditingLibrary.recompile_material(grass)
    require(library.save_loaded_asset(grass, only_if_is_dirty=False), 'Cannot save grass distance fade')


def build_realistic_assets(owned, save, subsystem, helpers):
    import unreal
    source = runpy.run_path(str(Path(__file__).with_name('environment_asset_data.py')))
    source['verify']()
    root = source['SOURCE']
    require, expression, output, scalar, color, connect, multiply, blend = (
        helpers[n] for n in ('require','expression','output','scalar','color','connect','multiply','blend'))
    library = unreal.EditorAssetLibrary
    editor = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    library.make_directory('/Game/Textures/Environment')

    def texture(filename, name, kind, gl_normal=False):
        path = '/Game/Textures/Environment/' + name
        if library.does_asset_exist(path):
            previous = library.load_asset(path)
            require(library.get_metadata_tag(previous,TAG)==GENERATOR,'Foreign PBR texture '+path)
        task = unreal.AssetImportTask()
        task.filename = str(filename); task.destination_path = '/Game/Textures/Environment'
        task.destination_name = name; task.automated = True; task.replace_existing = True; task.save = False
        tools.import_asset_tasks([task])
        tex = require(library.load_asset(path),'Cannot import PBR texture '+path)
        tex.set_editor_property('srgb',kind=='color')
        tex.set_editor_property('compression_settings',
            unreal.TextureCompressionSettings.TC_NORMALMAP if kind=='normal' else
            unreal.TextureCompressionSettings.TC_MASKS if kind=='rough' else unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_WORLD)
        tex.set_editor_property('max_texture_size',1024)
        tex.set_editor_property('flip_green_channel',gl_normal)
        # Nine compressed 1K maps stay resident: runtime-created mesh sections
        # have no baked streaming bounds. This avoids texture IO in a corner.
        tex.set_editor_property('never_stream',True)
        save(tex)
        return tex

    def maps(identifier, prefix):
        if identifier=='rock_moss_set_02':
            folder=root/identifier/'textures'
            names=(identifier+'_diff_1k.jpg',identifier+'_nor_gl_1k.jpg',identifier+'_rough_1k.jpg')
        else:
            folder=root/identifier
            names=(identifier+'_1K-JPG_Color.jpg',identifier+'_1K-JPG_NormalDX.jpg',identifier+'_1K-JPG_Roughness.jpg')
        return [texture(folder/name,'T_'+prefix+'_'+kind.title(),kind,
            identifier=='rock_moss_set_02' and kind=='normal')
            for name,kind in zip(names,('color','normal','rough'))]

    def pbr(material, textures, world_tile_cm=None, ground_tint=False, stone_variation=False):
        editor.delete_all_material_expressions(material)
        material.set_editor_property('used_with_instanced_static_meshes',True)
        if world_tile_cm:
            world=expression(material,unreal.MaterialExpressionWorldPosition)
            xy=expression(material,unreal.MaterialExpressionComponentMask,r=True,g=True,b=False,a=False)
            connect(world,xy)
            uv=multiply(material,xy,1.0/world_tile_cm)
        else:
            uv=expression(material,unreal.MaterialExpressionTextureCoordinate)
        samples=[]
        for tex,kind in zip(textures,('color','normal','rough')):
            sample=expression(material,unreal.MaterialExpressionTextureSample,texture=tex)
            sample.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind=='normal'
                else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if kind=='rough' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
            connect(uv,sample); samples.append(sample)
        rgb=samples[0]
        if ground_tint:
            # Reuse the same resident colour photograph at a broad, offset
            # scale beyond the verge. One extra sample removes the distant
            # 1.4-m repetition without another texture or procedural noise.
            far_uv = expression(material, unreal.MaterialExpressionAdd)
            connect(multiply(material, uv, 1.0 / 5.7), far_uv, 'A')
            offset = expression(material, unreal.MaterialExpressionConstant2Vector, r=.37, g=.61)
            connect(offset, far_uv, 'B')
            far_color = expression(material, unreal.MaterialExpressionTextureSample, texture=textures[0],
                                   sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
            connect(far_uv, far_color)
            depth = expression(material, unreal.MaterialExpressionPixelDepth)
            distance = expression(material, unreal.MaterialExpressionSubtract)
            connect(depth, distance, 'A'); connect(scalar(material, 2500), distance, 'B')
            fade = expression(material, unreal.MaterialExpressionSaturate)
            connect(multiply(material, distance, 1.0 / 13500.0), fade)
            rgb = blend(material, rgb, far_color, fade)
            tint=multiply(material,expression(material,unreal.MaterialExpressionVertexColor),7.0)
            tint=blend(material,color(material,(1,1,1)),tint,scalar(material,.25))
            product=expression(material,unreal.MaterialExpressionMultiply)
            connect(rgb,product,'A');connect(tint,product,'B');rgb=product
            # Large, aperiodic sun-dried/cool patches break the repeated 1.4-m
            # photograph. Linear-phase sine waves formed artificial parallel
            # stripes across hills, so hash a lattice and interpolate smoothly.
            # Two fixed, unrolled layers need no texture fetch or animation.
            entry=unreal.CustomInput();entry.set_editor_property('input_name','World')
            macro=expression(material,unreal.MaterialExpressionCustom,
                output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,inputs=[entry],code='''
float2 p = World.xy / 6500.0;
float patch = 0.0;
float weight = 0.72;
[unroll]
for (int layer = 0; layer < 2; ++layer)
{
    float2 cell = floor(p);
    float2 f = frac(p);
    // Quintic interpolation has zero first/second derivatives at cell edges.
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float4 hx = frac((cell.x + float4(0, 1, 0, 1)) * 0.1031);
    float4 hy = frac((cell.y + float4(0, 0, 1, 1)) * 0.1030);
    float4 hz = frac((cell.x + float4(0, 1, 0, 1)) * 0.0973);
    float4 d = hx * (hy + 33.33) + hy * (hz + 33.33) + hz * (hx + 33.33);
    hx += d; hy += d; hz += d;
    float4 values = frac((hx + hy) * hz);
    patch += lerp(lerp(values.x, values.y, f.x), lerp(values.z, values.w, f.x), f.y) * weight;
    // A rotated, offset finer layer removes an obvious common grid direction.
    p = float2(p.x * 0.8 - p.y * 0.6, p.x * 0.6 + p.y * 0.8) * 2.71 + float2(17.3, -39.7);
    weight = 0.28;
}
return lerp(float3(0.87, 0.92, 0.88), float3(1.07, 1.03, 0.94), patch);
''')
            connect(world,macro,'World')
            product=expression(material,unreal.MaterialExpressionMultiply)
            connect(rgb,product,'A');connect(macro,product,'B');rgb=product
        if stone_variation:
            variation=expression(material,unreal.MaterialExpressionPerInstanceRandom)
            tint=blend(material,color(material,(.82,.88,.91)),color(material,(1.12,1.06,.94)),variation)
            product=expression(material,unreal.MaterialExpressionMultiply)
            connect(rgb,product,'A');connect(tint,product,'B');rgb=product
        output(rgb,unreal.MaterialProperty.MP_BASE_COLOR)
        output(samples[1],unreal.MaterialProperty.MP_NORMAL)
        output(samples[2],unreal.MaterialProperty.MP_ROUGHNESS)
        output(scalar(material,.0),unreal.MaterialProperty.MP_METALLIC)
        output(scalar(material,.22),unreal.MaterialProperty.MP_SPECULAR)
        if stone_variation:
            apply_instance_fade(material, helpers)
        editor.recompile_material(material)
        require(library.save_loaded_asset(material,only_if_is_dirty=False),'Cannot save PBR '+material.get_name())

    asphalt=maps('Asphalt010','Asphalt')
    ground=maps('Grass004','GroundGrass')
    rock=maps('rock_moss_set_02','MossRock')
    # M_Asphalt belongs to bootstrap_assets.py. Preserve its ownership tag so
    # a full clean bootstrap remains repeatable on the following build.
    asphalt_material=require(library.load_asset('/Game/Materials/M_Asphalt'),'Missing base asphalt material')
    require(library.get_metadata_tag(asphalt_material,TAG)=='bootstrap_assets.py:1','Foreign asphalt material')
    pbr(asphalt_material,asphalt,400.0)
    pbr(owned('/Game/Materials/M_EnvironmentGround',unreal.Material),ground,140.0,True)
    rock_material=owned('/Game/Materials/M_EnvironmentRock',unreal.Material)
    pbr(rock_material,rock,stone_variation=True)

    positions,uvs,indices=source['rock_geometry']()
    mesh=owned('/Game/Meshes/SM_EnvironmentRock',unreal.StaticMesh)
    mesh.set_editor_property('static_materials',[unreal.StaticMaterial(material_interface=rock_material,material_slot_name='M_EnvironmentRock')])
    description=unreal.StaticMesh.create_static_mesh_description(mesh)
    group=description.create_polygon_group();description.set_polygon_group_material_slot_name(group,'M_EnvironmentRock')
    vertices=[]
    for position in positions:
        vertex=description.create_vertex();description.set_vertex_position(vertex,unreal.Vector(*position));vertices.append(vertex)
    for offset in range(0,len(indices),3):
        instances=[]
        for index in indices[offset:offset+3]:
            instance=description.create_vertex_instance(vertices[index])
            description.set_vertex_instance_uv(instance,unreal.Vector2D(*uvs[index]),0);instances.append(instance)
        description.create_triangle(group,instances)
    mesh.build_from_static_mesh_descriptions([description],False,False)
    settings=subsystem.get_lod_build_settings(mesh,0)
    settings.recompute_normals=True;settings.recompute_tangents=True;settings.generate_lightmap_u_vs=False
    subsystem.set_lod_build_settings(mesh,0,settings)
    options=unreal.StaticMeshReductionOptions()
    options.auto_compute_lod_screen_size=False
    # LOD0 is reduced from the 8K source; later LODs use that reduced LOD0.
    options.reduction_settings=[unreal.StaticMeshReductionSettings(percent_triangles=ratio,screen_size=screen)
        for ratio,screen in ((.25,1.0),(.40,.14),(.12,.045))]
    require(subsystem.set_lods(mesh,options)==3,'Cannot build the three rock LODs')
    save(mesh)
    triangles=[mesh.get_num_triangles(i) for i in range(3)]
    require(0<triangles[2]<=320 and triangles[2]<=triangles[1]<=960 and triangles[1]<=triangles[0]<=2400,
        'Rock LOD triangle budget exceeded: '+str(triangles))
    unreal.log('VROOM ENVIRONMENT PBR: PASS 9 compressed 1K maps, CC0 rock LOD triangles='+str(triangles))

if __name__=='__main__':
    geometry=make_geometry();audit={name:mesh.audit()for name,mesh in geometry.items()}
    assert max(m['triangles']for m in audit.values()) <= 1200
    if '--audit' in sys.argv:
        audit['CC0_sources']=runpy.run_path(str(Path(__file__).with_name('environment_asset_data.py')))['audit']()
        audit['final_tree_models']=runpy.run_path(str(Path(__file__).with_name('bootstrap_tree_assets.py')))['audit']()
        print(json.dumps(audit,indent=2))
    else:build_assets(geometry)
