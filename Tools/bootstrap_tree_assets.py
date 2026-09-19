"""Import the pinned CC0 rubberduck trees without executing the source .blend.

Ordinary Python: --export reads Blender 2.73 SDNA and emits portable mesh JSON;
--audit verifies source/export hashes, UVs, geometry and the three authored LODs.
Unreal Python: build_tree_assets() runs after bootstrap_environment_assets.py.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import random
import runpy
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Content/ArtSource/TreesRealistic'
GENERATOR = 'bootstrap_tree_assets.py:1'
ENVIRONMENT_GENERATOR = 'bootstrap_environment_assets.py:1'
TAG = 'Vroom.AssetGenerator'
TEXTURES = ('bark.JPG', 'branches_leaves.png', 'branches_small_leaves.png')
TREES = (
    ('Broadleaf', ('Mesh.109', 'Mesh.017', 'Mesh.020'), 930.0, 400.0, (880, 525, 321)),
    ('Birch', ('Mesh.012', 'Mesh.021', 'Mesh.023'), 1050.0, 220.0, (726, 476, 278)),
)
BRANCHED_TREES = (('Conifer', 1150.0, 300.0), ('Umbrella', 980.0, 480.0))


class BranchMesh:
    """Small authored-style branch meshes, with real cylindrical/leaf-card UVs."""
    def __init__(self):
        self.data = {'positions': [], 'corners': [], 'triangles': []}
        self.vertices = {}

    def polygon(self, points, uvs, material):
        start = len(self.data['corners'])
        for point, uv in zip(points, uvs):
            key = tuple(point)
            if key not in self.vertices:
                self.vertices[key] = len(self.data['positions'])
                self.data['positions'].append(key)
            # Shared positions permit smooth recalculated bark normals while
            # UV seams remain independent corner attributes.
            self.data['corners'].append([self.vertices[key], *uv])
        for i in range(1, len(points) - 1):
            self.data['triangles'].append([material, start, start + i, start + i + 1])

    def branch(self, centers, radii, sides):
        # Shared ring orientation prevents twisted bark between tapered joints.
        direction = unit([centers[-1][i] - centers[0][i] for i in range(3)])
        reference = (0, 0, 1) if abs(direction[2]) < .9 else (0, 1, 0)
        across = unit(cross(direction, reference))
        up = cross(direction, across)
        rings = [[tuple(center[j] + radius * (across[j] * math.cos(i * math.tau / sides)
                   + up[j] * math.sin(i * math.tau / sides)) for j in range(3))
                 for i in range(sides)] for center, radius in zip(centers, radii)]
        along = 0.0
        for index in range(len(rings) - 1):
            next_along = along + math.dist(centers[index], centers[index + 1]) / 160.0
            for side in range(sides):
                nxt = (side + 1) % sides
                self.polygon((rings[index][side], rings[index + 1][side],
                              rings[index + 1][nxt], rings[index][nxt]),
                             ((side / sides, along), (side / sides, next_along),
                              ((side + 1) / sides, next_along), ((side + 1) / sides, along)), 0)
            along = next_along
        # Buried root and branch tips need no caps; all visible ends are covered
        # by a smaller twig/card. This also avoids triangles hidden in crowns.

    def foliage(self, center, direction, length, width, roll=0.0, segments=2):
        direction = unit(direction)
        across = unit(cross(direction, (0, 0, 1)))
        up = cross(direction, across)
        across = [across[i] * math.cos(roll) + up[i] * math.sin(roll) for i in range(3)]
        # The photographed branch bends in two planes instead of being a flat
        # billboard. Both sides render; no per-frame camera-facing work.
        def point(t, side):
            bend = math.sin(t * math.pi) * length * .09
            return tuple(center[i] + direction[i] * (t - .5) * length
                         + across[i] * side * width * .5 + up[i] * bend for i in range(3))
        for part in range(segments):
            low, high = part / segments, (part + 1) / segments
            self.polygon((point(low, -1), point(high, -1), point(high, 1), point(low, 1)),
                         ((0, 1 - low), (0, 1 - high), (1, 1 - high), (1, 1 - low)), 1)


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def branched_tree_lods(species):
    """Deterministic silhouettes replacing the old opaque polygonal lobes.

    Procedural needle cutouts supply evergreen detail, while the existing CC0
    bark photograph dresses the tapered branches. No deciduous leaf photograph
    is repurposed as pine needles.
    Every LOD retains the same trunk origin and crown envelope.
    """
    lods = []
    for lod in range(3):
        mesh = BranchMesh()
        rng = random.Random(6151 if species == 'Conifer' else 6152)
        sides = (8, 6, 5)[lod]
        if species == 'Conifer':
            centers = [(0, 0, 0), (-5, 3, 240), (4, -4, 550), (6, 1, 850), (0, 0, 1120)]
            mesh.branch(centers, (18, 14, 10, 6, 1), sides)
            # All levels exist in every LOD: only branch/twig density changes.
            # Thus distance changes never shift the apex or remove a full tier.
            for level in range(9):
                z = 295 + level * 93
                length = 275 * (1 - level / 11.0)
                for branch in range(6):
                    angle = level * 1.27 + branch * math.tau / 6
                    reach = length * rng.uniform(.87, 1.03)
                    dz = rng.uniform(-25, 24)
                    if lod and branch % (2 if lod == 1 else 3):
                        continue
                    axis = (math.cos(angle), math.sin(angle), -.16)
                    end = (axis[0] * reach, axis[1] * reach, z + dz - 26)
                    if lod < 2:
                        mesh.branch(((0, 0, z + 15), (end[0] * .52, end[1] * .52, z - 18), end),
                                    (5.6 * (1 - level / 13), 2.8, .7), 3)
                    center = (end[0] * .58, end[1] * .58, z + dz)
                    mesh.foliage(center, axis, reach * 1.16, reach * .62,
                                 -.16, segments=2 if lod == 0 else 1)
                    mesh.foliage((center[0], center[1], center[2] + 8), axis,
                                 reach * 1.02, reach * .58, 1.08, segments=1)
            mesh.foliage((0, 0, 1091), (.035, .02, 1), 112, 79, segments=1)
            mesh.foliage((0, 0, 1091), (.035, .02, 1), 112, 79, 1.57, segments=1)
        else:
            mesh.branch(((0, 0, 0), (5, -4, 200), (-9, 2, 415), (13, 8, 635)),
                        (22, 17, 13, 7), sides)
            for branch in range(9):
                angle = branch * 2.399963
                spread = 225 + (branch % 3) * 48
                end = (math.cos(angle) * spread, math.sin(angle) * spread, 815 + (branch % 3) * 32)
                mesh.branch(((1, 0, 425 + (branch % 2) * 55),
                             (end[0] * .58, end[1] * .58, end[2] - 85), end),
                            (9.5, 5.2, 1.2), (5, 4, 3)[lod])
                # Open, irregular horizontal crown; gaps reveal branching and
                # sky, unlike the old eight solid low-poly ellipsoids.
                for twig in range(5):
                    a = angle + (twig - 2) * .47
                    radius = rng.uniform(70, 108)
                    z = rng.uniform(-18, 20)
                    if lod == 1 and twig in (1, 3):
                        continue
                    if lod == 2 and twig not in (0, 2, 4):
                        continue
                    center = (end[0] + math.cos(a) * radius * .38,
                              end[1] + math.sin(a) * radius * .38, end[2] + z)
                    mesh.foliage(center, (math.cos(a), math.sin(a), .12),
                                 208, 153, .18 + (twig % 2) * .35,
                                 segments=2 if lod == 0 else 1)
                    if lod < 2 or twig == 2:
                        mesh.foliage((center[0], center[1], center[2] - 10),
                                     (math.cos(a + .7), math.sin(a + .7), -.08),
                                     180, 143, 1.10, segments=1)
        lods.append(mesh.data)
    # One shared scale for the complete LOD family preserves the original
    # placement/collision footprint and does not make trees shrink at distance.
    radius_limit = next(radius for name, _, radius in BRANCHED_TREES if name == species)
    scale = min(1.0, radius_limit / max(math.hypot(p[0], p[1]) for lod in lods for p in lod['positions']))
    for lod in lods:
        lod['positions'] = [(p[0] * scale, p[1] * scale, max(0.0, p[2])) for p in lod['positions']]
    return lods


def audit_branched_trees():
    report = {}
    for species, height, radius in BRANCHED_TREES:
        lods = branched_tree_lods(species)
        counts = []
        for lod in lods:
            points = lod['positions']
            assert all(math.isfinite(v) for p in points for v in p)
            assert min(p[2] for p in points) >= -.5
            assert max(p[2] for p in points) <= height
            assert max(math.hypot(p[0], p[1]) for p in points) <= radius + 1.e-6
            assert {t[0] for t in lod['triangles']} == {0, 1}
            for material, *indices in lod['triangles']:
                a, b, c = [points[lod['corners'][i][0]] for i in indices]
                normal = cross([b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)])
                assert sum(v * v for v in normal) > 1.e-12
            counts.append(len(lod['triangles']))
        assert counts[0] <= 1400 and counts[0] > counts[1] > counts[2]
        report[species] = {'lod_triangles': counts, 'height_limit_cm': height,
                           'radius_limit_cm': radius, 'material_sections': 2}
    return report


class BlendReader:
    """Minimal read-only SDNA reader for the pinned Blender 2.73 mesh source."""
    def __init__(self, path):
        raw = Path(path).read_bytes()
        if raw[:12] != b'BLENDER-v273':
            raise ValueError('Expected uncompressed little-endian 64-bit Blender 2.73 source')
        self.blocks = {}
        ordered = []
        position = 12
        while position + 24 <= len(raw):
            code, length, address, schema, count = struct.unpack_from('<4sIQII', raw, position)
            position += 24
            block = (schema, count, raw[position:position + length])
            self.blocks[address] = block
            ordered.append((code, block))
            position += length
            if code == b'ENDB':
                break
        dna = next(block[2] for code, block in ordered if code == b'DNA1')
        position = 4
        def strings(marker):
            nonlocal position
            assert dna[position:position + 4] == marker
            count = struct.unpack_from('<I', dna, position + 4)[0]
            position += 8
            result = []
            for _ in range(count):
                end = dna.index(0, position)
                result.append(dna[position:end].decode('utf8'))
                position = end + 1
            position = (position + 3) & ~3
            return result
        names, types = strings(b'NAME'), strings(b'TYPE')
        assert dna[position:position + 4] == b'TLEN'
        position += 4
        sizes = struct.unpack_from('<' + str(len(types)) + 'H', dna, position)
        position = (position + len(types) * 2 + 3) & ~3
        assert dna[position:position + 4] == b'STRC'
        count = struct.unpack_from('<I', dna, position + 4)[0]
        position += 8
        self.schemas, self.types = [], {}
        for _ in range(count):
            type_index, field_count = struct.unpack_from('<HH', dna, position)
            position += 4
            fields, offset = {}, 0
            for _ in range(field_count):
                field_type, field_name = struct.unpack_from('<HH', dna, position)
                position += 4
                full_name = names[field_name]
                length = 8 if '*' in full_name else sizes[field_type]
                for dimension in re.findall(r'\[(\d+)\]', full_name):
                    length *= int(dimension)
                key = full_name.replace('*', '').split('[')[0]
                fields[key] = (offset, types[field_type], length, '*' in full_name)
                offset += length
            schema = (types[type_index], sizes[type_index], fields)
            self.schemas.append(schema)
            self.types[schema[0]] = schema

    def field(self, body, kind, name, index=0):
        _, size, fields = self.types[kind]
        offset, field_kind, length, pointer = fields[name]
        value = body[index * size + offset:index * size + offset + length]
        if pointer:
            return struct.unpack_from('<Q', value)[0]
        formats = {'char':'b', 'short':'h', 'int':'i', 'float':'f', 'double':'d'}
        if field_kind in formats:
            if field_kind == 'char' and length > 1:
                return value.split(b'\0')[0].decode('utf8', errors='replace')
            fmt = formats[field_kind]
            result = struct.unpack('<' + str(length // struct.calcsize(fmt)) + fmt, value)
            return result[0] if len(result) == 1 else result
        return value

    def array(self, address):
        return self.blocks[address][2]

    def named(self, kind):
        result = {}
        for schema, count, body in self.blocks.values():
            if schema < len(self.schemas) and self.schemas[schema][0] == kind:
                identity = self.field(body, kind, 'id')
                result[self.field(identity, 'ID', 'name')[2:]] = body
        return result


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def unit(vector):
    length = math.sqrt(sum(v * v for v in vector))
    if length < 1.e-10:
        raise ValueError('Invalid zero normal')
    return [v / length for v in vector]


def verify_files(section):
    manifest = json.loads((SOURCE / 'manifest.json').read_text())
    for name, expected in manifest[section].items():
        if digest(SOURCE / name) != expected:
            raise ValueError('Modified pinned tree asset: ' + name)
    return manifest


def source_mesh(reader, mesh, matrix):
    vertex_data = reader.array(reader.field(mesh, 'Mesh', 'mvert'))
    loop_data = reader.array(reader.field(mesh, 'Mesh', 'mloop'))
    uv_data = reader.array(reader.field(mesh, 'Mesh', 'mloopuv'))
    polygon_data = reader.array(reader.field(mesh, 'Mesh', 'mpoly'))
    texture_data = reader.array(reader.field(mesh, 'Mesh', 'mtpoly'))
    def rotate(vector):
        # Blender's obmat columns include its unapplied object rotation/scale.
        # Scene-layout translations are deliberately not baked into a tree.
        return [sum(matrix[column * 4 + row] * vector[column] for column in range(3))
                for row in range(3)]
    positions = [rotate(reader.field(vertex_data, 'MVert', 'co', index))
                 for index in range(reader.field(mesh, 'Mesh', 'totvert'))]
    normals = [unit(rotate(reader.field(vertex_data, 'MVert', 'no', index)))
               for index in range(len(positions))]
    corners = []
    for index in range(reader.field(mesh, 'Mesh', 'totloop')):
        vertex = reader.field(loop_data, 'MLoop', 'v', index)
        u, v = reader.field(uv_data, 'MLoopUV', 'uv', index)
        corners.append([vertex, u, 1.0 - v])
    triangles = []
    for index in range(reader.field(mesh, 'Mesh', 'totpoly')):
        start = reader.field(polygon_data, 'MPoly', 'loopstart', index)
        count = reader.field(polygon_data, 'MPoly', 'totloop', index)
        image = reader.array(reader.field(texture_data, 'MTexPoly', 'tpage', index))
        filename = Path(reader.field(image, 'Image', 'name')).name
        material = TEXTURES.index(filename)
        # Reflect Y without reversing indices: Unreal's front-face convention
        # is the opposite of Blender's counter-clockwise winding.
        for corner in range(1, count - 1):
            triangles.append([material, start, start + corner, start + corner + 1])
    return {'positions': positions, 'normals': normals, 'corners': corners, 'triangles': triangles}


def export_source():
    manifest = verify_files('source_files')
    reader = BlendReader(SOURCE / 'free_vegetation_pack.blend')
    meshes = reader.named('Mesh')
    matrices = {}
    for obj in reader.named('Object').values():
        pointer = reader.field(obj, 'Object', 'data')
        if pointer not in reader.blocks:
            continue
        schema, _, body = reader.blocks[pointer]
        if reader.schemas[schema][0] == 'Mesh':
            name = reader.field(reader.field(body, 'Mesh', 'id'), 'ID', 'name')[2:]
            matrices[name] = reader.field(obj, 'Object', 'obmat')
    result = {'schema': 1, 'units': 'cm', 'materials': TEXTURES, 'trees': {}}
    for species, names, height, radius, budgets in TREES:
        lods = [source_mesh(reader, meshes[name], matrices[name]) for name in names]
        points = lods[0]['positions']
        bottom, top = min(p[2] for p in points), max(p[2] for p in points)
        roots = [p for p in points if p[2] < bottom + (top - bottom) * .01]
        center = [sum(p[axis] for p in roots) / len(roots) for axis in range(2)]
        vertical_scale = height / (top - bottom)
        extent = max(math.hypot(p[0] - center[0], p[1] - center[1]) for p in points)
        lateral_scale = min(vertical_scale, radius / extent)
        for lod in lods:
            lod['positions'] = [[round((p[0] - center[0]) * lateral_scale, 6),
                                 round(-(p[1] - center[1]) * lateral_scale, 6),
                                 round((p[2] - bottom) * vertical_scale, 6)] for p in lod['positions']]
            lod['normals'] = [[round(value, 6) for value in unit(
                (n[0] / lateral_scale, -n[1] / lateral_scale, n[2] / vertical_scale))]
                for n in lod['normals']]
            lod['corners'] = [[v, round(u, 7), round(w, 7)] for v, u, w in lod['corners']]
        result['trees'][species] = {'source_meshes': names, 'height_cm': height,
                                   'radius_limit_cm': radius, 'lods': lods}
    output = SOURCE / 'trees.json'
    output.write_text(json.dumps(result, separators=(',', ':')) + '\n')
    manifest['generated_files'] = {'trees.json': digest(output)}
    (SOURCE / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    audit()


def audit():
    verify_files('source_files')
    verify_files('generated_files')
    data = json.loads((SOURCE / 'trees.json').read_text())
    report = {}
    for species, names, height, radius, budgets in TREES:
        tree = data['trees'][species]
        counts = []
        for index, lod in enumerate(tree['lods']):
            positions, normals, corners, triangles = (lod[key] for key in ('positions','normals','corners','triangles'))
            assert len(positions) == len(normals)
            assert all(math.isfinite(value) for row in positions + normals + corners for value in row)
            assert all(abs(sum(v * v for v in normal) - 1) < 1.e-5 for normal in normals)
            assert all(0 <= corner[0] < len(positions) for corner in corners)
            assert all(0 <= triangle[0] < len(TEXTURES) and all(0 <= corner < len(corners) for corner in triangle[1:])
                       for triangle in triangles)
            assert len(triangles) == budgets[index], (species, index, len(triangles))
            correct_normals = 0
            for material, *indices in triangles:
                vertices = [corners[corner][0] for corner in indices]
                a, b, c = [positions[vertex] for vertex in vertices]
                u, v = [c[i] - a[i] for i in range(3)], [b[i] - a[i] for i in range(3)]
                cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
                assert sum(value * value for value in cross) > 1.e-16, 'Degenerate tree triangle'
                normal = [sum(normals[vertex][i] for vertex in vertices) for i in range(3)]
                correct_normals += sum(cross[i] * normal[i] for i in range(3)) >= -1.e-8
            # A few cached normals in the author's simplified branches are
            # stale; Unreal recomputes them. LOD0 verifies the handedness fully.
            assert correct_normals >= len(triangles) * (1.0 if index == 0 else .95), 'Unexpected tree front-face winding'
            # Authored simplification can shift a stump by <1cm; shared transform
            # keeps all three LODs aligned instead of resizing every level.
            assert min(p[2] for p in positions) >= -1.0
            assert max(p[2] for p in positions) <= height + 1.0
            assert max(math.hypot(p[0], p[1]) for p in positions) <= radius + 1.0
            counts.append(len(triangles))
        report[species] = {'lod_triangles': counts, 'height_cm': height, 'radius_limit_cm': radius}
    report['license'] = 'CC0-1.0'
    report['source_sha256'] = digest(SOURCE / 'free_vegetation_pack.blend')
    report['photographic_textures'] = len(TEXTURES)
    report['maximum_texture_resolution'] = 1024
    report['branched_trees'] = audit_branched_trees()
    return report


def build_tree_assets():
    import unreal
    report = audit()
    data = json.loads((SOURCE / 'trees.json').read_text())
    helpers = runpy.run_path(str(Path(__file__).with_name('bootstrap_assets.py')),
                             run_name='vroom_tree_helpers')
    require, expression, output, scalar, multiply, connect = (
        helpers[key] for key in ('require','expression','output','scalar','multiply','connect'))
    instance_fade = runpy.run_path(str(Path(__file__).with_name('bootstrap_environment_assets.py')),
                                  run_name='vroom_environment_fade')['apply_instance_fade']
    library = unreal.EditorAssetLibrary
    editor = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    subsystem = (unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
                 or unreal.get_default_object(unreal.StaticMeshEditorSubsystem))
    require(subsystem, 'Missing static mesh editor subsystem')
    def save(asset, owner=GENERATOR):
        library.set_metadata_tag(asset, TAG, owner)
        require(library.save_loaded_asset(asset, only_if_is_dirty=False), 'Cannot save ' + asset.get_name())
    def owned_material(name):
        path = '/Game/Materials/' + name
        if library.does_asset_exist(path):
            material = library.load_asset(path)
            require(isinstance(material, unreal.Material) and
                    library.get_metadata_tag(material, TAG) == GENERATOR, 'Foreign tree material ' + path)
        else:
            material = require(tools.create_asset(name, '/Game/Materials', unreal.Material,
                                                 unreal.MaterialFactoryNew()), 'Cannot create ' + name)
        return material
    materials = []
    names = ('Bark', 'Leaves', 'SmallLeaves')
    library.make_directory('/Game/Textures/TreesRealistic')
    for index, (filename, name) in enumerate(zip(TEXTURES, names)):
        texture_name = 'T_RealisticTree' + name
        texture_path = '/Game/Textures/TreesRealistic/' + texture_name
        if library.does_asset_exist(texture_path):
            previous = library.load_asset(texture_path)
            require(library.get_metadata_tag(previous, TAG) == GENERATOR, 'Foreign tree texture ' + texture_path)
        task = unreal.AssetImportTask()
        task.filename = str(SOURCE / 'textures' / filename)
        task.destination_path = '/Game/Textures/TreesRealistic'
        task.destination_name = texture_name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tools.import_asset_tasks([task])
        texture = require(library.load_asset(texture_path), 'Cannot import ' + filename)
        texture.set_editor_property('srgb', True)
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
        texture.set_editor_property('max_texture_size', 1024)
        texture.set_editor_property('never_stream', True)
        # Preserve the source cutout through mip generation. The distance-fade
        # helper remaps this .45 cutoff to the final .5 dither mask afterwards.
        if index:
            texture.set_editor_property('do_scale_mips_for_alpha_coverage', True)
            texture.set_editor_property('alpha_coverage_thresholds', unreal.Vector4(0, 0, 0, .45))
        save(texture)
        material = owned_material('M_RealisticTree' + name)
        editor.delete_all_material_expressions(material)
        material.set_editor_property('used_with_instanced_static_meshes', True)
        material.set_editor_property('two_sided', index > 0)
        material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED if index else unreal.BlendMode.BLEND_OPAQUE)
        material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE
                                     if index else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        sample = expression(material, unreal.MaterialExpressionTextureSample, texture=texture,
                            sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        # Photographic RGB is kept intact. Existing per-instance regional tint
        # must not replace this texture with a uniform painted leaf colour.
        require(editor.connect_material_property(sample, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR), 'Cannot connect tree RGB')
        if index:
            output(multiply(material, sample, .15), unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
        # TextureSample's default output is RGB: select its actual A output,
        # never attempt to component-mask alpha from the three RGB channels.
        instance_fade(material, helpers, sample if index else None, 'A' if index else '')
        output(scalar(material, .90), unreal.MaterialProperty.MP_ROUGHNESS)
        output(scalar(material, .15), unreal.MaterialProperty.MP_SPECULAR)
        editor.recompile_material(material)
        save(material)
        materials.append(material)
    needle_material = owned_material('M_EnvironmentEvergreenNeedles')
    editor.delete_all_material_expressions(needle_material)
    needle_material.set_editor_property('used_with_instanced_static_meshes', True)
    needle_material.set_editor_property('two_sided', True)
    needle_material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    needle_material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    inputs = []
    for name in ('UV', 'Variation'):
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    needles = expression(needle_material, unreal.MaterialExpressionCustom,
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4, inputs=inputs, code='''
// One feathered branchlet on each curved card. Pixel footprint, never time,
// softens fine needles at distance; no noise texture, loops or CPU animation.
float t = saturate(1.0 - UV.y);
float x = (UV.x - 0.5) * 2.0;
float width = 0.13 + 0.85 * pow(saturate(sin(t * 3.14159265)), 0.7);
float tip = saturate((0.99 - t) / max(fwidth(t), 0.015));
float envelope = saturate((width - abs(x)) / max(fwidth(x), 0.018) + 0.5);
float row = t * 21.0 - abs(x) * 0.82 + (x > 0 ? 0.22 : 0.0);
float gap = abs(frac(row) - 0.5);
float pixel = max(fwidth(row), 0.035);
float needle = saturate((0.19 - gap) / pixel + 0.5);
float stem = saturate((0.026 * (1.0 - t * 0.55) - abs(x)) / max(fwidth(x), 0.01) + 0.5);
float alpha = max(stem, needle * envelope) * tip;
float edge = saturate(abs(x) / max(width, 0.1));
float3 dark = float3(0.030, 0.069, 0.033);
float3 light = float3(0.105, 0.162, 0.063);
float3 tint = lerp(dark, light, edge * 0.65 + t * 0.22);
tint *= lerp(0.84, 1.18, Variation);
return float4(tint, alpha);
''')
    connect(expression(needle_material, unreal.MaterialExpressionTextureCoordinate), needles, 'UV')
    connect(expression(needle_material, unreal.MaterialExpressionPerInstanceRandom), needles, 'Variation')
    rgb = expression(needle_material, unreal.MaterialExpressionComponentMask, r=True, g=True, b=True, a=False)
    alpha = expression(needle_material, unreal.MaterialExpressionComponentMask, r=False, g=False, b=False, a=True)
    connect(needles, rgb); connect(needles, alpha)
    output(rgb, unreal.MaterialProperty.MP_BASE_COLOR)
    instance_fade(needle_material, helpers, alpha, opacity_clip=.35)
    output(multiply(needle_material, rgb, .28), unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    output(scalar(needle_material, .86), unreal.MaterialProperty.MP_ROUGHNESS)
    output(scalar(needle_material, .12), unreal.MaterialProperty.MP_SPECULAR)
    editor.recompile_material(needle_material)
    save(needle_material)

    def build_mesh(species, lods, selected_materials, budgets, screens):
        path = '/Game/Meshes/SM_Environment' + species
        mesh = require(library.load_asset(path), 'Missing base tree ' + path)
        require(isinstance(mesh, unreal.StaticMesh) and library.get_metadata_tag(mesh, TAG) == ENVIRONMENT_GENERATOR,
                'Foreign environment tree ' + path)
        mesh.set_editor_property('static_materials', [unreal.StaticMaterial(
            material_interface=material, material_slot_name=material.get_name()) for material in selected_materials])
        descriptions = []
        for lod in lods:
            description = unreal.StaticMesh.create_static_mesh_description(mesh)
            groups = [description.create_polygon_group() for _ in selected_materials]
            for group, material in zip(groups, selected_materials):
                description.set_polygon_group_material_slot_name(group, material.get_name())
            vertices = []
            for point in lod['positions']:
                vertex = description.create_vertex()
                description.set_vertex_position(vertex, unreal.Vector(*point))
                vertices.append(vertex)
            for material_index, *corner_indices in lod['triangles']:
                instances = []
                for corner in corner_indices:
                    vertex, u, v = lod['corners'][corner]
                    instance = description.create_vertex_instance(vertices[vertex])
                    description.set_vertex_instance_uv(instance, unreal.Vector2D(u, v), 0)
                    instances.append(instance)
                description.create_triangle(groups[material_index], instances)
            descriptions.append(description)
        mesh.build_from_static_mesh_descriptions(descriptions, False, False)
        for index in range(3):
            # New source models inherit automatic 50% reductions. These LODs
            # are authored meshes, so preserve each level's own geometry.
            reduction = subsystem.get_lod_reduction_settings(mesh, index)
            reduction.base_lod_model = index
            reduction.percent_triangles = 1.0
            reduction.percent_vertices = 1.0
            reduction.max_deviation = 0.0
            subsystem.set_lod_reduction_settings(mesh, index, reduction)
            settings = subsystem.get_lod_build_settings(mesh, index)
            settings.recompute_normals = True
            settings.recompute_tangents = True
            settings.generate_lightmap_u_vs = False
            subsystem.set_lod_build_settings(mesh, index, settings)
        require(subsystem.set_lod_screen_sizes(mesh, screens), 'Cannot set tree LOD screens')
        # Preserve the base generator tag; its next run can rebuild these owned
        # meshes, followed by this deterministic photographic replacement.
        save(mesh, ENVIRONMENT_GENERATOR)
        counts = [mesh.get_num_triangles(index) for index in range(3)]
        require(counts == list(budgets), 'Authored tree LOD counts changed: ' + str(counts))
        unreal.log('VROOM TREES: ' + species + ' authored LOD triangles=' + str(counts))
    for species, _, _, _, budgets in TREES:
        build_mesh(species, data['trees'][species]['lods'], materials, budgets, [1.0, .06, .022])
    for species, _, _ in BRANCHED_TREES:
        lods = branched_tree_lods(species)
        build_mesh(species, lods, [materials[0], needle_material],
                   report['branched_trees'][species]['lod_triangles'], [1.0, .08, .028])
    unreal.log('VROOM TREES: PASS two preserved photographic broadleaves, branched evergreen cutouts, 3 LODs per species, unchanged collision trunks')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', action='store_true')
    parser.add_argument('--audit', action='store_true')
    args = parser.parse_args()
    if args.export:
        export_source()
    if args.audit:
        print(json.dumps(audit(), indent=2))
    if not args.export and not args.audit:
        build_tree_assets()


if __name__ == '__main__':
    main()
