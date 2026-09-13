"""Build lightweight, contoured early-1990s open-wheel racing cars.

Run with UnrealEditor-Cmd's Python commandlet to save assets. ``--audit`` runs
the geometry checks with ordinary Python and writes no Unreal assets.
Only packages tagged by this generator may be rebuilt.
"""

import json
import math
from pathlib import Path
import runpy
import sys


GENERATOR = "bootstrap_car_assets.py:1"
TAG = "Vroom.AssetGenerator"
PAINT, PEARL, CARBON, RUBBER, ALLOY = range(5)
MATERIAL_NAMES = ("M_F1Paint", "M_F1Pearl", "M_F1Carbon", "M_F1Rubber", "M_F1Alloy")


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def unit(v):
    length = math.sqrt(sum(x * x for x in v))
    return tuple(x / length for x in v)


class Geometry:
    def __init__(self):
        self.faces = []

    def tri(self, a, b, c, material):
        if sum(x * x for x in cross(sub(b, a), sub(c, a))) > 1e-10:
            self.faces.append((material, (a, b, c)))

    def quad(self, a, b, c, d, material):
        self.tri(a, b, c, material)
        self.tri(a, c, d, material)

    def merge(self, other, offset=(0, 0, 0), material=None):
        for mat, vertices in other.faces:
            if material is None or material == mat:
                self.faces.append((mat, tuple(tuple(v[i] + offset[i] for i in range(3)) for v in vertices)))

    def audit(self):
        points = [v for _, face in self.faces for v in face]
        assert points and all(math.isfinite(x) for point in points for x in point)
        assert all(sum(x * x for x in cross(sub(b, a), sub(c, a))) > 1e-10 for _, (a, b, c) in self.faces)
        return {"triangles": len(self.faces),
                "bounds_cm": [[round(min(p[i] for p in points), 2) for i in range(3)],
                              [round(max(p[i] for p in points), 2) for i in range(3)]],
                "material_sections": len(set(mat for mat, _ in self.faces))}


def loft(mesh, sections, material, y_offset=0):
    """Bevelled cross sections along the car's X axis; no engine cubes."""
    rings = []
    for x, width, low, high in sections:
        bevel = (high - low) * .22
        yz = ((width, low + bevel), (width, high - bevel), (.68 * width, high),
              (-.68 * width, high), (-width, high - bevel), (-width, low + bevel),
              (-.68 * width, low), (.68 * width, low))
        rings.append([(x, y + y_offset, z) for y, z in yz])
    for first, second in zip(rings, rings[1:]):
        for i in range(8):
            j = (i + 1) % 8
            mesh.quad(first[i], first[j], second[j], second[i], material)
    for i in range(1, 7):
        mesh.tri(rings[0][0], rings[0][i + 1], rings[0][i], material)
        mesh.tri(rings[-1][0], rings[-1][i], rings[-1][i + 1], material)


def rod(mesh, a, b, radius, material, sides=6):
    direction = unit(sub(b, a))
    reference = (0, 0, 1) if abs(direction[2]) < .9 else (0, 1, 0)
    u = unit(cross(direction, reference))
    v = cross(direction, u)
    rings = [[tuple(center[j] + radius * (u[j] * math.cos(i * math.tau / sides)
              + v[j] * math.sin(i * math.tau / sides)) for j in range(3))
              for i in range(sides)] for center in (a, b)]
    for i in range(sides):
        j = (i + 1) % sides
        mesh.quad(rings[0][i], rings[0][j], rings[1][j], rings[1][i], material)
        mesh.tri(a, rings[0][j], rings[0][i], material)
        mesh.tri(b, rings[1][i], rings[1][j], material)


def wing(mesh, x, z, span, chord, material, y_center=0):
    # Thin cambered profile, with a pointed trailing edge and rounded nose.
    profile = ((x + chord * .5, z + 1), (x + chord * .3, z + 4),
               (x - chord * .48, z + 6), (x - chord * .5, z + 3),
               (x + chord * .35, z))
    rings = [[(px, y_center + side * span * .5, pz) for px, pz in profile] for side in (-1, 1)]
    for i in range(len(profile)):
        j = (i + 1) % len(profile)
        mesh.quad(rings[0][i], rings[1][i], rings[1][j], rings[0][j], material)
    for i in range(1, len(profile) - 1):
        mesh.tri(rings[0][0], rings[0][i], rings[0][i + 1], material)
        mesh.tri(rings[1][0], rings[1][i + 1], rings[1][i], material)


