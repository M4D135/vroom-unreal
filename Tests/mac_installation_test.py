"""Portable fixtures for package identity and rollback; never launch Unreal."""
import copy
import contextlib
import io
import json
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
import install_mac
from install_mac import check_validation, copy_validation_report, package_data_paths, sha256
from regression_gates import release_limits_for_preset


class InstallationFixture:
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
                           release_limits=release_limits_for_preset(None),
                           cases=[dict(name=name, exit=0, status='PASS') for name in names], motion=motion,
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


class MacInstallation(InstallationFixture, unittest.TestCase):
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

    def test_new_release_requires_offroad_and_start_checks_without_breaking_old_rollback(self):
        check_validation(self.report, self.bundle)
        self.report['version'] = '0.33.0'
        names = ['offroad-left', 'offroad-right', 'start-sequence', 'start-restart', 'start-pause', 'start-mute',
                 'drift-unskilled', 'collision-rub', 'collision-rear'] + [f'track-audit-{track + 1}' for track in range(6)]
        for name in names:
            with self.assertRaisesRegex(ValueError, 'Missing required'):
                check_validation(self.report, self.bundle)
            self.report['cases'].append(dict(name=name, exit=0, status='PASS'))
        check_validation(self.report, self.bundle)

    def test_034_requires_expert_drift_evidence(self):
        self.test_new_release_requires_offroad_and_start_checks_without_breaking_old_rollback()
        self.report['version'] = '0.34.0'
        with self.assertRaisesRegex(ValueError, 'Missing required'):
            check_validation(self.report, self.bundle)
        for name in ('drift-expert', 'traffic-overtake'):
            with self.assertRaisesRegex(ValueError, 'Missing required'):
                check_validation(self.report, self.bundle)
            self.report['cases'].append(dict(name=name, exit=0, status='PASS'))
        check_validation(self.report, self.bundle)

    def make_photorealism_report(self):
        self.test_034_requires_expert_drift_evidence()
        self.report.update(graphics_preset='photorealism',
                           release_limits=release_limits_for_preset('photorealism'))
        for row in self.report['performance']:
            row['report']['mean_fps'] = 30.0
            frames = row['report']['frame_time']
            frames.update(p99_ms=34.0, max_ms=40.0, over_20_ms_count=frames['samples'], over_40_ms_count=0)

    def test_photorealism_30hz_with_exact_declared_budget_is_accepted(self):
        self.make_photorealism_report()
        check_validation(self.report, self.bundle)

    def test_30hz_metrics_cannot_install_as_a_60hz_profile(self):
        self.make_photorealism_report()
        self.report.update(graphics_preset='high', release_limits=release_limits_for_preset('high'))
        with self.assertRaisesRegex(ValueError, 'Performance release gate failed'):
            check_validation(self.report, self.bundle)

    def test_report_limits_must_match_profile_even_when_measurements_pass(self):
        self.make_photorealism_report()
        for preset in (None, 'performance', 'balanced', 'high', 'ultra'):
            with self.subTest(preset=preset):
                self.report['graphics_preset'] = preset
                with self.assertRaisesRegex(ValueError, 'inconsistent'):
                    check_validation(self.report, self.bundle)
        self.report['graphics_preset'] = 'photorealism'
        self.report['release_limits']['performance']['max_frame_ms'] = 100
        with self.assertRaisesRegex(ValueError, 'inconsistent'):
            check_validation(self.report, self.bundle)

    def test_photorealism_missing_counter_or_excessive_hitches_cannot_install(self):
        self.make_photorealism_report()
        frames = self.report['performance'][-1]['report']['frame_time']
        frames.pop('over_40_ms_count')
        with self.assertRaisesRegex(ValueError, 'Performance release gate failed'):
            check_validation(self.report, self.bundle)
        frames['over_40_ms_count'] = 9
        with self.assertRaisesRegex(ValueError, 'Performance release gate failed'):
            check_validation(self.report, self.bundle)

    def test_missing_limits_only_allowed_for_old_60hz_rollback(self):
        self.report.pop('release_limits')
        check_validation(self.report, self.bundle)
        self.report['version'] = '0.33.0'
        # Missing new functional cases is expected; the old profile itself
        # must not be rejected for lacking the optional historical limits.
        with self.assertRaisesRegex(ValueError, 'Missing required'):
            check_validation(self.report, self.bundle)
        self.report['version'] = '0.34.0'
        with self.assertRaisesRegex(ValueError, 'Release limits'):
            check_validation(self.report, self.bundle)

    def test_old_release_cannot_be_relabelled_photorealism(self):
        self.report.update(graphics_preset='photorealism', release_limits=release_limits_for_preset('photorealism'))
        with self.assertRaisesRegex(ValueError, '0.34'):
            check_validation(self.report, self.bundle)

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


