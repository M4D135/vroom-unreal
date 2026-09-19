"""Check first visible wheel surfaces using Unreal's MeshDescription winding."""
import math
from pathlib import Path
import re
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
from bootstrap_car_assets import ALLOY, CARBON, PAINT, RUBBER, Geometry, cross, face_uvs, make_geometry, make_player_body, mesh_vertex_key, sub, wheel


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def first_hit(mesh, origin, direction):
    hits = []
    for material, (a, b, c) in mesh.faces:
        edge1, edge2 = sub(b, a), sub(c, a)
        p = cross(direction, edge2)
        determinant = dot(edge1, p)
        if abs(determinant) < 1e-10:
            continue
        offset = sub(origin, a)
        u = dot(offset, p) / determinant
        q = cross(offset, edge1)
        v = dot(direction, q) / determinant
        distance = dot(edge2, q) / determinant
        if u >= -1e-8 and v >= -1e-8 and u + v <= 1 + 1e-8 and distance > 1e-8:
            # Both UV and no-UV paths in UE StaticMeshOperations use this order.
            normal = cross(edge2, edge1)
            hits.append((distance, material, dot(normal, direction)))
    if not hits:
        raise AssertionError('Ray passed through the wheel without hitting a surface')
    return min(hits)


class CarSurfaceUVs(unittest.TestCase):
    def test_every_car_face_has_a_noncollapsed_tangent_basis(self):
        for name, mesh in make_geometry().items():
            for index, (_, face) in enumerate(mesh.faces):
                uv = face_uvs(face)
                dp1, dp2 = sub(face[1], face[0]), sub(face[2], face[0])
                duv1, duv2 = sub(uv[1], uv[0]), sub(uv[2], uv[0])
                determinant = duv1[0] * duv2[1] - duv1[1] * duv2[0]
                area = math.sqrt(dot(cross(dp1, dp2), cross(dp1, dp2)))
                with self.subTest(mesh=name, face=index):
                    # Dominant-axis projection retains at least 1/sqrt(3)
                    # of the geometric area (UVs are scaled .01 per cm).
                    self.assertGreaterEqual(abs(determinant), area * .0001 / math.sqrt(3) * .99999)
                    tangent = tuple((a * duv2[1] - b * duv1[1]) / determinant for a, b in zip(dp1, dp2))
                    bitangent = tuple((b * duv1[0] - a * duv2[0]) / determinant for a, b in zip(dp1, dp2))
                    self.assertTrue(all(math.isfinite(v) for v in (*tangent, *bitangent)))
                    self.assertGreater(dot(tangent, tangent), 1.e-8)
                    self.assertGreater(dot(bitangent, bitangent), 1.e-8)

    def test_uv_direction_does_not_depend_on_triangle_corner_order(self):
        # Includes vertical wing caps and rods, where the old arbitrary
        # (0,0)/(1,0)/(0,1) mapping could oppose neighbouring tangents.
        for name, mesh in make_geometry().items():
            for _, face in mesh.faces:
                expected = dict(zip(face, face_uvs(face)))
                rotated = face[1:] + face[:1]
                with self.subTest(mesh=name):
                    self.assertEqual(dict(zip(rotated, face_uvs(rotated))), expected)