def ellipsoid(mesh, center, radii, material, longitude=14, latitude=7,
              theta_range=(0, math.pi), phi_range=(0, math.tau)):
    def point(row, col):
        theta = theta_range[0] + (theta_range[1] - theta_range[0]) * row / latitude
        phi = phi_range[0] + (phi_range[1] - phi_range[0]) * col / longitude
        return (center[0] + radii[0] * math.sin(theta) * math.cos(phi),
                center[1] + radii[1] * math.sin(theta) * math.sin(phi),
                center[2] + radii[2] * math.cos(theta))
    for row in range(latitude):
        for col in range(longitude):
            mesh.quad(point(row, col), point(row + 1, col),
                      point(row + 1, col + 1), point(row, col + 1), material)


def wheel(radius, width, segments=20):
    mesh = Geometry()
    profile = ((-.5 * width, .46 * radius), (-.5 * width, .84 * radius),
               (-.4 * width, radius), (.4 * width, radius),
               (.5 * width, .84 * radius), (.5 * width, .46 * radius))
    rings = [[(r * math.cos(i * math.tau / segments), y, r * math.sin(i * math.tau / segments))
              for i in range(segments)] for y, r in profile]
    for first, second in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i + 1) % segments
            # MeshDescription derives normals from cross(C-A, B-A). The
            # opposite order culled the outside of the tyre, exposing its rim.
            mesh.quad(first[i], first[j], second[j], second[i], RUBBER)
    for side in (-1, 1):
        y = side * width * .505
        outer = [(radius * .465 * math.cos(i * math.tau / segments), y,
                  radius * .465 * math.sin(i * math.tau / segments)) for i in range(segments)]
        inner = [(radius * .385 * math.cos(i * math.tau / segments), y,
                  radius * .385 * math.sin(i * math.tau / segments)) for i in range(segments)]
        back = [(x, y - side * 4, z) for x, _, z in inner]
        for i in range(segments):
            j = (i + 1) % segments
            face = (outer[i], inner[i], inner[j], outer[j])
            mesh.quad(*(face if side < 0 else face[::-1]), ALLOY)
            barrel = (inner[i], back[i], back[j], inner[j])
            mesh.quad(*(barrel if side < 0 else barrel[::-1]), ALLOY)
        for spoke in range(5):
            angle = spoke * math.tau / 5
            face = []
            for r, a in ((5, angle - .26), (radius * .4, angle - .10),
                         (radius * .4, angle + .10), (5, angle + .26)):
                face.append((r * math.cos(a), y - side * 1.5, r * math.sin(a)))
            mesh.quad(*(face if side > 0 else face[::-1]), ALLOY)
    # Keep the generic rod helper unchanged for the body assets. The wheel's
    # closed hub needs outward-facing walls and caps under Unreal's winding.
    hub = Geometry()
    rod(hub, (0, -width * .52, 0), (0, width * .52, 0), 5.5, ALLOY, 10)
    mesh.faces.extend((material, face[::-1]) for material, face in hub.faces)
    return mesh


