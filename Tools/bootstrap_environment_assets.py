"""Bake low-cost regional tree silhouettes and stone for instanced scenery.

Run with UnrealEditor-Cmd's Python commandlet; --audit is ordinary Python.
All geometry is original, deterministic and rebuilt only into owned packages.
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

if __name__=='__main__':
    geometry=make_geometry();audit={name:mesh.audit()for name,mesh in geometry.items()}
    assert max(m['triangles']for m in audit.values()) <= 1200
    if '--audit' in sys.argv:print(json.dumps(audit,indent=2))
    else:build_assets(geometry)