class InstallationEntryPoints(InstallationFixture, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.home = self.release / 'home'
        (self.home / 'Applications').mkdir(parents=True)
        (self.home / 'Desktop').mkdir()
        self.validation = self.release / 'source-results.json'
        self.storage = self.home / 'Library/Application Support/Vroom'
        self.launcher = self.home / 'Applications/Vroom.app'
        self.desktop = self.home / 'Desktop/Vroom.app'
        self.compiler = mock.patch.object(install_mac, 'build_launcher', side_effect=self.build_fixture_launcher)
        self.build = self.compiler.start()
        self.addCleanup(self.compiler.stop)

    def build_fixture_launcher(self, path, version):
        executable = path / 'Contents/MacOS/VroomLauncher'
        executable.parent.mkdir(parents=True)
        executable.write_text(version)
        (path / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleIdentifier': install_mac.LAUNCHER_IDENTIFIER,
        }))

    def install(self, validation=None, bundle=None):
        self.validation.write_text(json.dumps(self.report))
        return install_mac.install_release(validation or self.validation, bundle or self.bundle, self.home)

    def test_desktop_and_applications_follow_the_same_manifest(self):
        manifest = self.install()
        self.assertTrue(self.desktop.is_symlink())
        self.assertEqual(self.desktop.resolve(), self.launcher.resolve())
        self.assertEqual(json.loads((self.storage / 'active.json').read_text()), manifest)
        self.assertEqual(sha256(manifest['validation']), manifest['validation_sha256'])
        check_validation(json.loads(Path(manifest['validation']).read_text()), Path(manifest['bundle']))
        command = self.home / 'Desktop/Vroom.command'
        self.assertTrue(command.stat().st_mode & 0o111)
        self.assertIn('$HOME/Applications/Vroom.app/Contents/MacOS/VroomLauncher', command.read_text())

    def test_previous_build_shortcut_and_command_are_preserved(self):
        old_build = self.home / 'old-checkout/Binaries/Mac/Vroom.app'
        (old_build / 'Contents').mkdir(parents=True)
        (old_build / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleIdentifier': install_mac.GAME_IDENTIFIER,
        }))
        self.desktop.symlink_to(old_build, target_is_directory=True)
        command = self.home / 'Desktop/Vroom.command'
        command.write_text('old command to retain')
        self.install()
        backups = list((self.storage / 'Launchers').iterdir())
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / 'Desktop-Vroom.app').resolve(), old_build.resolve())
        self.assertEqual((backups[0] / 'Desktop-Vroom.command').read_text(), 'old command to retain')
        self.assertTrue(old_build.is_dir())
        self.assertEqual(self.desktop.resolve(), self.launcher.resolve())

    def test_unrelated_apps_are_never_displaced(self):
        for path in [self.launcher, self.desktop]:
            with self.subTest(path=path):
                (path / 'Contents').mkdir(parents=True)
                plist = path / 'Contents/Info.plist'
                plist.write_bytes(plistlib.dumps({'CFBundleIdentifier': 'other.publisher.app'}))
                with self.assertRaisesRegex(ValueError, 'unrelated application'):
                    self.install()
                self.assertEqual(plistlib.loads(plist.read_bytes())['CFBundleIdentifier'], 'other.publisher.app')
                self.assertFalse(self.storage.exists())
                plist.unlink()
                plist.parent.rmdir()
                path.rmdir()
        self.build.assert_not_called()

    def test_incomplete_forced_and_failed_reports_cannot_activate(self):
        for change in [dict(completed=False), dict(forced=True), dict(failures=[{'name': 'soak-track-1'}])]:
            with self.subTest(change=change):
                self.validation.write_text(json.dumps(dict(self.report, **change)))
                with self.assertRaisesRegex(ValueError, 'successful packaged-game validation'):
                    install_mac.install_release(self.validation, self.bundle, self.home)
                self.assertFalse(self.storage.exists())
        self.report['cases'][0]['status'] = 'FAIL'
        with self.assertRaisesRegex(ValueError, 'game case failed'):
            self.install()
        self.build.assert_not_called()
        self.assertFalse(self.storage.exists())

    def test_failed_measured_performance_cannot_activate_even_without_failure_summary(self):
        self.report['performance'][0]['report']['frame_time']['max_ms'] = 46.0
        with self.assertRaisesRegex(ValueError, 'Performance release gate failed'):
            self.install()
        self.assertFalse(self.storage.exists())
        self.build.assert_not_called()

    def test_force_is_not_an_installation_option(self):
        with mock.patch.object(sys, 'argv', ['install_mac.py', '--validation', 'ignored.json', '--force']):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                install_mac.main()
        self.assertEqual(raised.exception.code, 2)
        self.build.assert_not_called()

    def test_rollback_reactivates_validated_release_without_removing_versions(self):
        first = self.install()
        self.report['version'] = '0.30.1'
        executable = self.write('Contents/MacOS/Vroom', b'new executable fixture')
        self.report['executable_sha256'] = sha256(executable)
        second = self.install()
        self.assertNotEqual(first['bundle'], second['bundle'])
        restored = self.install(Path(first['validation']), Path(first['bundle']))
        self.assertEqual(restored, first)
        self.assertEqual(self.desktop.resolve(), self.launcher.resolve())
        self.assertTrue(Path(first['bundle']).exists())
        self.assertTrue(Path(second['bundle']).exists())
        self.assertEqual(json.loads((self.storage / 'active.json').read_text()), first)
        self.assertEqual(len(list((self.storage / 'Releases').iterdir())), 2)
        self.assertTrue(any((p / 'Applications-Vroom.app').exists() for p in (self.storage / 'Launchers').iterdir()))

    def test_failed_manifest_publication_restores_all_entry_points(self):
        first = self.install()
        original_launcher = (self.launcher / 'Contents/MacOS/VroomLauncher').read_bytes()
        original_command = (self.home / 'Desktop/Vroom.command').read_bytes()
        self.report['version'] = '0.30.1'
        executable = self.write('Contents/MacOS/Vroom', b'new executable fixture')
        self.report['executable_sha256'] = sha256(executable)
        original_replace = Path.replace
        def fail_publication(path, target):
            if path.name == 'active.json.tmp':
                raise OSError('simulated atomic publication failure')
            return original_replace(path, target)
        with mock.patch.object(Path, 'replace', fail_publication):
            with self.assertRaisesRegex(OSError, 'publication failure'):
                self.install()
        self.assertEqual(json.loads((self.storage / 'active.json').read_text()), first)
        self.assertEqual(self.desktop.resolve(), self.launcher.resolve())
        self.assertEqual((self.launcher / 'Contents/MacOS/VroomLauncher').read_bytes(), original_launcher)
        self.assertEqual((self.home / 'Desktop/Vroom.command').read_bytes(), original_command)
        self.assertFalse((self.storage / 'active.json.tmp').exists())
        self.assertTrue(Path(first['bundle']).exists())

    def test_reinstall_keeps_active_report_immutable_if_launcher_build_fails(self):
        first = self.install()
        report_path = Path(first['validation'])
        report_bytes = report_path.read_bytes()
        self.report['date'] = 'different successful rerun'
        self.build.side_effect = OSError('simulated compiler failure')
        with self.assertRaisesRegex(OSError, 'compiler failure'):
            self.install()
        self.assertEqual(report_path.read_bytes(), report_bytes)
        self.assertEqual(json.loads((self.storage / 'active.json').read_text()), first)
        self.assertEqual(sha256(report_path), first['validation_sha256'])


if __name__ == '__main__':
    unittest.main()
