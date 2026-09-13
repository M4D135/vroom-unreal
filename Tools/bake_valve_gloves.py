#!/usr/bin/env python3
"""Bake a grip from Valve's left OpenXR glove (Python 3 + NumPy).

The GLB is a local source reference, not an Unreal dependency. From the repo:
  python3 Tools/bake_valve_gloves.py
  python3 Tools/bake_valve_gloves.py --source /path/to/left.glb --output /path/gloves.json

Exact Valve source URL used (the main branch may change; verify the checksum):
  https://raw.githubusercontent.com/ValveSoftware/openxr_engine_plugins/main/assets/valve_hand_models/openxr_glove_left_model_slim.glb
SHA256:
  135189e6a2caab7d5fa0182feda4c2c7b1a9485bc5a7b27b0fe7b5b5ed7e33eb
Download and verify from the repository root before baking:
  mkdir -p ../reference/valve-hands
  curl --fail --location --output ../reference/valve-hands/openxr_glove_left_model_slim.glb https://raw.githubusercontent.com/ValveSoftware/openxr_engine_plugins/main/assets/valve_hand_models/openxr_glove_left_model_slim.glb
  echo '135189e6a2caab7d5fa0182feda4c2c7b1a9485bc5a7b27b0fe7b5b5ed7e33eb  ../reference/valve-hands/openxr_glove_left_model_slim.glb' | shasum -a 256 --check

Rest-pose skinning must reproduce the input mesh before any pose is exported.
The output keeps source UVs and triangle order; the final reflection makes its
winding clockwise relative to the normals, as expected by Unreal procedural
meshes. A subsequent reflection for the other hand must reverse triangle order.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT.parent / "reference/valve-hands/openxr_glove_left_model_slim.glb"
COMPONENTS = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
CANONICAL = np.array([[0., -100., 0.], [0., 0., 100.], [100., 0., 0.]])
WRIST_CM = np.array([-5.4, 27., -5.4])


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_glb(path):
    payload = path.read_bytes()
    require(len(payload) >= 20, "Truncated GLB")
    magic, version, length = struct.unpack_from("<III", payload)
    require(magic == 0x46546C67 and version == 2 and length == len(payload), "Expected a complete GLB 2 file")
    document, binary = None, None
    offset = 12
    while offset < length:
        require(offset + 8 <= length, "Truncated GLB chunk header")
        size, kind = struct.unpack_from("<II", payload, offset)
        offset += 8
        require(offset + size <= length, "Truncated GLB chunk")
        chunk = payload[offset:offset + size]
        if kind == 0x4E4F534A:
            require(document is None, "Duplicate JSON chunk")
            document = json.loads(chunk)
        elif kind == 0x004E4942:
            require(binary is None, "Duplicate BIN chunk")
            binary = chunk
        offset += size
    require(document is not None and binary is not None, "Missing JSON or BIN chunk")
    return document, binary, hashlib.sha256(payload).hexdigest()


def accessor(document, binary, index):
    entry = document["accessors"][index]
    require("sparse" not in entry, "Sparse accessors are unsupported")
    view = document["bufferViews"][entry["bufferView"]]
    require(view.get("buffer", 0) == 0, "Expected the GLB binary buffer")
    dtype = np.dtype(COMPONENTS[entry["componentType"]])
    width, count = WIDTHS[entry["type"]], entry["count"]
    row_size = width * dtype.itemsize
    stride = view.get("byteStride", row_size)
    start = view.get("byteOffset", 0) + entry.get("byteOffset", 0)
    end = start + (count - 1) * stride + row_size
    require(count > 0 and stride >= row_size and end <= len(binary), "Invalid accessor span")
    require(end <= view.get("byteOffset", 0) + view["byteLength"], "Accessor exceeds its buffer view")
    result = np.ndarray((count, width), dtype=dtype, buffer=binary, offset=start,
                        strides=(stride, dtype.itemsize)).copy()
    if entry.get("normalized", False):
        require(dtype.kind in "iu", "Only integer accessors may be normalized")
        result = result.astype(np.float64) / np.iinfo(dtype).max
        if dtype.kind == "i":
            result = np.maximum(result, -1.)
    if entry["type"] == "MAT4":
        # glTF matrices are serialized by columns.
        return result.reshape((-1, 4, 4)).transpose(0, 2, 1).astype(np.float64)
    return result[:, 0] if width == 1 else result


def quaternion_matrix(quaternion):
    q = np.asarray(quaternion, dtype=np.float64)
    require(np.linalg.norm(q) > 0, "Zero quaternion")
    x, y, z, w = q / np.linalg.norm(q)
    return np.array([
        [1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w)],
        [2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w)],
        [2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y)],
    ])


def node_matrix(node, curl_degrees=0.):
    if "matrix" in node:
        require(curl_degrees == 0, "Cannot add a TRS rotation to a matrix-only joint")
        return np.asarray(node["matrix"], dtype=np.float64).reshape(4, 4).T
    rotation = quaternion_matrix(node.get("rotation", [0, 0, 0, 1]))
    angle = math.radians(-curl_degrees)
    c, s = math.cos(angle), math.sin(angle)
    # Add flexion in the joint's local X axis, after its rest orientation.
    rotation = rotation @ np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    result = np.eye(4)
    result[:3, :3] = rotation @ np.diag(node.get("scale", [1, 1, 1]))
    result[:3, 3] = node.get("translation", [0, 0, 0])
    return result


def global_matrices(document, curls):
    nodes = document["nodes"]
    parents = {}
    for parent, node in enumerate(nodes):
        for child in node.get("children", []):
            require(child not in parents, "Node has multiple parents")
            parents[child] = parent
    result, visiting = {}, set()

    def visit(index):
        if index in result:
            return result[index]
        require(index not in visiting, "Cycle in the node hierarchy")
        visiting.add(index)
        node = nodes[index]
        local = node_matrix(node, curls.get(node.get("name"), 0.))
        result[index] = visit(parents[index]) @ local if index in parents else local
        visiting.remove(index)
        return result[index]

    return np.stack([visit(index) for index in range(len(nodes))])


def normalize(vectors):
    lengths = np.linalg.norm(vectors, axis=1, keepdims=True)
    require(np.all(lengths > 1e-12), "Degenerate normal")
    return vectors / lengths


def skin_mesh(positions, normals, joints, weights, joint_matrices):
    matrices = np.sum(joint_matrices[joints] * weights[:, :, None, None], axis=1)
    homogeneous = np.column_stack([positions, np.ones(len(positions))])
    points = np.einsum("nij,nj->ni", matrices, homogeneous)[:, :3]
    # Normal matrix of the blended affine skinning transform. This retains the
    # source smooth normals while correctly handling its small authored scales.
    normal_matrices = np.linalg.inv(matrices[:, :3, :3]).transpose(0, 2, 1)
    directions = normalize(np.einsum("nij,nj->ni", normal_matrices, normals))
    return points, directions


def bake(source, curl=None, thumb_curl=(0., 25.), finger_proximal=None):
    # Different finger lengths need different MCP flexion to clear a 2.4 cm
    # steering rim. A uniform 45-degree flexion closed the little/ring fingers
    # into the palm and put the rim through their proximal phalanges.
    if curl is None:
        curl = (20., 65., 40.)
        if finger_proximal is None:
            finger_proximal = (20., 10., 5., 0.)
    document, binary, source_sha = read_glb(source)
    mesh_nodes = [(i, n) for i, n in enumerate(document["nodes"]) if "mesh" in n and "skin" in n]
    require(len(mesh_nodes) == 1, "Expected exactly one skinned mesh node")
    mesh_index, mesh_node = mesh_nodes[0]
    mesh = document["meshes"][mesh_node["mesh"]]
    require(len(mesh["primitives"]) == 1, "Expected one primitive")
    primitive = mesh["primitives"][0]
    require(primitive.get("mode", 4) == 4 and "targets" not in primitive, "Expected triangles without morph targets")
    attrs = primitive["attributes"]
    require("JOINTS_1" not in attrs, "Additional skin influences are unsupported")
    positions = accessor(document, binary, attrs["POSITION"]).astype(np.float64)
    normals = accessor(document, binary, attrs["NORMAL"]).astype(np.float64)
    uvs = accessor(document, binary, attrs["TEXCOORD_0"]).astype(np.float64)
    joints = accessor(document, binary, attrs["JOINTS_0"]).astype(np.int64)
    weights = accessor(document, binary, attrs["WEIGHTS_0"]).astype(np.float64)
    indices = accessor(document, binary, primitive["indices"]).astype(np.int64)
    skin = document["skins"][mesh_node["skin"]]
    inverse_bind = accessor(document, binary, skin["inverseBindMatrices"])
    require(len(positions) == len(normals) == len(uvs) == len(joints) == len(weights), "Attribute count mismatch")
    require(len(indices) % 3 == 0 and indices.min() >= 0 and indices.max() < len(positions), "Invalid triangle indices")
    require(joints.min() >= 0 and joints.max() < len(skin["joints"]), "Invalid joint index")
    require(np.all(weights >= 0) and np.max(np.abs(weights.sum(axis=1) - 1)) < 1e-4, "Invalid skin weights")
    weights /= weights.sum(axis=1, keepdims=True)
    require(all(np.isfinite(a).all() for a in (positions, normals, uvs, weights, inverse_bind)), "Nonfinite source data")

    rest_globals = global_matrices(document, {})
    rest_points, rest_normals = skin_mesh(positions, normals, joints, weights,
                                         rest_globals[skin["joints"]] @ inverse_bind)
    mesh_global = rest_globals[mesh_index]
    expected_points = np.einsum("ij,nj->ni", mesh_global[:3, :3], positions) + mesh_global[:3, 3]
    expected_normals = normalize(np.einsum("ni,ij->nj", normals, np.linalg.inv(mesh_global[:3, :3])))
    rest_position_error = float(np.max(np.linalg.norm(rest_points - expected_points, axis=1)))
    rest_normal_error = float(np.max(np.linalg.norm(rest_normals - expected_normals, axis=1)))
    require(rest_position_error < 1e-5, f"Rest skin differs from input mesh by {rest_position_error:.9g} m")
    require(rest_normal_error < 1e-4, f"Rest normal mismatch: {rest_normal_error:.9g}")

    # Keep the intermediate/distal joints curled around the rim while opening
    # each proximal joint enough to leave an actual cavity between palm/fingers.
    # The distal thumb flexion closes its silhouette without crossing the index.
    curls = {f"{finger}_{part}_L": float(degrees)
             for finger in ("Index", "Middle", "Ring", "Little")
             for part, degrees in zip(("Proximal", "Intermediate", "Distal"), curl)}
    if finger_proximal is not None:
        require(len(finger_proximal) == 4, "Expected four proximal finger angles")
        for finger, degrees in zip(("Index", "Middle", "Ring", "Little"), finger_proximal):
            curls[f"{finger}_Proximal_L"] = float(degrees)
    for part, degrees in zip(("Proximal", "Distal"), thumb_curl):
        if degrees:
            curls[f"Thumb_{part}_L"] = float(degrees)
    names = {n.get("name"): i for i, n in enumerate(document["nodes"])}
    require("Wrist_L" in names and all(n in names for n in curls), "Missing named hand joints")
    posed_globals = global_matrices(document, curls)
    world_points, world_normals = skin_mesh(positions, normals, joints, weights,
                                           posed_globals[skin["joints"]] @ inverse_bind)
    world_to_wrist = np.linalg.inv(posed_globals[names["Wrist_L"]])
    hand_points = np.einsum("ij,nj->ni", world_to_wrist[:3, :3], world_points) + world_to_wrist[:3, 3]
    hand_normals = normalize(np.einsum("ni,ij->nj", world_normals, np.linalg.inv(world_to_wrist[:3, :3])))
    vertices = np.einsum("ij,nj->ni", CANONICAL, hand_points) + WRIST_CM
    output_normals = normalize(np.einsum("ni,ij->nj", hand_normals, np.linalg.inv(CANONICAL)))
    require(np.isfinite(vertices).all() and np.isfinite(output_normals).all(), "Nonfinite baked mesh")
    triangles = vertices[indices.reshape(-1, 3)]
    crosses = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    areas = np.linalg.norm(crosses, axis=1) * .5
    require(np.count_nonzero(areas > 1e-8) > .99 * len(areas), "Degenerate posed mesh")
    output = {
        "schema_version": 1,
        "units": "cm",
        "vertices": np.round(vertices, 7).tolist(),
        "normals": np.round(output_normals, 9).tolist(),
        "uvs": uvs.tolist(),
        "indices": indices.tolist(),
        "wrist": WRIST_CM.tolist(),
        "source": {"file": source.name, "sha256": source_sha, "author": "Valve Corporation",
                   "mesh": mesh_node["name"], "joint_count": len(skin["joints"])},
        "pose": {"additional_local_x_degrees": {k: -v for k, v in curls.items()},
                 "thumb": "Authored rest opposition retained; optional extra flexion via --thumb-curl.",
                 "coordinates": "inverse(global Wrist_L), then X=-5.4-100*Y, Y=27+100*Z, Z=-5.4+100*X",
                 "winding": "Source glTF indices retained; clockwise versus normals after canonical reflection. Reverse on an additional mirror.",
                 "uvs": "Source TEXCOORD_0 unchanged; no V flip."},
        "validation": {"rest_max_position_error_m": rest_position_error,
                       "rest_max_normal_error": rest_normal_error,
                       "vertex_count": len(vertices), "triangle_count": len(indices) // 3,
                       "bounds_min_cm": vertices.min(axis=0).tolist(),
                       "bounds_max_cm": vertices.max(axis=0).tolist(),
                       "zero_area_triangle_count": int(np.count_nonzero(areas <= 1e-8))},
    }
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=ROOT / "Content/Data/gloves.json")
    parser.add_argument("--curl", type=float, nargs=3, default=None, metavar=("PROXIMAL", "INTERMEDIATE", "DISTAL"), help="Override the fitted driving pose with uniform finger curls")
    parser.add_argument("--thumb-curl", type=float, nargs=2, default=(0., 25.), metavar=("PROXIMAL", "DISTAL"))
    parser.add_argument("--finger-proximal", type=float, nargs=4, default=None,
                        metavar=("INDEX", "MIDDLE", "RING", "LITTLE"),
                        help="Override the four proximal flexions independently")
    args = parser.parse_args()
    angles = (*(args.curl or ()), *args.thumb_curl, *(args.finger_proximal or ()))
    require(all(math.isfinite(a) and abs(a) <= 90 for a in angles), "Curl angles must be finite and within +/-90 degrees")
    output = bake(args.source, args.curl, args.thumb_curl, args.finger_proximal)
    if args.curl is None and args.finger_proximal is None and tuple(args.thumb_curl) == (0., 25.):
        from fit_glove_clearance import fit
        output = fit(output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), **output["validation"]}, indent=2))


if __name__ == "__main__":
    main()