class WheelSurfaces(unittest.TestCase):
    def assert_visible(self, mesh, origin, direction, material):
        distance, actual_material, facing = first_hit(mesh, origin, direction)
        self.assertEqual(actual_material, material)
        self.assertLess(facing, -1e-8, 'The nearest physical surface is back-facing and would be culled')
        return distance

    def test_tread_faces_outward_around_the_whole_tyre(self):
        for radius, width in ((35, 38), (38, 46)):
            for segments in (20, 40):
                mesh = wheel(radius, width, segments, rounded_shoulders=segments == 40)
                for i in range(36):
                    angle = i * math.tau / 36 + .037
                    radial = (math.cos(angle), 0, math.sin(angle))
                    with self.subTest(radius=radius, segments=segments, angle=i):
                        distance = self.assert_visible(mesh, tuple(3 * radius * v for v in radial),
                                                       tuple(-v for v in radial), RUBBER)
                        self.assertGreaterEqual(3 * radius - distance, .98 * radius)
                        self.assertLessEqual(3 * radius - distance, radius + 1e-7)

    def test_both_sidewalls_and_rim_faces_face_outward(self):
        for radius, width in ((35, 38), (38, 46)):
            for segments in (20, 40):
                mesh = wheel(radius, width, segments, rounded_shoulders=segments == 40)
                for side in (-1, 1):
                    for fraction, material in ((.65, RUBBER), (.43, ALLOY), (.28, ALLOY)):
                        with self.subTest(radius=radius, segments=segments, side=side, radius_fraction=fraction):
                            self.assert_visible(mesh, (radius * fraction, side * width * 3, 0),
                                                (0, -side, 0), material)

    def test_both_inner_rim_walls_face_the_opening(self):
        for radius, width in ((35, 38), (38, 46)):
            for segments in (20, 40):
                mesh = wheel(radius, width, segments, rounded_shoulders=segments == 40)
                for side in (-1, 1):
                    with self.subTest(radius=radius, segments=segments, side=side):
                        self.assert_visible(mesh, (radius * .2, side * (width * .505 - 2.5), 0),
                                            (1, 0, 0), ALLOY)

    def test_hub_wall_and_end_caps_face_outward(self):
        for radius, width in ((35, 38), (38, 46)):
            mesh = wheel(radius, width, 40, rounded_shoulders=True)
            self.assert_visible(mesh, (10, 0, 0), (-1, 0, 0), ALLOY)
            for side in (-1, 1):
                with self.subTest(radius=radius, side=side):
                    self.assert_visible(mesh, (2, side * width * 3, 1), (0, -side, 0), ALLOY)

    def test_close_slick_shoulders_round_over_without_changing_wheel_bounds(self):
        for radius, width in ((35, 38), (38, 46)):
            old = wheel(radius, width, 40)
            refined = wheel(radius, width, 40, rounded_shoulders=True)
            self.assertEqual(old.audit()['bounds_cm'], refined.audit()['bounds_cm'])
            self.assertEqual(len(refined.faces) - len(old.faces), 320)
            self.assertEqual([face for mat, face in old.faces if mat == ALLOY],
                             [face for mat, face in refined.faces if mat == ALLOY],
                             'Rounding the rubber must not move or rebuild rim openings')
            shoulder_rings = set()
            for material, face in refined.faces:
                if material != RUBBER:
                    continue
                for x, y, z in face:
                    r = math.hypot(x, z)
                    if r >= .84 * radius - 1.e-6:
                        a = (abs(y) / width - .4) / .1
                        b = (r / radius - .84) / .16
                        self.assertAlmostEqual(a * a + b * b, 1.0, places=6)
                        shoulder_rings.add(round(y, 6))
            self.assertEqual(len(shoulder_rings), 8)

    def test_rim_openings_have_a_separate_normal_seam_from_their_flat_lip(self):
        mesh = wheel(35, 38, 40, rounded_shoulders=True)
        position = (35 * .385, 38 * .505, 0)
        candidates = [face for material, face in mesh.faces if material == ALLOY
                      and any(math.dist(vertex, position) < 1.e-6 for vertex in face)]
        planar = [face for face in candidates if max(p[1] for p in face) - min(p[1] for p in face) < 1.e-6]
        barrels = [face for face in candidates if face not in planar]
        self.assertTrue(planar and barrels)
        self.assertNotEqual(mesh_vertex_key(position, planar[0], ALLOY, True),
                            mesh_vertex_key(position, barrels[0], ALLOY, True))
        self.assertEqual({mesh_vertex_key(position, face, ALLOY, True) for face in barrels},
                         {mesh_vertex_key(position, barrels[0], ALLOY, True)},
                         'The curved inside of the rim must remain smooth between its own segments')

    def test_split_rival_wheels_keep_central_pivots_material_slots_and_axle_positions(self):
        meshes = make_geometry()
        assembly = Geometry()
        for part, radius, width, x, lateral in (('Front', 35, 38, 128, 91), ('Rear', 38, 46, -115, 96)):
            mesh = meshes['SM_F1OpponentWheel' + part]
            bounds = mesh.audit()['bounds_cm']
            self.assertEqual(bounds, [[-radius, round(-width * .52, 2), -radius],
                                     [radius, round(width * .52, 2), radius]])
            self.assertEqual(sorted({material for material, _ in mesh.faces}), [RUBBER, ALLOY])
            self.assertLessEqual(len(mesh.faces), 900)
            for side in (-1, 1):
                assembly.merge(mesh, (x, side * lateral, radius))
        self.assertCountEqual(assembly.faces, meshes['SM_F1OpponentWheels'].faces)

    def test_rotation_markers_stay_inside_the_wheel_envelope_without_bright_paint(self):
        mesh = wheel(35, 38, 40, rounded_shoulders=True)
        markers = [face for material, face in mesh.faces if material == RUBBER
                   and all(abs(abs(point[1]) - 38 * .502) < 1.e-6 for point in face)]
        self.assertEqual(len(markers), 8)
        for face in markers:
            self.assertTrue(all(.70 * 35 < math.hypot(x, z) < .81 * 35 for x, _, z in face))
        angles = [math.atan2(z, x) for face in markers for x, _, z in face]
        self.assertTrue(any(.30 < angle < .45 for angle in angles))
        self.assertTrue(any(-2.8 < angle < -2.6 for angle in angles))


