"""Pure-Python decoding and validation for the pinned CC0 environment assets."""
import json
import math
from pathlib import Path
import struct
import runpy

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Content/ArtSource/realistic'


def verify():
    return runpy.run_path(str(Path(__file__).with_name('fetch_environment_assets.py')))['verify_sources']()


def rock_geometry():
    folder = SOURCE / 'rock_moss_set_02'
    gltf = json.loads((folder / 'rock_moss_set_02_1k.gltf').read_text())
    buffers = [(folder / item['uri']).read_bytes() for item in gltf['buffers']]
    def accessor(index):
        a = gltf['accessors'][index]
        view = gltf['bufferViews'][a['bufferView']]
        components = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        code = {5123: 'H', 5125: 'I', 5126: 'f'}[a['componentType']]
        fmt = '<' + code * components
        stride = view.get('byteStride', struct.calcsize(fmt))
        offset = view.get('byteOffset', 0) + a.get('byteOffset', 0)
        data = buffers[view['buffer']]
        return [struct.unpack_from(fmt, data, offset + i * stride) for i in range(a['count'])]
    node = next(n for n in gltf['nodes'] if n.get('name') == 'rock_moss_set_02_rock08')
    primitive = gltf['meshes'][node['mesh']]['primitives'][0]
    assert primitive.get('mode', 4) == 4
    raw = accessor(primitive['attributes']['POSITION'])
    uv = accessor(primitive['attributes']['TEXCOORD_0'])
    normal = accessor(primitive['attributes']['NORMAL'])
    indices = [i[0] for i in accessor(primitive['indices'])]
    # glTF right-handed Y-up -> Unreal left-handed Z-up. Preserve UVs and
    # triangle order; the handedness reflection gives Unreal its front faces.
    raw = [(-p[2], p[0], p[1]) for p in raw]
    normal = [(-p[2], p[0], p[1]) for p in normal]
    low = [min(p[i] for p in raw) for i in range(3)]
    high = [max(p[i] for p in raw) for i in range(3)]
    # Keep the old rock's scene footprint/pivot, so existing placements and
    # the broad-phase scenery contacts do not acquire a different scale.
    target_low = (-108.06, -81.74, -52.0)
    target_high = (115.0, 81.74, 136.0)
    scale = [(target_high[i] - target_low[i]) / (high[i] - low[i]) for i in range(3)]
    positions = [tuple(target_low[i] + (p[i] - low[i]) * scale[i] for i in range(3)) for p in raw]
    assert len(indices) % 3 == 0 and len(indices) // 3 == 8000
    assert len(uv) == len(positions) and all(0 <= i < len(positions) for i in indices)
    assert all(math.isfinite(x) for p in positions + uv for x in p)
    correct = 0
    for a, b, c in zip(indices[::3], indices[1::3], indices[2::3]):
        u = [raw[c][i] - raw[a][i] for i in range(3)]
        v = [raw[b][i] - raw[a][i] for i in range(3)]
        cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
        correct += sum(cross[i] * normal[a][i] for i in range(3)) >= -1e-9
    assert correct >= 7960, 'Unexpected front-face orientation in the selected rock'
    return positions, uv, indices


def audit():
    manifest = verify()
    positions, uv, indices = rock_geometry()
    return {'sources': [s['id'] for s in manifest['sources']], 'source_triangles': len(indices)//3,
            'source_vertices': len(positions), 'lod_triangle_targets': [2000, 800, 240],
            'texture_resolution': 1024, 'texture_count': 9,
            'bytes': sum(f['bytes'] for s in manifest['sources'] for f in s['files'])}
