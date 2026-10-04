import copy
import json
import sys
import unittest
from pathlib import Path

# Ensure the Tests directory is in sys.path to import the fixture module
sys.path.insert(0, str(Path(__file__).resolve().parent))

import mac_installation_test as fixtures
from unittest import mock
import install_mac
from install_mac import check_validation, sha256


class TargetedFixture(fixtures.InstallationFixture):
    """Strict validation tests for the targeted-gameplay scope (0.35.0+)."""

    def setUp(self):
        super().setUp()
        # Upgrade the base fixture to meet targeted gameplay requirements
        self.full_report = copy.deepcopy(self.report)
        self.report['version'] = '0.35.0'
        self.report['platform'] = 'Mac'
        self.report['scope'] = 'targeted-gameplay'
        self.report['forced'] = False
        self.report['editor'] = False
        self.report['failures'] = []
        self.report['performance_retested'] = False

        # Remove full-gameplay specific keys that must not exist in targeted reports
        for key in ('performance', 'soak_motion', 'motion', 'release_limits'):
            self.report.pop(key, None)

        # Define the exact 11 required scenarios for targeted gameplay
        required_cases = [
            'cockpit-final', 'collision-crash', 'drift-expert', 'drift-keyboard',
            'start-mute', 'start-pause', 'pit-service', 'collision-rub',
            'start-sequence', 'drift-straight', 'traffic-overtake'
        ]

        # Construct valid cases
        self.report['cases'] = [
            {
                'name': name,
                'exit': 0,
                'status': 'PASS',
                'offscreen': False,
                'foreground': True
            } for name in required_cases
        ]

        # Update hashes to reflect current bundle state
        executable = self.bundle / 'Contents/MacOS/Vroom'
        self.report['executable_sha256'] = sha256(executable)
        self.report['package_data_sha256'] = {
            path: sha256(self.bundle / path)
            for path in install_mac.package_data_paths(self.bundle)
        }



