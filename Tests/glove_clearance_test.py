import hashlib
import json
import math
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools'))
from audit_cockpit_grip import audit, source_parameters, glove_vertices, rim_segments, surface_clearance, segment_triangle_sq


def thumb_patch_measurements(samples, params, side):
    """Measure a fixed anatomical patch, never the nearest arbitrary vertices.

    Distance uses the true circular tube. The separate all-face capsule audit
    conservatively certifies the rendered faceted rim's non-intersection.
    Normal and point receive the same rigid hand rotation/reflection; common
    steering and wheel tilt cannot change these relative measurements.
    """
    cosine = math.cos(math.radians(params['wrist_tilt']))
    sine = math.sin(math.radians(params['wrist_tilt']))
    result = {}
    for index, sample in samples.items():
        point = glove_vertices({'vertices': [sample['position']]}, params, side)[0]
        x, y, z = point
        radius = math.hypot(y, z)
        axis = (params['depth_base'], params['rim_radius']*y/radius,
                params['rim_radius']*z/radius)
        toward = tuple(axis[i]-point[i] for i in range(3))
        distance = math.sqrt(sum(value*value for value in toward))
        nx, ny, nz = sample['normal']
        normal = (cosine*nx-sine*ny, -side*(sine*nx+cosine*ny), nz)
        normal_length = math.sqrt(sum(value*value for value in normal))
        result[int(index)] = {
            'gap_cm': distance-params['tube_radius'],
            'normal_toward_rim': sum(normal[i]*toward[i] for i in range(3))
                               / (normal_length*distance),
            'radial_position_cm': radius,
        }
    return result


def thumb_contact_failures(measurements, patch, params):
    """A distal palmar contact inside the ring, with local support."""
    principal = measurements[patch['principal_distal_vertex']]
    failures = []
    if not .1 <= principal['gap_cm'] <= .4:
        failures.append('distal palmar pad must be 1–4 mm from the rim surface')
    if principal['normal_toward_rim'] <= .3:
        failures.append('distal palmar pad must face the rim')
    if principal['radial_position_cm'] > params['rim_radius']:
        failures.append('distal palmar pad must return inside the circle')
    if not any(0 <= measurements[i]['gap_cm'] <= .6
               and measurements[i]['normal_toward_rim'] > .3
               for i in patch['distal_neighbor_indices']):
        failures.append('a neighboring palmar pad point must support the grip')
    return failures