class PlayerCockpitVisibility(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.body = make_player_body()
        cls.meshes = make_geometry()
        cls.camera = (-65, 0, 82)

    def test_road_and_horizon_are_clear_across_the_cockpit_frustum(self):
        # The old shared model placed its helmet, shoulder wall and airbox in
        # front of this actual driver viewpoint, hiding the entire road.
        for yaw in range(-34, 35, 4):
            for pitch in (0, 5, 10, 20):
                a, b = math.radians(yaw), math.radians(pitch)
                direction = (math.cos(a) * math.cos(b), math.sin(a) * math.cos(b), math.sin(b))
                with self.subTest(yaw=yaw, pitch=pitch), self.assertRaises(AssertionError):
                    first_hit(self.body, self.camera, direction)

    def test_steering_wheel_and_grip_volume_stay_in_the_open_cockpit(self):
        for y in (-20, 0, 20):
            for z in (40, 49, 58):
                with self.subTest(y=y, z=z):
                    direction = sub((40, y, z), self.camera)
                    # Unnormalised direction makes t=1 the requested hand/
                    # wheel point; any nearest body hit must lie beyond it.
                    try:
                        distance, _, _ = first_hit(self.body, self.camera, direction)
                    except AssertionError:
                        continue  # An unobstructed ray can also miss the car entirely.
                    self.assertGreater(distance, 1.02)

    def test_long_red_nose_is_visible_and_faces_the_driver(self):
        for target in ((80, 0, 61), (115, 0, 61.8), (157, 0, 58), (196, 0, 54.3)):
            with self.subTest(target=target):
                distance, material, facing = first_hit(self.body, self.camera, sub(target, self.camera))
                self.assertAlmostEqual(distance, 1.0, places=6)
                self.assertEqual(material, PAINT)
                self.assertLess(facing, 0, 'Visible nose must not be culled by Unreal winding')

    def test_terminal_nose_is_thin_and_opaque_from_the_external_front_view(self):
        tip = [point for _, face in self.body.faces for point in face if abs(point[0] - 225) < 1.e-6]
        self.assertAlmostEqual(max(point[2] for point in tip) - min(point[2] for point in tip), 8)
        distance, material, facing = first_hit(self.body, (260, 0, 34), (-1, 0, 0))
        self.assertAlmostEqual(distance, 35)
        self.assertEqual(material, PAINT)
        self.assertLess(facing, 0)

    def test_runtime_front_suspension_pickups_are_buried_in_the_actual_nose(self):
        source = (Path(__file__).resolve().parents[1] / 'Source/Vroom/Public/VroomRaceRules.h').read_text()
        table = source.split('static const Wishbone Bars[] = {', 1)[1].split('};', 1)[0]
        pickups = re.findall(r'\{([^{}]+),\s*true\}', table)
        self.assertEqual(len(pickups), 12)
        for values in pickups:
            x, y, z, _, _, _ = [float(value.strip()) for value in values.split(',')]
            side = 1 if y > 0 else -1
            with self.subTest(pickup=(x, y, z)):
                side_hit, material, facing = first_hit(self.body, (x, side * 200, z), (0, -side, 0))
                self.assertEqual(material, PAINT)
                self.assertLess(facing, 0)
                self.assertGreater(200 - abs(y) - side_hit, .5,
                                   'Inner suspension endpoint floats outside the nose skin')
                top_hit, material, facing = first_hit(self.body, (x, y, 200), (0, 0, -1))
                self.assertEqual(material, PAINT)
                self.assertGreater(200 - z - top_hit, .5,
                                   'Inner suspension endpoint protrudes above the nose')

    def test_real_front_tyres_remain_visible_without_forward_offset(self):
        combined = Geometry()
        combined.merge(self.body)
        for side in (-1, 1):
            combined.merge(self.meshes['SM_F1WheelFront'], (128, side * 91, 35))
        for side in (-1, 1):
            distance, material, facing = first_hit(combined, self.camera,
                                                   sub((128, side * 91, 60), self.camera))
            self.assertEqual(material, RUBBER)
            self.assertLess(distance, 1.0)
            self.assertLess(facing, 0)

    def test_player_split_keeps_the_opponent_driver_and_triangle_budgets(self):
        paint = self.meshes['SM_F1OpponentPaint']
        self.assertTrue(any(-31 < p[0] < 0 and abs(p[1]) < 15 and p[2] > 100
                            for _, face in paint.faces for p in face), 'Opponent driver helmet disappeared')
        opponent_count = sum(len(self.meshes['SM_F1Opponent' + part].faces)
                             for part in ('Paint', 'Trim', 'Carbon', 'Wheels'))
        self.assertLessEqual(opponent_count, 10000)
        self.assertLessEqual(len(self.body.faces), 5000)
        self.body.audit()

    def test_rear_has_a_visible_transmission_diffuser_and_tapered_pod_tails(self):
        # Gearbox and five diffuser exits must be opaque outside-facing carbon
        # when approached from the chase-camera side of the car.
        for y, z in ((8, 33), (-46, 24), (-24, 24), (0, 24), (24, 24), (46, 24)):
            with self.subTest(y=y, z=z):
                _, material, facing = first_hit(self.body, (-250, y, z), (1, 0, 0))
                self.assertEqual(material, CARBON)
                self.assertLess(facing, 0)
        for side in (-1, 1):
            distance, material, facing = first_hit(self.body, (-250, side * 54, 36), (1, 0, 0))
            self.assertEqual(material, PAINT)
            self.assertGreater(-250 + distance, -133,
                               'A thick pod tail still hangs behind the rear wheel like a red tube')
            self.assertLess(facing, 0)

    def test_rival_body_and_helmet_present_outside_surfaces(self):
        rival = Geometry()
        for part in ('Paint', 'Trim', 'Carbon'):
            rival.merge(self.meshes['SM_F1Opponent' + part])
        for origin, direction in (((190, 0, 200), (0, 0, -1)),
                                  ((100, 0, 200), (0, 0, -1)),
                                  ((-16, 0, 200), (0, 0, -1)),
                                  ((-54, -200, 45), (0, 1, 0)),
                                  ((-54, 200, 45), (0, -1, 0))):
            with self.subTest(origin=origin):
                _, _, facing = first_hit(rival, origin, direction)
                self.assertLess(facing, 0, 'Rival body is inside-out and would expose its interior')

    def test_rival_fixed_axles_reach_all_four_wheel_pivots(self):
        carbon = self.meshes['SM_F1OpponentCarbon']
        vertices = {tuple(round(value, 6) for value in point) for _, face in carbon.faces for point in face}
        for x, lateral, z in ((128, 91, 35), (-115, 96, 38)):
            for side in (-1, 1):
                self.assertIn((x, side * lateral, z), vertices,
                              'An axle must meet the centre pivot so steering cannot detach the rim from the suspension')

    def test_chase_driver_is_separate_coloured_geometry_with_an_outward_helmet(self):
        driver = Geometry()
        for part in ('Paint', 'Trim', 'Carbon'):
            mesh = self.meshes['SM_F1PlayerDriver' + part]
            self.assertEqual(len({material for material, _ in mesh.faces}), 1,
                             'Each driver component needs one material so its override cannot recolour the visor or gloves')
            driver.merge(mesh)
        distance, material, facing = first_hit(driver, (-16, 0, 200), (0, 0, -1))
        self.assertAlmostEqual(distance, 93, places=5)
        self.assertEqual(material, PAINT)
        self.assertLess(facing, 0)
        self.assertLessEqual(len(driver.faces), 1600)
        self.assertTrue(all(point[0] < 1 for _, face in driver.faces for point in face),
                        'Player must use the real cockpit arms/gloves in chase, without a second baked pose')
        self.assertTrue(any(point[0] > 23 and 59 < point[2] < 72
                            for _, face in self.meshes['SM_F1OpponentTrim'].faces for point in face),
                        'Rival gloves must remain on their separate static steering wheel')
        # The same helmet does intersect the eye's forward view if merged by
        # mistake, while the dedicated body tests above require it to stay clear.
        distance, _, _ = first_hit(driver, self.camera, (1, 0, 0))
        self.assertGreater(distance, 0)

    def test_refined_front_wing_preserves_its_envelope_and_outside_surfaces(self):
        front = self.meshes['SM_F1FrontWing']
        self.assertEqual(front.audit()['bounds_cm'], [[-22, -103.7, -2], [21, 103.7, 18]])
        # Top main plane, separate flap and both end plates remain opaque from
        # outside after refining the camber and its rounded leading edge.
        for origin, direction in (((0, 40, 100), (0, 0, -1)),
                                  ((-10, 40, 100), (0, 0, -1)),
                                  ((0, -180, 5), (0, 1, 0)),
                                  ((0, 180, 5), (0, -1, 0))):
            with self.subTest(origin=origin):
                _, _, facing = first_hit(front, origin, direction)
                self.assertLess(facing, 0, 'A smoothed wing profile must not invert its front faces')


if __name__ == '__main__':
    unittest.main()