def make_geometry():
    body = Geometry()
    # Nose narrows smoothly to its low tip; monocoque has an open driver's well.
    loft(body, ((42, 28, 34, 75), (80, 22, 31, 61), (130, 15, 25, 47),
                (188, 10, 23, 36), (225, 5, 24, 30)), PAINT)
    for side in (-1, 1):
        loft(body, ((-65, 7, 34, 83), (-35, 8, 33, 79), (12, 7, 33, 76),
                    (43, 5, 34, 75)), PAINT, side * 30)
        loft(body, ((-149, 10, 28, 43), (-119, 22, 25, 60), (-54, 26, 24, 68),
                    (12, 24, 27, 65), (48, 15, 33, 50)), PAINT, side * 54)
        # Contrasting swept white side panel follows the pod's outer slope.
        y = side * 80.15
        face = ((-108, y, 46), (-39, y, 59), (-30, y, 65), (-119, y, 53))
        body.quad(*(face if side > 0 else face[::-1]), PEARL)
        # Intake mouths sit forward of the radiators, against the sidepods.
        face = ((47.5, side * 44, 37), (47.5, side * 66, 37),
                (42, side * 68, 48), (42, side * 44, 49))
        body.quad(*(face if side > 0 else face[::-1]), CARBON)
        # Paired wishbones, lower links and steering track rods.
        for a, b in (((56, side * 27, 54), (128, side * 79, 42)),
                     ((106, side * 22, 24), (128, side * 79, 29)),
                     ((77, side * 25, 33), (128, side * 79, 34)),
                     ((-60, side * 32, 54), (-115, side * 82, 44)),
                     ((-148, side * 24, 28), (-115, side * 82, 30))):
            rod(body, a, b, 1.7, CARBON)
        rod(body, (35, side * 26, 69), (37, side * 48, 76), 1.7, CARBON)
        loft(body, ((30, 6, 74, 81), (42, 6, 74, 81)), PAINT, side * 49)
        # Upper body / gloved hands visible from chase camera.
        rod(body, (-12, side * 16, 71), (24, side * 16, 66), 6, PEARL)
    loft(body, ((-169, 56, 17, 20), (-127, 77, 17, 20),
                (46, 74, 17, 20), (93, 24, 18, 21)), CARBON)
    loft(body, ((-159, 8, 36, 48), (-116, 26, 40, 72),
                (-76, 24, 42, 103), (-54, 12, 64, 119)), PAINT)
    # Airbox inlet, shoulder bolster, floor and black open cockpit recess.
    ellipsoid(body, (-53, 0, 106), (1.1, 8, 9), CARBON, 10, 5)
    loft(body, ((-55, 23, 44, 48), (34, 23, 44, 48)), CARBON)
    loft(body, ((-58, 25, 55, 85), (-47, 25, 55, 85)), CARBON)
    ellipsoid(body, (-16, 0, 93), (16, 14, 18), PAINT)
    ellipsoid(body, (-16, 0, 93), (16.25, 14.25, 18.25), CARBON, 10, 2,
              (.36 * math.pi, .57 * math.pi), (-1.18, 1.18))
    ellipsoid(body, (-16, 0, 93), (16.4, 14.4, 18.4), PEARL, 10, 1,
              (.63 * math.pi, .69 * math.pi), (-1.6, 1.6))
    # Nose livery follows its top surface and remains legible at racing distance.
    body.quad((84, -11, 60.1), (156, -6, 42.3), (156, 6, 42.3), (84, 11, 60.1), PEARL)
    # Raised rear wing with separate cambered main plane and flap.
    for side in (-1, 1):
        loft(body, ((-193, 2, 73, 109), (-144, 2, 77, 109)), PAINT, side * 96)
        rod(body, (-163, side * 15, 44), (-169, side * 15, 99), 3, CARBON)
    wing(body, -167, 96, 190, 48, CARBON)
    wing(body, -184, 105, 189, 19, PEARL)
    # Front wing has a separate pivot so existing crash deformation stays local.
    front = Geometry()
    wing(front, 0, 0, 204, 40, CARBON)
    for side in (-1, 1):
        loft(front, ((-22, 1.7, -2, 18), (21, 1.7, -2, 15)), PAINT, side * 102)
        # The split upper flaps sit outside the nose, not through it.
        wing(front, -11, 6, 78, 19, PEARL, side * 61)
    # The thin centre support is tucked directly beneath the nose tip.
    loft(front, ((-6, 7, 5, 12), (10, 6, 5, 11)), PAINT)

    meshes = {}
    for name, material in (("Paint", PAINT), ("Trim", PEARL), ("Carbon", CARBON)):
        player = Geometry(); player.merge(body, material=material)
        opponent = Geometry(); opponent.merge(player); opponent.merge(front, (208, 0, 24), material)
        meshes[f"SM_F1Player{name}"] = player
        meshes[f"SM_F1Opponent{name}"] = opponent
    meshes["SM_F1FrontWing"] = front
    # The player's close cockpit view needs a smooth tyre silhouette. Keep
    # the shared opponent assembly at its existing distant-view budget.
    meshes["SM_F1WheelFront"] = wheel(35, 38, segments=40)
    meshes["SM_F1WheelRear"] = wheel(38, 46, segments=40)
    opponent_front, opponent_rear = wheel(35, 38), wheel(38, 46)
    opponents_wheels = Geometry()
    for side in (-1, 1):
        opponents_wheels.merge(opponent_front, (128, side * 91, 35))
        opponents_wheels.merge(opponent_rear, (-115, side * 96, 38))
    meshes["SM_F1OpponentWheels"] = opponents_wheels
    return meshes


