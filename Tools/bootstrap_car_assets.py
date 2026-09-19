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
    # Rounded leading edge and continuous camber on a simple period two-plane
    # wing. Its chord/span and 0..6-cm envelope are unchanged; extra profile
    # samples remove the old five-sided wedge highlights without aero gimmicks.
    profile = ((x + chord * .5, z + 1), (x + chord * .43, z + 2.1),
               (x + chord * .29, z + 3.8), (x, z + 5.3),
               (x - chord * .34, z + 6), (x - chord * .47, z + 5.1),
               (x - chord * .5, z + 3), (x - chord * .47, z + 1.1),
               (x - chord * .28, z + .25), (x + chord * .10, z),
               (x + chord * .35, z + .35))
    rings = [[(px, y_center + side * span * .5, pz) for px, pz in profile] for side in (-1, 1)]
    for i in range(len(profile)):
        j = (i + 1) % len(profile)
        mesh.quad(rings[0][i], rings[1][i], rings[1][j], rings[0][j], material)
    for i in range(1, len(profile) - 1):
        mesh.tri(rings[0][0], rings[0][i], rings[0][i + 1], material)
        mesh.tri(rings[1][0], rings[1][i + 1], rings[1][i], material)


def rounded_plate(mesh, x_min, x_max, z_min, z_max, y, half_width, material, corner=2):
    """Thin endplate with rounded outline, using the legacy helper winding."""
    radius = min(corner, (x_max - x_min) * .25, (z_max - z_min) * .25)
    outline = []
    for cx, cz, angle in ((x_max - radius, z_min + radius, -math.pi / 2),
                          (x_max - radius, z_max - radius, 0),
                          (x_min + radius, z_max - radius, math.pi / 2),
                          (x_min + radius, z_min + radius, math.pi)):
        for step in range(4):
            a = angle + step * math.pi / 6
            outline.append((cx + radius * math.cos(a), cz + radius * math.sin(a)))
    rings = [[(x, y + side * half_width, z) for x, z in outline] for side in (-1, 1)]
    for i in range(len(outline)):
        j = (i + 1) % len(outline)
        mesh.quad(rings[0][i], rings[1][i], rings[1][j], rings[0][j], material)
    for i in range(1, len(outline) - 1):
        mesh.tri(rings[0][0], rings[0][i], rings[0][i + 1], material)
        mesh.tri(rings[1][0], rings[1][i + 1], rings[1][i], material)


def outward_quad(mesh, face, material, direction):
    normal = cross(sub(face[2], face[0]), sub(face[1], face[0]))
    if sum(a * b for a, b in zip(normal, direction)) < 0:
        face = face[::-1]
    mesh.quad(*face, material)


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


def wheel(radius, width, segments=20, rounded_shoulders=False):
    mesh = Geometry()
    if rounded_shoulders:
        # Quarter-ellipses are tangent to the vertical sidewall and flat slick
        # tread. Two extra rings on each shoulder remove the old straight bevel
        # while preserving the exact radius, width and contact-band limits.
        left = [(-width * (.4 + .1 * math.cos(i * math.pi / 6)),
                 radius * (.84 + .16 * math.sin(i * math.pi / 6))) for i in range(4)]
        profile = [(-.5 * width, .46 * radius), *left,
                   *((-y, r) for y, r in reversed(left)), (.5 * width, .46 * radius)]
    else:
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
            # Raised five-spoke casting, with real thickness instead of a
            # paper cut-out. The broad low-contrast spokes read while rotating.
            back_face = [(x, py - side * 1.2, z) for x, py, z in face]
            outward_quad(mesh, face, ALLOY, (0, side, 0))
            outward_quad(mesh, back_face, ALLOY, (0, -side, 0))
            centre = tuple(sum(point[i] for point in face) / 4 for i in range(3))
            for i in range(4):
                j = (i + 1) % 4
                edge = (face[i], back_face[i], back_face[j], face[j])
                direction = sub(tuple((face[i][k] + face[j][k]) * .5 for k in range(3)), centre)
                outward_quad(mesh, edge, ALLOY, direction)
        # Two short embossed mould marks and a valve provide an asymmetric
        # rotation cue without brands, bright stripes or repetitive white text.
        for angle in (.38, 3.57):
            points = [(radius * r * math.cos(angle + a), side * width * .502,
                       radius * r * math.sin(angle + a))
                      for r, a in ((.71, -.022), (.80, -.022), (.80, .022), (.71, .022))]
            outward_quad(mesh, points, RUBBER, (0, side, 0))
        valve = Geometry()
        angle = .24
        x, z = radius * .425 * math.cos(angle), radius * .425 * math.sin(angle)
        rod(valve, (x, y - side * .4, z), (x, y + side * .25, z), .48, ALLOY, 6)
        mesh.faces.extend((material, face[::-1]) for material, face in valve.faces)
    # Keep the generic rod helper unchanged for the body assets. The wheel's
    # closed hub needs outward-facing walls and caps under Unreal's winding.
    hub = Geometry()
    rod(hub, (0, -width * .52, 0), (0, width * .52, 0), 5.5, ALLOY, 10)
    mesh.faces.extend((material, face[::-1]) for material, face in hub.faces)
    return mesh


