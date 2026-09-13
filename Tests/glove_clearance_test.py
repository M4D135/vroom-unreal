import json
import math
from pathlib import Path
import unittest


class GripGeometry(unittest.TestCase):
    def test_both_hands_keep_rim_clearance_through_steering(self):
        glove=json.loads((Path(__file__).resolve().parents[1]/'Content/Data/gloves.json').read_text())
        self.assertFalse(glove['validation']['rim_clearance']['pose_changed'])
        self.assertEqual(len(glove['vertices']),3028)
        self.assertEqual(len(glove['indices'])//3,4899)
        pose=glove['driving_grip']; scale=pose['scale']
        ct,st=math.cos(math.radians(pose['wrist_tilt_deg'])),math.sin(math.radians(pose['wrist_tilt_deg']))
        for side in (-1,1):
            for steering in (-.62,0,.62):
                angle=math.radians((180-pose['neutral_angle_deg'] if side<0 else pose['neutral_angle_deg'])-steering/.62*95)
                c,s=math.cos(angle-math.pi),math.sin(angle-math.pi)
                for vertex in glove['vertices']:
                    x,y,z=(vertex[0]+2.5)*scale,(vertex[1]-19.1)*scale,-side*(vertex[2]+5.4)*scale
                    x,y=ct*x-st*y,st*x+ct*y
                    y,z=c*y-s*z,s*y+c*z
                    y+=18.8*math.cos(angle);z+=18.8*math.sin(angle)
                    self.assertGreaterEqual(math.hypot(x,math.hypot(y,z)-18.8),1.2149)


if __name__=='__main__':
    unittest.main()