def build_assets(meshes):
    import unreal
    library = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    helpers = runpy.run_path(str(Path(__file__).with_name("bootstrap_assets.py")), run_name="vroom_car_helpers")
    require, expression, output = (helpers[name] for name in ("require", "expression", "output"))
    scalar, color, connect = (helpers[name] for name in ("scalar", "color", "connect"))

    def save(asset):
        library.set_metadata_tag(asset, TAG, GENERATOR)
        require(library.save_loaded_asset(asset, only_if_is_dirty=False), f"Cannot save {asset.get_name()}")

    def owned(path, kind):
        if library.does_asset_exist(path):
            asset = library.load_asset(path)
            require(isinstance(asset, kind), f"Wrong asset type: {path}")
            require(library.get_metadata_tag(asset, TAG) == GENERATOR, f"Refusing to overwrite foreign asset: {path}")
            return asset
        directory, name = path.rsplit("/", 1)
        if kind == unreal.StaticMesh:
            asset = require(library.duplicate_asset("/Engine/BasicShapes/Plane", path), f"Cannot create {path}")
        else:
            asset = require(tools.create_asset(name, directory, kind, unreal.MaterialFactoryNew()), f"Cannot create {path}")
        save(asset)
        return asset

    materials = []
    tones = ((.72, .012, .018), (.82, .85, .9), (.012, .016, .021), (.017, .02, .024), (.42, .46, .52))
    for index, name in enumerate(MATERIAL_NAMES):
        material = owned("/Game/Materials/" + name, unreal.Material)
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        material.set_editor_property("used_with_instanced_static_meshes", True)
        if index == PAINT:
            channels = [expression(material, unreal.MaterialExpressionPerInstanceCustomData,
                                   data_index=i, const_default_value=tones[index][i]) for i in range(3)]
            rg = expression(material, unreal.MaterialExpressionAppendVector)
            connect(channels[0], rg, "A"); connect(channels[1], rg, "B")
            rgb = expression(material, unreal.MaterialExpressionAppendVector)
            connect(rg, rgb, "A"); connect(channels[2], rgb, "B")
            output(rgb, unreal.MaterialProperty.MP_BASE_COLOR)
        else:
            output(color(material, tones[index]), unreal.MaterialProperty.MP_BASE_COLOR)
        output(scalar(material, (.18, .15, .35, 0, .88)[index]), unreal.MaterialProperty.MP_METALLIC)
        output(scalar(material, (.27, .31, .46, .78, .24)[index]), unreal.MaterialProperty.MP_ROUGHNESS)
        output(scalar(material, (.5, .5, .45, .24, .55)[index]), unreal.MaterialProperty.MP_SPECULAR)
        unreal.MaterialEditingLibrary.recompile_material(material)
        save(material); materials.append(material)

    subsystem = (unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
                 or unreal.get_default_object(unreal.StaticMeshEditorSubsystem))
    for name, geometry in meshes.items():
        mesh = owned("/Game/Meshes/" + name, unreal.StaticMesh)
        used = sorted(set(mat for mat, _ in geometry.faces))
        mesh.set_editor_property("static_materials", [unreal.StaticMaterial(
            material_interface=materials[mat], material_slot_name=MATERIAL_NAMES[mat]) for mat in used])
        description = unreal.StaticMesh.create_static_mesh_description(mesh)
        groups = {mat: description.create_polygon_group() for mat in used}
        for mat, group in groups.items():
            description.set_polygon_group_material_slot_name(group, MATERIAL_NAMES[mat])
        vertices = {}
        for material, face in geometry.faces:
            instances = []
            for corner, position in enumerate(face):
                key = tuple(round(v, 6) for v in position)
                if key not in vertices:
                    vertex = description.create_vertex()
                    description.set_vertex_position(vertex, unreal.Vector(*position))
                    vertices[key] = vertex
                instance = description.create_vertex_instance(vertices[key])
                description.set_vertex_instance_uv(instance, unreal.Vector2D(float(corner == 1), float(corner == 2)), 0)
                instances.append(instance)
            description.create_triangle(groups[material], instances)
        mesh.build_from_static_mesh_descriptions([description], False, False)
        settings = subsystem.get_lod_build_settings(mesh, 0)
        settings.recompute_normals = True
        settings.recompute_tangents = True
        settings.generate_lightmap_u_vs = False
        subsystem.set_lod_build_settings(mesh, 0, settings)
        save(mesh)
        require(description.get_triangle_count() == len(geometry.faces), f"Triangle mismatch: {name}")
        unreal.log(f"VROOM CAR: {name}: {geometry.audit()}")


if __name__ == "__main__":
    geometry = make_geometry()
    audit = {name: mesh.audit() for name, mesh in geometry.items()}
    opponent_triangles = sum(audit[f"SM_F1Opponent{part}"]["triangles"] for part in ("Paint", "Trim", "Carbon", "Wheels"))
    assert opponent_triangles <= 3600, f"Car geometry budget exceeded: {opponent_triangles}"
    if "--audit" in sys.argv:
        print(json.dumps({"opponent_triangles": opponent_triangles, "meshes": audit}, indent=2))
    else:
        build_assets(geometry)