def mesh_vertex_key(position, face, material, wheel_surface=False):
    """Share smooth surfaces, but keep rim faces separate from their opening.

    Weighted normals must not blend a flat metal rim into its perpendicular
    inner barrel or a rubber shoulder. This changes normals, not geometry.
    """
    group = ()
    if wheel_surface and material == ALLOY:
        if max(p[1] for p in face) - min(p[1] for p in face) < 1.e-6:
            group = ('rim-face', round(face[0][1], 6))
        else:
            group = ('rim-barrel',)
    return material, group, tuple(round(v, 6) for v in position)


def face_uvs(face):
    """Project each face onto its strongest plane without splitting normals.

    Resetting UVs to the same unit triangle for every face reverses tangents
    arbitrarily across shared vertices. Dominant-axis charts give neighbours
    consistent directions and never collapse vertical caps into a UV line.
    UVs belong to vertex instances; the existing vertex/normal groups stay
    untouched. Separate chart offsets also prevent unrelated projections from
    sharing the same tangent seam at an otherwise identical UV coordinate.
    """
    normal = cross(sub(face[2], face[0]), sub(face[1], face[0]))
    axis = max(range(3), key=lambda i: abs(normal[i]))
    u, v = ((1, 2), (2, 0), (0, 1))[axis]
    sign = 1 if normal[axis] >= 0 else -1
    chart = axis * 2 + (sign < 0)
    return tuple((point[u] * .01 * sign + chart * 16, point[v] * .01)
                 for point in face)


def rounded_loft(mesh, sections, material, y_offset=0, sides=16, subdivisions=3,
                 roof_exponent=.72, cap_start_material=None):
    """Smooth rounded body with outward Unreal winding and shared ring vertices.

    Catmull-Rom samples soften longitudinal silhouette changes without adding
    a subdivision asset. A separate inner bulkhead material keeps the open
    cockpit from looking like a solid red block below its dashboard.
    """
    rings = []
    for interval in range(len(sections) - 1):
        p0, p1 = sections[max(0, interval - 1)], sections[interval]
        p2, p3 = sections[interval + 1], sections[min(len(sections) - 1, interval + 2)]
        for step in range(subdivisions):
            t = step / subdivisions
            values = [0.5 * ((2 * p1[i]) + (-p0[i] + p2[i]) * t
                      + (2 * p0[i] - 5 * p1[i] + 4 * p2[i] - p3[i]) * t * t
                      + (-p0[i] + 3 * p1[i] - 3 * p2[i] + p3[i]) * t * t * t)
                      for i in range(4)]
            rings.append(values)
    rings.append(sections[-1])
    points = []
    def rounded(value, exponent):
        return math.copysign(abs(value) ** exponent, value)
    for x, width, low, high in rings:
        middle, radius = (high + low) * .5, (high - low) * .5
        points.append([(x, y_offset + width * rounded(math.cos(i * math.tau / sides), .62),
                        middle + radius * rounded(math.sin(i * math.tau / sides), roof_exponent))
                       for i in range(sides)])
    for first, second in zip(points, points[1:]):
        for i in range(sides):
            j = (i + 1) % sides
            mesh.quad(first[i], second[i], second[j], first[j], material)
    for i in range(1, sides - 1):
        mesh.tri(points[0][0], points[0][i], points[0][i + 1],
                 material if cap_start_material is None else cap_start_material)
        mesh.tri(points[-1][0], points[-1][i + 1], points[-1][i], material)


