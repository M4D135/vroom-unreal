"""Fit glove vertices to the rim; this is not a triangle collision solver."""
import copy
import math


def fit(document, grip_depth=-2.8):
    result = copy.deepcopy(document)
    vertices = result['vertices']
    # Same fixed grip as DrivingHandPose. Rotating both around the wheel keeps
    # this clearance unchanged in every steering pose and for the mirrored hand.
    pose = result['driving_grip'] = {'neutral_angle_deg': 30., 'wrist_tilt_deg': 80., 'scale': 1.05,
        'grip_depth_cm': grip_depth, 'grip_reach_cm': 19.1}
    angle, tilt = math.radians(180-pose['neutral_angle_deg']), math.radians(pose['wrist_tilt_deg'])
    scale = pose['scale']
    ca, sa, ct, st = math.cos(angle-math.pi), math.sin(angle-math.pi), math.cos(tilt), math.sin(tilt)
    contact = (0, 18.8*math.cos(angle), 18.8*math.sin(angle))
    grip = (pose['grip_depth_cm'], pose['grip_reach_cm'], -5.4)
    changed, maximum = [], 0
    for index, original in enumerate(vertices):
        x, y, z = ((original[k]-grip[k])*scale for k in range(3))
        x, y = ct*x-st*y, st*x+ct*y
        y, z = ca*y-sa*z, sa*y+ca*z
        point = [x+contact[0], y+contact[1], z+contact[2]]
        radial = math.hypot(point[1], point[2])
        if radial < 1.e-6:
            continue
        center = [0, point[1]*18.8/radial, point[2]*18.8/radial]
        delta = [point[k]-center[k] for k in range(3)]
        distance = math.sqrt(sum(value*value for value in delta))
        if distance >= 1.215 or distance < 1.e-6:
            continue
        maximum = max(maximum, 1.215-distance)
        point = [center[k]+delta[k]*1.215/distance-contact[k] for k in range(3)]
        x, y, z = point
        y, z = ca*y+sa*z, -sa*y+ca*z
        x, y = ct*x+st*y, -st*x+ct*y
        vertices[index] = [round(value/scale+grip[k], 7) for k, value in enumerate((x,y,z))]
        changed.append(index)
    # Update only normals adjacent to the small local deformation. Preserve
    # all UV seams, source winding and the original normal elsewhere.
    affected = set(changed)
    moved = set(changed)
    indices = result['indices']
    for i in range(0, len(indices), 3):
        triangle = indices[i:i+3]
        if any(index in moved for index in triangle):
            affected.update(triangle)
    normals = {index: [0.,0.,0.] for index in affected}
    for i in range(0, len(indices), 3):
        triangle = indices[i:i+3]
        a,b,c = (vertices[index] for index in triangle)
        u,v = [b[k]-a[k] for k in range(3)],[c[k]-a[k] for k in range(3)]
        # Canonical reflection makes the source clockwise vs outward normals.
        normal = [v[1]*u[2]-v[2]*u[1],v[2]*u[0]-v[0]*u[2],v[0]*u[1]-v[1]*u[0]]
        for index in triangle:
            if index in normals:
                normals[index] = [normals[index][k]+normal[k] for k in range(3)]
    for index, normal in normals.items():
        length = math.sqrt(sum(n*n for n in normal))
        if length > 1.e-8:
            result['normals'][index] = [round(n/length,9) for n in normal]
    result['validation']['rim_clearance'] = {'adjusted_vertices': len(changed),
        'max_displacement_world_cm': maximum, 'rim_radius_cm':18.8, 'tube_radius_cm':1.2,
        'surface_clearance_cm':.015, 'clearance_scope':'vertices_only', 'pose_changed':False,
        'grip_depth_cm':grip_depth}
    if grip_depth != -2.5:
        # Quantify the small refit against the unchanged white 0.30 glove.
        # Grip rotation/scale/height, topology and UVs remain identical. The
        # inward grip translation is separate from this local mesh correction.
        reference = fit(document, grip_depth=-2.5)
        differences = [math.dist(a, b)*scale
            for a, b in zip(vertices, reference['vertices'])]
        result['validation']['grip_tightening'] = {
            'reference_grip_depth_cm':-2.5,
            'grip_translation_world_cm':abs(grip_depth+2.5)*scale,
            'locally_adjusted_vertices':sum(d > 1.e-6 for d in differences),
            'max_local_refit_vs_030_world_cm':max(differences, default=0.),
            'rotation_scale_height_changed':False,
            'topology_and_uvs_changed':False}
    return result
