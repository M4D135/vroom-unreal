import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
from regression_gates import (performance_failures, motion_failures, PERFORMANCE_LIMITS,
                              PHOTOREALISM_PERFORMANCE_LIMITS, performance_limits_for_preset,
                              release_limits_for_preset, check_release_limits)


class ReleaseGates(unittest.TestCase):
    def photo_report(self):
        return dict(numeric_frame_rows=1800, invalid_frames_after_warmup=0, mean_fps=30.0,
                    frame_time=dict(p99_ms=34, max_ms=40, samples=1680,
                                    over_20_ms_count=1680, over_40_ms_count=0))

    def test_only_photorealism_gets_the_30hz_budget(self):
        for preset in (None, 'performance', 'balanced', 'high', 'ultra'):
            self.assertEqual(performance_limits_for_preset(preset), PERFORMANCE_LIMITS)
            self.assertTrue(performance_failures(self.photo_report(), 1800, performance_limits_for_preset(preset)))
        self.assertEqual(performance_failures(self.photo_report(), 1800,
                                             performance_limits_for_preset('photorealism')), [])
        with self.assertRaises(ValueError):
            performance_limits_for_preset('photo')

    def test_every_30hz_limit_is_enforced(self):
        for field, value in (('mean_fps', 29.69), ('p99_ms', 35.01), ('max_ms', 50.01), ('over_40_ms_count', 9)):
            with self.subTest(field=field):
                report = self.photo_report()
                (report if field == 'mean_fps' else report['frame_time'])[field] = value
                self.assertTrue(performance_failures(report, 1800, PHOTOREALISM_PERFORMANCE_LIMITS))
        report = self.photo_report()
        report['mean_fps'] = 29.7
        report['frame_time'].update(p99_ms=35, max_ms=50, over_40_ms_count=8)
        self.assertEqual(performance_failures(report, 1800, PHOTOREALISM_PERFORMANCE_LIMITS), [])

    def test_missing_40ms_counter_and_nonfinite_metrics_fail_closed(self):
        report = self.photo_report()
        report['frame_time'].pop('over_40_ms_count')
        self.assertTrue(performance_failures(report, 1800, PHOTOREALISM_PERFORMANCE_LIMITS))
        for field in ('mean_fps', 'p99_ms', 'max_ms', 'samples', 'over_40_ms_count'):
            with self.subTest(field=field):
                report = self.photo_report()
                (report if field == 'mean_fps' else report['frame_time'])[field] = float('nan')
                self.assertTrue(performance_failures(report, 1800, PHOTOREALISM_PERFORMANCE_LIMITS))

    def test_mislabeled_or_edited_release_limits_are_rejected(self):
        for preset, limits in (('high', release_limits_for_preset('photorealism')),
                               ('photorealism', release_limits_for_preset('high'))):
            with self.subTest(preset=preset), self.assertRaises(ValueError):
                check_release_limits(dict(graphics_preset=preset, release_limits=limits))
        limits = release_limits_for_preset('photorealism')
        limits['performance']['max_frame_ms'] = 100
        with self.assertRaises(ValueError):
            check_release_limits(dict(graphics_preset='photorealism', release_limits=limits))
        self.assertEqual(check_release_limits({}, allow_legacy=True), release_limits_for_preset(None))
        with self.assertRaises(ValueError):
            check_release_limits(dict(graphics_preset='photorealism'), allow_legacy=True)

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