def make_player_body(close=True):
    """Open cockpit from (-65, 0, 82), with a long low red nose in front.

    No opponent helmet, fake arms, shoulder bulkhead or mirrors are baked into
    this first-person shell. The runtime supplies the coaming, hands and mirrors.
    Rear engine cover, seat and wing remain usable from a chase camera.
    """
    body = Geometry()
    legacy = Geometry()
    rounded_loft(body, ((50, 24, 31, 55), (80, 22, 31, 61), (115, 18, 29, 61.8),
                       (157, 13.5, 27, 58), (196, 9, 25, 54.3), (225, 5.8, 24, 52)), PAINT,
                 sides=24 if close else 20, subdivisions=4 if close else 3,
                 roof_exponent=.5, cap_start_material=CARBON)
    def nose_tip(point):
        x, y, z = point
        if x <= 196:
            return point
        t = min(1, (x - 196) / 29)
        blend = t * t * (3 - 2 * t)
        # Only the terminal 29 cm changes: the previous 28-cm-high end cap
        # looked like a rectangular shoe. The tapered end is now z30..38,
        # while every cockpit-visible section up to x196 stays untouched.
        return x, y * (1 - .18 * blend), z + blend * (6 - 20 * (z - 24) / 28)
    body.faces = [(material, tuple(nose_tip(point) for point in face)) for material, face in body.faces]
    for side in (-1, 1):
        # The opening stays continuous between the seat and the steering wheel.
        # Thin side rails meet the runtime cockpit rim, leaving the centre clear.
        rounded_loft(body, ((-105, 6, 28, 68), (-65, 6, 27, 66), (-20, 5.5, 26, 62),
                           (25, 5, 28, 59), (52, 6, 31, 58)), PAINT, side * 29,
                     sides=16 if close else 12, subdivisions=3 if close else 2)
        # Radiator pods pinch in ahead of the rear axle. Their former thick
        # rounded tail caps read as two red exhaust tubes from the chase view.
        rounded_loft(body, ((-138, 3.5, 23, 29), (-110, 17, 22, 43), (-55, 24, 22, 60),
                           (0, 22, 23, 57), (48, 12, 26, 48)), PAINT, side * 54,
                     sides=20 if close else 16, subdivisions=3 if close else 2)
        # Suspension is authored only in CarWishbones and follows wheel steer
        # at runtime. Baking a second set here produced floating duplicate rods.
        # Recessed radiator opening on the outboard side, outside the cockpit.
        face = ((-18, side * 76.2, 34), (20, side * 70.2, 33),
                (20, side * 70.2, 48), (-18, side * 76.2, 51))
        body.quad(*(face if side < 0 else face[::-1]), CARBON)
        # Dark forward radiator mouth with a small central divider. This sits
        # on the pod's front face, leaving the cockpit and suspension clear.
        intake = ((48.2, side * 46, 31), (48.2, side * 62, 31),
                  (48.2, side * 62, 43), (48.2, side * 46, 43))
        outward_quad(body, intake, CARBON, (1, 0, 0))
        rod(legacy, (48.5, side * 54, 31), (48.5, side * 54, 43), .6, PAINT, 6)
        # Small exposed fasteners replace the old conspicuous white nose stripe.
        for x, y, z in ((84, 16.6, 59.1), (153, 9.9, 56.6)):
            ellipsoid(legacy, (x, side * y, z), (.55, .55, .16), PEARL, 8, 3)
        rounded_plate(legacy, -193, -144, 73, 109, side * 96, 2, PAINT, corner=3)
        rod(legacy, (-163, side * 15, 44), (-169, side * 15, 99), 3, CARBON, 8)
        # Two small dark exhausts with thin metal-coloured lips, well behind
        # the driver's view. Open mouths avoid a solid silver cylinder end.
        rod(legacy, (-137, side * 26, 43), (-167, side * 24, 38), 2.4, CARBON, 10)
        ellipsoid(legacy, (-167.1, side * 24, 38), (.2, 2.65, 2.65), PEARL, 10, 4)
        ellipsoid(legacy, (-167.35, side * 24, 38), (.2, 2.1, 2.1), CARBON, 10, 4)
    rounded_loft(body, ((-166, 3.5, 44, 49), (-144, 11, 37, 65), (-118, 23, 34, 105),
                       (-100, 16, 53, 117), (-84, 5, 71, 106)), PAINT,
                 sides=20 if close else 16, subdivisions=3 if close else 2)
    # The exposed transmission and rear diffuser break up the three red
    # lobes, grounding the engine cover in a credible mechanical rear end.
    rounded_loft(body, ((-174, 9, 26, 39), (-155, 14, 24, 48), (-113, 18, 25, 50)),
                 CARBON, sides=12, subdivisions=2)
    loft(legacy, ((-169, 54, 18, 28), (-145, 62, 17, 22), (-123, 62, 17, 20)), CARBON)
    for y in (-46, -24, 0, 24, 46):
        loft(legacy, ((-169, .65, 18, 30), (-130, .65, 17, 21)), CARBON, y)
    # Small rear rain-light housing and red lens, with no extra material slot.
    loft(legacy, ((-177, 4, 29, 37), (-171, 4, 29, 37)), CARBON)
    ellipsoid(legacy, (-177.2, 0, 33), (.3, 3.2, 2.8), PAINT, 10, 4)
    # The airbox opening faces forwards above the shoulders, behind the eye.
    ellipsoid(legacy, (-83.8, 0, 96), (.3, 3.5, 7), CARBON, 12, 5)
    loft(legacy, ((-169, 56, 17, 20), (-127, 77, 17, 20),
                  (46, 74, 17, 20), (93, 24, 18, 21)), CARBON)
    loft(legacy, ((-108, 23, 24, 28), (48, 23, 24, 28)), CARBON)
    loft(legacy, ((-101, 22, 30, 76), (-94, 22, 30, 76)), CARBON)
    wing(legacy, -167, 96, 190, 48, CARBON)
    wing(legacy, -184, 105, 189, 19, PAINT)
    # Legacy helpers wind mathematical normals outward; MeshDescription uses
    # cross(C-A, B-A). Reverse only these new player surfaces, not shared helpers.
    body.faces.extend((material, face[::-1]) for material, face in legacy.faces)
    return body