class TargetedGameplayValidationTests(TargetedFixture, unittest.TestCase):
    def test_valid_targeted_gameplay_report_is_accepted(self):
        """Baseline: A perfectly formed targeted report passes validation."""
        check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_opt_in_scope_is_mandatory(self):
        """Installing with default 'full' scope must reject a targeted report."""
        with self.assertRaisesRegex(ValueError, 'Report scope differs'):
            check_validation(self.report, self.bundle)

    def test_unknown_scope_is_rejected(self):
        """Invalid scope strings are rejected immediately."""
        with self.assertRaisesRegex(ValueError, 'Unknown validation scope'):
            check_validation(self.report, self.bundle, scope='invalid-scope')

    def test_missing_case_is_rejected(self):
        """Removing one of the 11 required cases fails validation."""
        self.report['cases'].pop()
        with self.assertRaisesRegex(ValueError, 'exactly eleven unique scenarios'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_duplicate_case_is_rejected(self):
        """Duplicating a case name fails the uniqueness check."""
        self.report['cases'].append(copy.deepcopy(self.report['cases'][0]))
        with self.assertRaisesRegex(ValueError, 'exactly eleven unique scenarios'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_invisible_case_is_rejected(self):
        """Cases marked as offscreen are invalid for targeted gameplay."""
        self.report['cases'][0]['offscreen'] = True
        with self.assertRaisesRegex(ValueError, 'must pass visibly'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_failed_case_is_rejected(self):
        """Cases with non-zero exit or non-PASS status are invalid."""
        self.report['cases'][0]['exit'] = 1
        with self.assertRaisesRegex(ValueError, 'must pass visibly'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

        self.report['cases'][0]['exit'] = 0
        self.report['cases'][0]['status'] = 'FAIL'
        with self.assertRaisesRegex(ValueError, 'must pass visibly'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_false_declaration_forced_is_rejected(self):
        """Targeted reports must explicitly declare forced=False."""
        self.report['forced'] = True
        with self.assertRaisesRegex(ValueError, 'successful packaged-game validation'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_false_declaration_editor_is_rejected(self):
        """Targeted reports must explicitly declare editor=False."""
        self.report['editor'] = True
        with self.assertRaisesRegex(ValueError, 'successful packaged-game validation'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_missing_platform_field_is_rejected(self):
        """Targeted gameplay requires explicit Mac platform declaration."""
        self.report.pop('platform')
        with self.assertRaisesRegex(ValueError, 'requires a Mac release'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_wrong_platform_is_rejected(self):
        """Targeted gameplay is Mac-specific."""
        self.report['platform'] = 'Windows'
        with self.assertRaisesRegex(ValueError, 'requires a Mac release'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_version_below_035_is_rejected(self):
        """Targeted gameplay gate requires version >= 0.35.0."""
        self.report['version'] = '0.34.9'
        with self.assertRaisesRegex(ValueError, 'requires a Mac release >= 0.35.0'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_missing_version_field_is_rejected(self):
        """Version is mandatory."""
        self.report.pop('version')
        with self.assertRaisesRegex(ValueError, 'Missing or invalid release version'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_missing_required_fields_in_case_is_rejected(self):
        """Cases must contain exit, status, offscreen, foreground."""
        self.report['cases'][0].pop('foreground')
        with self.assertRaisesRegex(ValueError, 'must pass visibly'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_changed_executable_is_rejected(self):
        """Binary hash mismatch fails validation."""
        self.write('Contents/MacOS/Vroom', b'tampered executable')
        with self.assertRaisesRegex(ValueError, 'Binary differs'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_changed_packaged_data_is_rejected(self):
        """Data hash mismatch fails validation."""
        hud_path = 'Contents/UE/Vroom/Content/Data/hud/panel.json'
        self.write(hud_path, b'{"tampered": true}')
        with self.assertRaisesRegex(ValueError, 'Packaged data differs'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_report_cannot_choose_its_own_scope(self):
        self.report['scope'] = 'full'
        with self.assertRaisesRegex(ValueError, 'Report scope differs'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_duplicate_cannot_replace_a_missing_case(self):
        self.report['cases'][-1] = copy.deepcopy(self.report['cases'][0])
        with self.assertRaisesRegex(ValueError, 'unique scenarios'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_targeted_mode_cannot_hide_performance_measurements(self):
        self.report['performance'] = self.full_report['performance']
        with self.assertRaisesRegex(ValueError, 'performance measurements'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')

    def test_declarations_cannot_be_omitted(self):
        for field in ('forced', 'editor', 'failures', 'performance_retested'):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                report.pop(field)
                with self.assertRaisesRegex(ValueError, 'explicit successful'):
                    check_validation(report, self.bundle, scope='targeted-gameplay')

    def test_visible_background_case_is_not_accepted(self):
        self.report['cases'][0]['foreground'] = False
        with self.assertRaisesRegex(ValueError, 'must pass visibly'):
            check_validation(self.report, self.bundle, scope='targeted-gameplay')


class TargetedInstallationLifecycleTests(TargetedFixture, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.home = self.release / 'home'
        (self.home / 'Applications').mkdir(parents=True)
        (self.home / 'Desktop').mkdir()
        self.storage = self.home / 'Library/Application Support/Vroom'
        self.launcher = self.home / 'Applications/Vroom.app'
        self.desktop = self.home / 'Desktop/Vroom.app'
        patcher = mock.patch.object(install_mac, 'build_launcher', side_effect=lambda path, version:
            fixtures.InstallationEntryPoints.build_fixture_launcher(self, path, version))
        self.build = patcher.start()
        self.addCleanup(patcher.stop)

    def install(self, report=None, scope='targeted-gameplay'):
        report = self.report if report is None else report
        validation = self.release / 'input.json'
        validation.write_text(json.dumps(report))
        return install_mac.install_release(validation, self.bundle, self.home, scope=scope)

    def test_install_rollback_and_reinstall_preserve_both_artifacts(self):
        targeted = self.install()
        full = self.install(self.full_report, scope='full')
        self.assertEqual(full['version'], '0.30.0')
        self.assertEqual(full['validation_scope'], 'full')
        for manifest in (targeted, full):
            bundle = Path(manifest['bundle'])
            report = json.loads(Path(manifest['validation']).read_text())
            check_validation(report, bundle, scope=manifest['validation_scope'])
        self.assertEqual(self.desktop.resolve(), self.launcher.resolve())
        self.assertEqual((self.launcher / 'Contents/MacOS/VroomLauncher').read_text(), '0.30.0')
        self.assertEqual(self.install(), targeted)
        self.assertEqual(json.loads((self.storage / 'active.json').read_text()), targeted)
        self.assertTrue(Path(full['bundle']).exists())

    def test_publication_failure_restores_shortcuts_and_manifest(self):
        first = self.install()
        old_launcher = (self.launcher / 'Contents/MacOS/VroomLauncher').read_bytes()
        old_command = (self.home / 'Desktop/Vroom.command').read_bytes()
        self.report['version'] = '0.35.1'
        self.write('Contents/MacOS/Vroom', b'new targeted executable')
        self.report['executable_sha256'] = sha256(self.bundle / 'Contents/MacOS/Vroom')
        original_replace = Path.replace
        def fail_publication(path, target):
            if path.name == 'active.json.tmp':
                raise OSError('simulated publication failure')
            return original_replace(path, target)
        with mock.patch.object(Path, 'replace', fail_publication):
            with self.assertRaisesRegex(OSError, 'publication failure'):
                self.install()
        self.assertEqual(json.loads((self.storage / 'active.json').read_text()), first)
        self.assertEqual(self.desktop.resolve(), self.launcher.resolve())
        self.assertEqual((self.launcher / 'Contents/MacOS/VroomLauncher').read_bytes(), old_launcher)
        self.assertEqual((self.home / 'Desktop/Vroom.command').read_bytes(), old_command)
        self.assertFalse((self.storage / 'active.json.tmp').exists())
        self.assertTrue(Path(first['bundle']).exists())


if __name__ == '__main__':
    unittest.main()
