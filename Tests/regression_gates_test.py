import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
from regression_gates import performance_failures, motion_failures


class ReleaseGates(unittest.TestCase):
    def test_sustained_drop_and_isolated_hitch_both_fail(self):
        report = dict(numeric_frame_rows=1800, invalid_frames_after_warmup=0,
                      mean_fps=60, frame_time=dict(p99_ms=17, max_ms=18, samples=1680, over_20_ms_count=0))
        self.assertEqual(performance_failures(report, 1800), [])
        report['mean_fps'] = 55
        self.assertTrue(performance_failures(report, 1800))
        report['mean_fps'] = 60
        report['frame_time']['max_ms'] = 100
        self.assertTrue(performance_failures(report, 1800))

    def test_bad_sample_count_fails(self):
        report = dict(numeric_frame_rows=120, invalid_frames_after_warmup=0,
                      mean_fps=60, frame_time=dict(p99_ms=17, max_ms=18, samples=120, over_20_ms_count=0))
        self.assertTrue(performance_failures(report, 1800))

    def test_reverse_camera_or_large_yaw_snap_fails(self):
        report = dict(analyzed_samples=1500, all_analyzed_telemetry_valid=True,
                      distance_backsteps_count=0, yaw_delta_degrees=dict(max_absolute=.8),
                      yaw_acceleration_degrees_per_second_squared=dict(max_absolute=80))
        self.assertEqual(motion_failures(report), [])
        report['distance_backsteps_count'] = 1
        self.assertTrue(motion_failures(report))
        report['distance_backsteps_count'] = 0
        report['yaw_delta_degrees']['max_absolute'] = 8
        self.assertTrue(motion_failures(report))


if __name__ == '__main__':
    unittest.main()