def make_player_driver(close=True):
    """Period helmet and suit; only rivals need their arms baked into the mesh.

    The player's real cockpit arms and gloves are also shown in chase view, so
    adding another set here would create two different poses. Keeping the head
    separate avoids putting it in front of the first-person cockpit camera.
    """
    legacy = Geometry()
    # Reclined torso sits below the cockpit lip. Its front overlaps the neck
    # roll, which in turn overlaps the lowered helmet: no floating head/gap.
    ellipsoid(legacy, (-24, 0, 55), (17, 16, 16), PEARL, 16 if close else 12, 7 if close else 5)
    ellipsoid(legacy, (-17, 0, 72), (9, 9, 4), CARBON, 16, 4)
    for side in (-1, 1):
        if close:
            # Shoulder starts descend inside the cockpit and stop before the
            # animated forearms; there is still no baked wrist or glove.
            rod(legacy, (-27, side * 14, 64), (-17, side * 18, 47), 4.5, PEARL, 8)
        else:
            # Rivals have their own static wheel at x26/z66. The player's
            # animated arms are supplied by VroomCockpit in both camera modes.
            rod(legacy, (-31, side * 17, 66), (-7, side * 21, 60), 5.8, PEARL, 6)
            ellipsoid(legacy, (-7, side * 21, 60), (5.8, 5.8, 5.8), PEARL, 8, 4)
            rod(legacy, (-7, side * 21, 60), (23, side * 16, 66), 4.8, PEARL, 6)
            ellipsoid(legacy, (24, side * 16, 66), (5.5, 4.5, 4.5), PEARL, 8, 4)
        # Shoulder straps remain dark and broad enough to read at chase scale.
        rod(legacy, (-34, side * 8, 65), (-18, side * 7, 67), 1.5, CARBON, 6)
    ellipsoid(legacy, (-16, 0, 89), (16, 14, 18), PAINT, 24 if close else 16, 12 if close else 8)
    ellipsoid(legacy, (-16, 0, 89), (16.25, 14.25, 18.25), CARBON, 18 if close else 14, 4 if close else 3,
              (.36 * math.pi, .57 * math.pi), (-1.18, 1.18))
    ellipsoid(legacy, (-16, 0, 89), (16.4, 14.4, 18.4), PEARL, 18 if close else 14, 2,
              (.63 * math.pi, .69 * math.pi), (-1.6, 1.6))
    driver = Geometry()
    driver.faces = [(material, face[::-1]) for material, face in legacy.faces]
    return driver


