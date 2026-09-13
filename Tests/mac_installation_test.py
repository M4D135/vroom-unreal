"""Portable fixtures for package identity and rollback; never launch Unreal."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
from install_mac import check_validation, copy_validation_report, package_data_paths, sha256


class MacInstallation(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='vroom-install-test-')
        self.addCleanup(self.temporary.cleanup)
        self.release = Path(self.temporary.name)
        self.bundle = self.release / 'Vroom.app'
        executable = self.write('Contents/MacOS/Vroom', b'executable fixture')
        self.write('Contents/UE/Vroom/Content/Paks/Vroom.ucas', b'cooked fixture')
        self.hud = self.write('Contents/UE/Vroom/Content/Data/hud/panel.json', b'{}')
        motion = dict(analyzed_samples=7200, all_analyzed_telemetry_valid=True,
                      distance_backsteps_count=0, yaw_delta_degrees=dict(max_absolute=1.0),
                      yaw_acceleration_degrees_per_second_squared=dict(max_absolute=100.0))
        def performance(frames, track, resolution=(1440, 900)):
            name = f'soak-track-{track + 1}' if track is not None else f'perf-{resolution[0]}'
            return dict(name=name, resolution=list(resolution), track=track, expected_frames=frames,
                        report=dict(numeric_frame_rows=frames, invalid_frames_after_warmup=0,
                                    mean_fps=60.0, frame_time=dict(p99_ms=17.0, max_ms=20.0,
                                    over_20_ms_count=0, samples=frames - 120)))
        names = ('neutral', 'left', 'right', 'options', 'collision-slow', 'collision-crash',
                 'collision-tyre', 'collision-scenery', 'progress-data', 'menu-flow',
                 'pit-service', 'drift-feedback')
        self.report = dict(completed=True, editor=False, version='0.30.0',
                           cases=[dict(name=name, exit=0) for name in names], motion=motion,
                           soak_motion=[dict(track=track, complete=True, report=motion) for track in range(6)],
                           performance=[performance(7800, track) for track in range(6)] +
                                       [performance(1800, None), performance(1800, None, (2560, 1664))],
                           executable_sha256=sha256(executable),
                           package_data_sha256={path: sha256(self.bundle / path)
                                                for path in package_data_paths(self.bundle)})

    def write(self, relative, content):
        path = self.bundle / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def test_unchanged_package_including_nested_data_is_accepted(self):
        self.assertIn(str(self.hud.relative_to(self.bundle)), self.report['package_data_sha256'])
        check_validation(self.report, self.bundle)

    def test_added_pak_or_nested_data_is_rejected(self):
        for relative in ('Contents/UE/Vroom/Content/Paks/override.pak',
                         'Contents/UE/Vroom/Content/Data/hud/new-panel.json'):
            with self.subTest(relative=relative):
                added = self.write(relative, b'added after validation')
                with self.assertRaisesRegex(ValueError, 'file list differs'):
                    check_validation(self.report, self.bundle)
                added.unlink()

    def test_removed_data_is_rejected(self):
        self.hud.unlink()
        with self.assertRaisesRegex(ValueError, 'file list differs'):
            check_validation(self.report, self.bundle)

    def test_changed_data_still_fails_its_digest(self):
        self.hud.write_bytes(b'{"changed": true}')
        with self.assertRaisesRegex(ValueError, 'Packaged data differs'):
            check_validation(self.report, self.bundle)

    def test_rollback_keeps_its_existing_report(self):
        stored = self.release / 'validation.json'
        stored.write_text(json.dumps(self.report))
        original = stored.read_bytes()
        copy_validation_report(stored, self.release / '.' / 'validation.json')
        self.assertEqual(stored.read_bytes(), original)
        check_validation(json.loads(stored.read_text()), self.bundle)

    def test_report_is_copied_for_a_new_installation(self):
        source = self.release / 'source-results.json'
        destination = self.release / 'validation.json'
        source.write_text(json.dumps(self.report))
        copy_validation_report(source, destination)
        self.assertEqual(destination.read_bytes(), source.read_bytes())

    def test_duplicate_profiles_cannot_replace_required_resolution(self):
        self.report['performance'][-1] = copy.deepcopy(self.report['performance'][-2])
        with self.assertRaisesRegex(ValueError, 'Duplicate performance'):
            check_validation(self.report, self.bundle)

    def test_expected_profile_names_tracks_and_resolutions_are_required(self):
        for index, field, value in [(7, 'name', 'perf-other'), (7, 'resolution', [1440, 900]),
                                    (0, 'track', 1), (0, 'resolution', [2560, 1664])]:
            with self.subTest(index=index, field=field):
                report = copy.deepcopy(self.report)
                report['performance'][index][field] = value
                with self.assertRaisesRegex(ValueError, 'required performance profile'):
                    check_validation(report, self.bundle)

    def test_resolution_profiles_require_exactly_1800_frames(self):
        row = self.report['performance'][-1]
        row['expected_frames'] = row['report']['numeric_frame_rows'] = 3600
        with self.assertRaisesRegex(ValueError, 'required performance profile'):
            check_validation(self.report, self.bundle)

    def test_soak_profiles_require_at_least_7800_frames(self):
        row = self.report['performance'][0]
        row['expected_frames'] = row['report']['numeric_frame_rows'] = 7799
        with self.assertRaisesRegex(ValueError, 'required performance profile'):
            check_validation(self.report, self.bundle)
        row['expected_frames'] = row['report']['numeric_frame_rows'] = 8000
        check_validation(self.report, self.bundle)


if __name__ == '__main__':
    unittest.main()
