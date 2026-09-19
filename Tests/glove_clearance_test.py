import json
import math
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools'))
from fit_glove_clearance import fit


class GripGeometry(unittest.TestCase):
    def setUp(self):
        self.glove=json.loads((ROOT/'Content/Data/gloves.json').read_text())

    def test_both_hands_keep_vertex_clearance_through_steering(self):
        # Vertex clearance does not imply triangle-interior clearance. Contact
        # appearance is also checked in the actual neutral cockpit capture.
        glove=self.glove
        self.assertFalse(glove['validation']['rim_clearance']['pose_changed'])
        self.assertEqual(glove['validation']['rim_clearance']['clearance_scope'],'vertices_only')
        self.assertEqual(len(glove['vertices']),3028)
        self.assertEqual(len(glove['indices'])//3,4899)
        pose=glove['driving_grip']; scale=pose['scale']
        ct,st=math.cos(math.radians(pose['wrist_tilt_deg'])),math.sin(math.radians(pose['wrist_tilt_deg']))
        for side in (-1,1):
            for steering in (-.62,0,.62):
                angle=math.radians((180-pose['neutral_angle_deg'] if side<0 else pose['neutral_angle_deg'])-steering/.62*95)
                c,s=math.cos(angle-math.pi),math.sin(angle-math.pi)
                for vertex in glove['vertices']:
                    x,y,z=(vertex[0]-pose['grip_depth_cm'])*scale,(vertex[1]-pose['grip_reach_cm'])*scale,-side*(vertex[2]+5.4)*scale
                    x,y=ct*x-st*y,st*x+ct*y
                    y,z=c*y-s*z,s*y+c*z
                    y+=18.8*math.cos(angle);z+=18.8*math.sin(angle)
                    self.assertGreaterEqual(math.hypot(x,math.hypot(y,z)-18.8),1.2149)

    def test_tightening_retains_030_anatomy_and_stays_bounded(self):
        pose=self.glove['driving_grip']
        self.assertEqual(pose,{'neutral_angle_deg':30.,'wrist_tilt_deg':80.,
            'scale':1.05,'grip_depth_cm':-2.8,'grip_reach_cm':19.1})
        delta=self.glove['validation']['grip_tightening']
        self.assertAlmostEqual(delta['grip_translation_world_cm'],.315)
        self.assertLessEqual(delta['max_local_refit_vs_030_world_cm'],.5)
        self.assertFalse(delta['rotation_scale_height_changed'])
        self.assertFalse(delta['topology_and_uvs_changed'])

    def test_refitting_preserves_faces_uvs_and_vertex_positions(self):
        # In particular, clearance fitting must never hide a collision by
        # deleting triangles or introducing an open cut in the glove.
        fitted=fit(self.glove)
        self.assertEqual(fitted['indices'],self.glove['indices'])
        self.assertEqual(fitted['uvs'],self.glove['uvs'])
        self.assertEqual(len(fitted['vertices']),len(self.glove['vertices']))
        for old,new in zip(self.glove['vertices'],fitted['vertices']):
            self.assertLess(math.dist(old,new),1.e-6)


if __name__=='__main__':
    unittest.main()