def make_geometry():
    player_body = make_player_body()
    player_driver = make_player_driver()
    # Rivals now use the same finished monocoque, radiators, transmission and
    # seated driver as the player. Only their fixed cockpit/suspension extras
    # are baked; all four wheels are separate origin-centred runtime parts.
    body = make_player_body(close=False); body.merge(make_player_driver(close=False))
    details = Geometry()
    for side in (-1, 1):
        for a, b in (((65, 19, 49), (128, 74, 43)), ((107, 14, 50), (128, 74, 43)),
                     ((59, 18, 35), (128, 74, 28)), ((103, 14, 35), (128, 74, 28)),
                     ((87, 18, 42), (126, 74, 35)), ((81, 15, 55), (128, 74, 28)),
                     ((-70, 40, 49), (-115, 79, 46)), ((-142, 34, 45), (-115, 79, 46)),
                     ((-76, 42, 29), (-115, 79, 28)), ((-140, 35, 30), (-115, 79, 28))):
            rod(details, (a[0], side * a[1], a[2]), (b[0], side * b[1], b[2]), 1.4, CARBON, 6)
        # Fixed uprights/axles sit inside the open rim barrels. Wishbones end
        # on these uprights rather than apparently disconnecting as the front
        # wheel steers; the wheel's spin never rotates the suspension mesh.
        for x, inner_y, pivot_y, z, low, high in ((128, 74, 91, 35, 28, 43),
                                                (-115, 79, 96, 38, 28, 46)):
            rod(details, (x, side * inner_y, low), (x, side * inner_y, high), 2.1, CARBON, 8)
            rod(details, (x, side * inner_y, z), (x, side * pivot_y, z), 2.2, CARBON, 8)
        rod(details, (-55, side * 24, 66), (42, side * 24, 55), 1.1, CARBON, 8)
        rod(details, (35, side * 29, 56), (52, side * 62, 64), 1.3, PAINT, 8)
        mirror = Geometry()
        rounded_loft(mirror, ((46, 5, 60, 68), (58, 5, 60, 68)), PAINT,
                     side * 63, sides=12, subdivisions=1)
        body.merge(mirror)
        # Rear-facing mirror insert.
        ellipsoid(details, (45.8, side * 63, 64), (.15, 4, 2.8), CARBON, 10, 4)
    # A simple round wheel and three spokes, with the hands on its sides.
    for i in range(16):
        a, b = i * math.tau / 16, (i + 1) * math.tau / 16
        rod(details, (26, 16 * math.cos(a), 66 + 12 * math.sin(a)),
            (26, 16 * math.cos(b), 66 + 12 * math.sin(b)), 1.4, CARBON, 6)
    for y, z in ((-14, 66), (14, 66), (0, 55)):
        rod(details, (26, 0, 66), (26, y, z), 1.1, CARBON, 6)
    body.faces.extend((material, face[::-1]) for material, face in details.faces)
    # Front wing has a separate pivot so existing crash deformation stays local.
    front = Geometry()
    wing(front, 0, 0, 204, 40, CARBON)
    for side in (-1, 1):
        rounded_plate(front, -22, 21, -2, 18, side * 102, 1.7, PAINT, corner=2.5)
        # The split upper flaps sit outside the nose, not through it.
        wing(front, -11, 6, 78, 19, PEARL, side * 61)
    # The thin centre support is tucked directly beneath the nose tip.
    loft(front, ((-6, 7, 5, 12), (10, 6, 5, 11)), PAINT)

    front.faces = [(material, face[::-1]) for material, face in front.faces]
    meshes = {}
    for name, material in (("Paint", PAINT), ("Trim", PEARL), ("Carbon", CARBON)):
        player = Geometry(); player.merge(player_body, material=material)
        opponent = Geometry(); opponent.merge(body, material=material); opponent.merge(front, (208, 0, 24), material)
        meshes[f"SM_F1Player{name}"] = player
        driver = Geometry(); driver.merge(player_driver, material=material)
        meshes[f"SM_F1PlayerDriver{name}"] = driver
        meshes[f"SM_F1Opponent{name}"] = opponent
    # The separately deformable player's front wing uses outward winding too.
    player_front = Geometry()
    player_front.merge(front)
    meshes["SM_F1FrontWing"] = player_front
    # Rivals use a 24-sided tyre with the same rounded shoulder; all axle,
    # radius and width contracts match the player, with lower distant cost.
    meshes["SM_F1WheelFront"] = wheel(35, 38, segments=40, rounded_shoulders=True)
    meshes["SM_F1WheelRear"] = wheel(38, 46, segments=40, rounded_shoulders=True)
    opponent_front = wheel(35, 38, segments=24, rounded_shoulders=True)
    opponent_rear = wheel(38, 46, segments=24, rounded_shoulders=True)
    meshes['SM_F1OpponentWheelFront'] = opponent_front
    meshes['SM_F1OpponentWheelRear'] = opponent_rear
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
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT
                                     if index == PAINT else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
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
        # Lacquer, white paint, resin/composite and rubber are dielectrics;
        # only the exposed wheel alloy is metallic. Keep wear/grain out of the
        # shader until proper authored UVs/textures exist, rather than adding
        # world-space glitter that would crawl across a moving racing car.
        output(scalar(material, (0, 0, 0, 0, .88)[index]), unreal.MaterialProperty.MP_METALLIC)
        output(scalar(material, (.24, .31, .40, .72, .28)[index]), unreal.MaterialProperty.MP_ROUGHNESS)
        output(scalar(material, (.5, .5, .40, .30, .55)[index]), unreal.MaterialProperty.MP_SPECULAR)
        # Clear-coat inputs are hidden EMaterialProperty enum entries in UE5.8
        # Python. Use the engine's supported MSM_CLEAR_COAT defaults (1.0/0.1)
        # instead of referring to non-existent MP_CLEAR_COAT Python attributes.
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
        wheel_surface = name in ('SM_F1WheelFront', 'SM_F1WheelRear', 'SM_F1OpponentWheels',
                                 'SM_F1OpponentWheelFront', 'SM_F1OpponentWheelRear')
        for material, face in geometry.faces:
            instances = []
            for position, uv in zip(face, face_uvs(face)):
                key = mesh_vertex_key(position, face, material, wheel_surface)
                if key not in vertices:
                    vertex = description.create_vertex()
                    description.set_vertex_position(vertex, unreal.Vector(*position))
                    vertices[key] = vertex
                instance = description.create_vertex_instance(vertices[key])
                description.set_vertex_instance_uv(instance, unreal.Vector2D(*uv), 0)
                instances.append(instance)
            description.create_triangle(groups[material], instances)
        mesh.build_from_static_mesh_descriptions([description], False, False)
        settings = subsystem.get_lod_build_settings(mesh, 0)
        settings.recompute_normals = True
        settings.recompute_tangents = True
        settings.compute_weighted_normals = True
        # Small aero/fastener faces must keep their UV differences even in
        # charts offset from the origin; half UVs can quantize these to zero.
        settings.use_full_precision_u_vs = True
        settings.generate_lightmap_u_vs = False
        subsystem.set_lod_build_settings(mesh, 0, settings)
        save(mesh)
        require(description.get_triangle_count() == len(geometry.faces), f"Triangle mismatch: {name}")
        unreal.log(f"VROOM CAR: {name}: {geometry.audit()}")


if __name__ == "__main__":
    geometry = make_geometry()
    audit = {name: mesh.audit() for name, mesh in geometry.items()}
    opponent_triangles = sum(audit[f"SM_F1Opponent{part}"]["triangles"] for part in ("Paint", "Trim", "Carbon", "Wheels"))
    assert opponent_triangles <= 10000, f"Car geometry budget exceeded: {opponent_triangles}"
    if "--audit" in sys.argv:
        print(json.dumps({"opponent_triangles": opponent_triangles, "meshes": audit}, indent=2))
    else:
        build_assets(geometry)
