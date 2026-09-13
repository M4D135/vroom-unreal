"""Check first visible wheel surfaces using Unreal's MeshDescription winding."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
from bootstrap_car_assets import ALLOY, RUBBER, cross, sub, wheel


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


class WheelSurfaces(unittest.TestCase):
    def assert_visible(self, mesh, origin, direction, material):
        distance, actual_material, facing = first_hit(mesh, origin, direction)
        self.assertEqual(actual_material, material)
        self.assertLess(facing, -1e-8, 'The nearest physical surface is back-facing and would be culled')
        return distance

    def test_tread_faces_outward_around_the_whole_tyre(self):
        for radius, width in ((35, 38), (38, 46)):
            for segments in (20, 40):
                mesh = wheel(radius, width, segments)
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
                mesh = wheel(radius, width, segments)
                for side in (-1, 1):
                    for fraction, material in ((.65, RUBBER), (.43, ALLOY), (.28, ALLOY)):
                        with self.subTest(radius=radius, segments=segments, side=side, radius_fraction=fraction):
                            self.assert_visible(mesh, (radius * fraction, side * width * 3, 0),
                                                (0, -side, 0), material)

    def test_both_inner_rim_walls_face_the_opening(self):
        for radius, width in ((35, 38), (38, 46)):
            for segments in (20, 40):
                mesh = wheel(radius, width, segments)
                for side in (-1, 1):
                    with self.subTest(radius=radius, segments=segments, side=side):
                        self.assert_visible(mesh, (radius * .2, side * (width * .505 - 2.5), 0),
                                            (1, 0, 0), ALLOY)

    def test_hub_wall_and_end_caps_face_outward(self):
        for radius, width in ((35, 38), (38, 46)):
            mesh = wheel(radius, width, 40)
            self.assert_visible(mesh, (10, 0, 0), (-1, 0, 0), ALLOY)
            for side in (-1, 1):
                with self.subTest(radius=radius, side=side):
                    self.assert_visible(mesh, (2, side * width * 3, 1), (0, -side, 0), ALLOY)


if __name__ == '__main__':
    unittest.main()