class GripGeometry(unittest.TestCase):
    def setUp(self):
        self.glove=json.loads((ROOT/'Content/Data/gloves.json').read_text())
        self.baseline=json.loads((ROOT/'Tests/fixtures/glove_anatomy_baseline.json').read_text())

    @staticmethod
    def digest(value):
        return hashlib.sha256(json.dumps(value,separators=(',', ':'),allow_nan=False).encode()).hexdigest()

    def test_original_glove_topology_and_uvs_are_preserved(self):
        baseline = self.baseline
        self.assertEqual(self.glove['source']['sha256'], baseline['source_glb_sha256'])
        self.assertEqual(len(self.glove['vertices']), baseline['vertex_count'])
        self.assertEqual(len(self.glove['indices'])//3, baseline['triangle_count'])
        self.assertEqual(self.digest(self.glove['indices']), baseline['indices_sha256'])
        self.assertEqual(self.digest(self.glove['uvs']), baseline['uvs_sha256'])

    def test_four_fingers_and_non_thumb_positions_remain_exactly_unchanged(self):
        baseline = self.baseline
        positions = [self.glove['vertices'][i] for i in baseline['non_thumb_vertex_indices']]
        normals = [self.glove['normals'][i] for i in baseline['protected_finger_vertex_indices']]
        self.assertEqual(self.digest(positions), baseline['non_thumb_positions_sha256'])
        self.assertEqual(self.digest(normals), baseline['protected_finger_normals_sha256'])
        self.assertEqual(len(baseline['protected_finger_vertex_indices']), 1671)

    def test_hand_attachment_preserves_the_established_white_glove_pose(self):
        source = (ROOT/'Source/Vroom/Private/VroomCockpit.cpp').read_text()
        params, _ = source_parameters(source, self.glove)
        for key, expected in {'neutral_angle':0., 'wrist_tilt':80., 'hand_scale':.92,
                              'grip_depth':-2.8, 'grip_reach':19.1, 'depth_offset':-.4}.items():
            self.assertAlmostEqual(params[key], expected)
        # The thumb changes through its authored rig, while wrist orientation,
        # hand size, contact anchor and four-finger geometry stay unchanged.
        self.assertTrue(self.glove['validation']['thumb_source_pose']['source_unfitted'])
        self.assertTrue(self.glove['validation']['thumb_source_pose']['normals_updated'])

    def test_current_runtime_rim_clears_every_face_of_both_gloves(self):
        report = audit()
        self.assertEqual(report['status'], 'PASS')
        for hand in report['hands'].values():
            self.assertEqual(hand['glove_triangles'], len(self.glove['indices'])//3)
            self.assertGreaterEqual(hand['minimum_surface_clearance_cm'], .05)

    def test_thumb_pulp_wraps_and_faces_the_rim_in_both_hands(self):
        # A clearance-only gate previously accepted an open thumb, then a
        # visible hook whose DORSAL surface faced the rim. UV-anchored source
        # pad samples prevent both errors without fixing skeletal angles.
        patch = self.baseline['thumb_contact_patch']
        self.assertEqual(patch['source_glb_sha256'], self.glove['source']['sha256'])
        samples = {}
        for index in patch['sample_vertex_indices']:
            self.assertEqual(self.glove['uvs'][index], patch['uvs'][str(index)])
            samples[str(index)] = {'position': self.glove['vertices'][index],
                                   'normal': self.glove['normals'][index]}
        source = (ROOT/'Source/Vroom/Private/VroomCockpit.cpp').read_text()
        params, _ = source_parameters(source, self.glove)
        for side in (-1, 1):
            with self.subTest(side=side):
                measurements = thumb_patch_measurements(samples, params, side)
                failures = thumb_contact_failures(measurements, patch, params)
                self.assertEqual(failures, [], f'{failures}; {measurements}')

    def test_palmar_contact_gate_rejects_open_thumb_and_dorsal_hook(self):
        patch = self.baseline['thumb_contact_patch']
        source = (ROOT/'Source/Vroom/Private/VroomCockpit.cpp').read_text()
        params, _ = source_parameters(source, self.glove)
        self.assertEqual({example['name'] for example in patch['negative_examples']},
                         {'open_thumb', 'dorsal_hook'})
        for example in patch['negative_examples']:
            for side in (-1, 1):
                with self.subTest(example=example['name'], side=side):
                    measurements = thumb_patch_measurements(example['samples'], params, side)
                    self.assertTrue(thumb_contact_failures(measurements, patch, params))
                    principal = measurements[patch['principal_distal_vertex']]
                    if example['name'] == 'open_thumb':
                        self.assertGreater(principal['gap_cm'], 1.)
                        self.assertGreater(principal['radial_position_cm'], params['rim_radius'])
                    else:
                        self.assertLess(principal['normal_toward_rim'], -.9)
                        self.assertGreater(principal['gap_cm'], 1.5)

    def test_runtime_rim_is_a_circle_with_constant_depth_and_thickness(self):
        source = (ROOT/'Source/Vroom/Private/VroomCockpit.cpp').read_text()
        params, _ = source_parameters(source, self.glove)
        self.assertEqual(params['radial_recess'], 0)
        self.assertEqual(params['depth_recess'], 0)
        self.assertEqual(params['tube_reduction'], 0)
        self.assertAlmostEqual(params['rim_radius'], 11.9)
        self.assertAlmostEqual(params['depth_base'], -.34)
        self.assertAlmostEqual(params['tube_radius'], .765)
        for start, end, radius in rim_segments(params):
            self.assertAlmostEqual(radius, .765)
            for x, y, z in (start, end):
                self.assertAlmostEqual(x, -.34)
                self.assertAlmostEqual(math.hypot(y, z), 11.9)

    def test_previous_thumb_face_is_rejected_against_the_true_circle(self):
        # Preserve the concrete regression: the old thumb crosses the upper
        # side arc even though sparse vertex checks can overlook the face.
        source = (ROOT/'Source/Vroom/Private/VroomCockpit.cpp').read_text()
        params, _ = source_parameters(source, self.glove)
        params.update(grip_depth=-2.8, grip_reach=19.1, depth_offset=-.4,
                      hand_scale=.92, wrist_tilt=80., neutral_angle=0.)
        legacy = {'vertices': self.baseline['legacy_thumb_triangle_vertices']}
        report = surface_clearance(glove_vertices(legacy,params,-1),
                                   [0,1,2],rim_segments(params))
        self.assertLess(report['minimum_surface_clearance_cm'], 0)

    def test_capsule_detects_large_face_interior_away_from_vertices(self):
        triangle = [(-10,-10,0),(10,-10,0),(0,10,0)]
        report = surface_clearance(triangle,[0,1,2],[((0,0,-1),(0,0,1),.1)])
        self.assertAlmostEqual(report['minimum_surface_clearance_cm'], -.1)
        self.assertAlmostEqual(segment_triangle_sq((.1,.1,2),(.2,.2,2),
                               (0,0,0),(1,0,0),(0,1,0)), 4)

    def test_audit_refuses_unreviewed_cpp_geometry(self):
        source = (ROOT/'Source/Vroom/Private/VroomCockpit.cpp').read_text()
        changed = source.replace('Radii.Add(.90', 'Radii.Add(.95', 1)
        self.assertNotEqual(changed, source)
        with self.assertRaisesRegex(ValueError, 'Unsupported cockpit geometry changed'):
            source_parameters(changed, self.glove)


if __name__=='__main__':
    unittest.main()
