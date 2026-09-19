"""Portable resume fixtures: never launch Unreal, compile code or access a GPU."""
import copy
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
import verify_mac
from install_mac import sha256
from regression_gates import MOTION_LIMITS, PERFORMANCE_LIMITS


def identity():
    return dict(version='0.30.0', editor=False, executable_sha256='a' * 64,
                package_data_sha256={
                    'Contents/UE/Vroom/Content/Paks/Vroom.ucas': 'b' * 64,
                    'Contents/UE/Vroom/Content/Paks/Vroom.utoc': 'c' * 64,
                    'Contents/UE/Vroom/Content/Data/hud/panel.json': 'd' * 64},
                release_limits=dict(performance=copy.deepcopy(PERFORMANCE_LIMITS),
                                    motion=copy.deepcopy(MOTION_LIMITS)))


def png_bytes(width, height):
    def chunk(kind, body):
        return (struct.pack('>I', len(body)) + kind + body
                + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff))
    scanline = b'\0' + bytes(width * 4)
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(scanline * height))
            + chunk(b'IEND', b''))


class ResumeIdentityTests(unittest.TestCase):
    def setUp(self):
        self.current = identity()
        self.previous = copy.deepcopy(self.current)

    def assert_rejected(self):
        with self.assertRaises(ValueError):
            verify_mac.validate_resume_identity(self.previous, self.current)

    def test_same_identity_allows_an_unfinished_run(self):
        self.previous.update(completed=False, cases=[], failures=[{'name': 'left'}])
        verify_mac.validate_resume_identity(self.previous, self.current)

    def test_different_graphics_profiles_cannot_reuse_validation(self):
        self.previous['graphics_preset'] = 'performance'
        self.current['graphics_preset'] = 'high'
        self.assert_rejected()

    def test_explicit_profile_cannot_reuse_implicit_player_settings(self):
        self.current['graphics_preset'] = 'performance'
        self.assert_rejected()

    def test_version_change_rejects_resume(self):
        self.previous['version'] = '0.29.0'
        self.assert_rejected()

    def test_editor_run_is_rejected_on_either_side(self):
        for side in ('previous', 'current'):
            with self.subTest(side=side):
                getattr(self, side)['editor'] = True
                self.assert_rejected()
                getattr(self, side)['editor'] = False

    def test_missing_or_empty_executable_fingerprint_is_rejected(self):
        for side in ('previous', 'current'):
            for value in (None, ''):
                with self.subTest(side=side, value=value):
                    report = getattr(self, side)
                    original = report.pop('executable_sha256')
                    if value is not None:
                        report['executable_sha256'] = value
                    self.assert_rejected()
                    report['executable_sha256'] = original

    def test_matching_missing_executable_fingerprints_are_not_identity(self):
        self.previous.pop('executable_sha256')
        self.current.pop('executable_sha256')
        self.assert_rejected()

    def test_changed_executable_is_rejected(self):
        self.previous['executable_sha256'] = 'e' * 64
        self.assert_rejected()

    def test_missing_or_empty_package_fingerprints_are_rejected(self):
        for side in ('previous', 'current'):
            for value in (None, {}):
                with self.subTest(side=side, value=value):
                    report = getattr(self, side)
                    original = report.pop('package_data_sha256')
                    if value is not None:
                        report['package_data_sha256'] = value
                    self.assert_rejected()
                    report['package_data_sha256'] = original

    def test_matching_empty_package_maps_are_not_identity(self):
        self.previous['package_data_sha256'] = {}
        self.current['package_data_sha256'] = {}
        self.assert_rejected()

    def test_incomplete_or_changed_package_digest_is_rejected(self):
        relative = 'Contents/UE/Vroom/Content/Paks/Vroom.ucas'
        for value in ('', None, 'f' * 64):
            with self.subTest(value=value):
                self.previous['package_data_sha256'][relative] = value
                self.assert_rejected()

    def test_matching_incomplete_package_fingerprints_are_rejected(self):
        relative = 'Contents/UE/Vroom/Content/Paks/Vroom.ucas'
        self.previous['package_data_sha256'][relative] = ''
        self.current['package_data_sha256'][relative] = ''
        self.assert_rejected()

    def test_added_or_removed_package_data_invalidates_resume(self):
        for side in ('previous', 'current'):
            with self.subTest(side=side):
                report = getattr(self, side)
                removed = report['package_data_sha256'].pop(
                    'Contents/UE/Vroom/Content/Data/hud/panel.json')
                self.assert_rejected()
                report['package_data_sha256'][
                    'Contents/UE/Vroom/Content/Data/hud/panel.json'] = removed

    def test_changed_release_limits_are_rejected(self):
        for category, field in (('performance', 'max_frame_ms'),
                                ('motion', 'max_yaw_delta_degrees')):
            with self.subTest(category=category):
                self.previous = copy.deepcopy(self.current)
                self.previous['release_limits'][category][field] += 1
                self.assert_rejected()

    def test_missing_release_limits_are_rejected(self):
        self.previous.pop('release_limits')
        self.assert_rejected()


class ResumeCaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='vroom-resume-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.previous = identity()
        self.previous.update(completed=False, cases=[])

    def make_case(self, name='neutral', *, legacy=False, capture=False, visible=False,
                  flags=None, seconds=5, resolution=(1440, 900), capture_after=3):
        request = dict(flags=list(flags or ['-VroomAutoDemo', '-VroomStartSpeed=0']),
                       seconds=seconds, capture=capture,
                       resolution=list(resolution), capture_after=capture_after)
        if visible:
            request.update(visible=True, offscreen=False)
        relative = Path('Saved/Validation') / (name + '.log')
        log = self.root / relative
        log.parent.mkdir(parents=True, exist_ok=True)
        png = log.with_suffix('.png')
        arguments = ['-LLM', '-windowed', '-ForceRes', f'-ResX={resolution[0]}',
                     f'-ResY={resolution[1]}', '-VroomVerify',
                     f'-abslog={log}', f'-VroomExitAfter={seconds}']
        if capture:
            if not visible:
                arguments += ['-RenderOffScreen', '-MetalOffscreenOnly', '-unattended']
            arguments += [f'-VroomCapture={png}', f'-VroomCaptureAfter={capture_after}']
        arguments += request['flags']
        def log_argument(argument):
            if ' ' in argument:
                key, value = argument.split('=', 1)
                return key + '="' + value + '"'
            return argument
        check = 'VROOM: Verification PASS scenario=fixture'
        log.write_text('LogInit: Command Line: ' + ' '.join(map(log_argument, arguments))
                       + '\nLogTemp: Display: VROOM: Version 0.30.0\n'
                       + 'LogTemp: Display: ' + check + '\n')
        row = dict(name=name, exit=0, checks=[check], log=str(relative), offscreen=capture and not visible)
        if not legacy:
            row.update(status='PASS', request=copy.deepcopy(request))
        if visible or name.startswith(('perf-', 'soak-')) or name == 'motion':
            status = 'foreground_before=0 foreground_after=1'
            row['foreground'] = (dict(exit=0, status=status) if legacy
                                 else dict(exit=0, stdout=status, stderr=''))
        if capture:
            png.write_bytes(png_bytes(*resolution))
            row.update(capture=str(png.relative_to(self.root)), size=list(resolution))
        self.previous['cases'].append(row)
        return row, request, log

    def reusable(self, row, request):
        return verify_mac.case_reusable(self.previous, row['name'], request, self.root)

    def test_successful_legacy_and_new_rows_are_reusable(self):
        for legacy in (True, False):
            with self.subTest(legacy=legacy):
                self.previous['cases'].clear()
                row, request, _ = self.make_case(legacy=legacy)
                self.assertEqual(self.reusable(row, request), row)

    def test_visible_capture_with_active_foreground_is_reusable(self):
        row, request, log = self.make_case(capture=True, visible=True)
        self.assertFalse(row['offscreen'])
        self.assertTrue(request['visible'])
        self.assertNotIn('-RenderOffScreen', log.read_text())
        self.assertEqual(self.reusable(row, request), row)

    def test_visible_capture_requires_successful_foreground_evidence(self):
        row, request, _ = self.make_case(capture=True, visible=True)
        for evidence in (None, {}, dict(exit=4, stdout='foreground_after=0', stderr='')):
            with self.subTest(evidence=evidence):
                row.pop('foreground', None)
                if evidence is not None:
                    row['foreground'] = evidence
                self.assertIsNone(self.reusable(row, request))

    def test_offscreen_capture_cannot_be_relabelled_visible(self):
        row, request, log = self.make_case(capture=True, visible=True)
        row['offscreen'] = True
        self.assertIsNone(self.reusable(row, request))
        row['offscreen'] = False
        original = log.read_text()
        # Recheck the command actually executed, not only mutable report labels.
        for flag in ('-RenderOffScreen', '-MetalOffscreenOnly'):
            with self.subTest(flag=flag):
                log.write_text(original.replace('LogInit: Command Line: ',
                                                f'LogInit: Command Line: {flag} '))
                self.assertIsNone(self.reusable(row, request))

    def test_missing_case_is_not_reusable(self):
        _, request, _ = self.make_case()
        self.assertIsNone(verify_mac.case_reusable(self.previous, 'absent', request, self.root))

    def test_duplicate_case_rows_are_not_unambiguous_evidence(self):
        row, request, _ = self.make_case()
        self.previous['cases'].append(copy.deepcopy(row))
        self.assertIsNone(self.reusable(row, request))

    def test_explicit_failure_and_incomplete_status_are_not_reusable(self):
        for status in ('FAIL', 'RUNNING', 'PENDING'):
            with self.subTest(status=status):
                self.previous['cases'].clear()
                row, request, _ = self.make_case()
                row['status'] = status
                self.assertIsNone(self.reusable(row, request))

    def test_nonzero_or_missing_exit_is_not_reusable(self):
        for value in (1, None):
            with self.subTest(exit=value):
                self.previous['cases'].clear()
                row, request, _ = self.make_case()
                row.pop('exit')
                if value is not None:
                    row['exit'] = value
                self.assertIsNone(self.reusable(row, request))

    def test_failed_case_or_its_subgate_is_not_reusable(self):
        row, request, _ = self.make_case()
        self.assertEqual(self.reusable(row, request), row)
        for name in ('neutral', 'neutral-panel', 'neutral-foreground'):
            with self.subTest(failure=name):
                self.previous['failures'] = [dict(name=name, reasons=['fixture failure'])]
                self.assertIsNone(self.reusable(row, request))

    def test_failure_of_another_case_does_not_discard_good_evidence(self):
        row, request, _ = self.make_case()
        self.previous['failures'] = [dict(name='neutralized', reasons=['other case'])]
        self.assertEqual(self.reusable(row, request), row)

    def test_missing_or_empty_log_is_not_reusable(self):
        row, request, log = self.make_case()
        log.write_text('')
        self.assertIsNone(self.reusable(row, request))
        log.unlink()
        self.assertIsNone(self.reusable(row, request))

    def test_log_outside_validation_directory_is_not_reusable(self):
        row, request, log = self.make_case()
        outside = self.root / 'unpreserved.log'
        outside.write_bytes(log.read_bytes())
        row['log'] = str(outside)
        self.assertIsNone(self.reusable(row, request))

    def test_saved_log_fingerprint_is_rechecked(self):
        row, request, log = self.make_case()
        row['evidence_sha256'] = dict(log=sha256(log))
        self.assertEqual(self.reusable(row, request), row)
        log.write_text(log.read_text() + 'Modified after validation\n')
        self.assertIsNone(self.reusable(row, request))

    def test_invalid_log_evidence_is_not_reusable(self):
        row, request, log = self.make_case()
        original = log.read_text()
        for body in (original.replace('Version 0.30.0', 'Version 0.29.0'),
                     original.replace('VROOM: Version 0.30.0', 'no version'),
                     original + 'LogMaterial: Error: material fixture failed\n',
                     original + 'Failed to compile Material. Using Default Material\n',
                     original + 'VROOM: Verification FAIL scenario=fixture\n'):
            with self.subTest(body=body.splitlines()[-1]):
                log.write_text(body)
                self.assertIsNone(self.reusable(row, request))

    def test_failed_or_missing_verification_checks_are_not_reusable(self):
        row, request, _ = self.make_case()
        for checks in ([], ['VROOM: Verification FAIL scenario=fixture']):
            with self.subTest(checks=checks):
                row['checks'] = checks
                self.assertIsNone(self.reusable(row, request))

    def test_unverified_csv_exit_case_requires_csv_exit_flag(self):
        row, request, log = self.make_case('perf-1440', flags=[
            '-VroomAutoDemo', '-csvCaptureFrames=1800', '-csvGpuStats',
            '-ExitAfterCsvProfiling', '-LogCmds=LogPSOHitching Verbose'])
        row['checks'] = []
        log.write_text(log.read_text().replace(
            'LogTemp: Display: VROOM: Verification PASS scenario=fixture\n', ''))
        self.assertEqual(self.reusable(row, request), row)

    def test_changed_requested_arguments_are_not_reusable(self):
        row, request, _ = self.make_case()
        for field, value in (('flags', ['-VroomAutoDemo', '-VroomStartSpeed=30']),
                             ('seconds', 9), ('capture', True),
                             ('resolution', [1600, 900])):
            with self.subTest(field=field):
                changed = copy.deepcopy(request)
                changed[field] = value
                self.assertIsNone(self.reusable(row, changed))

    def test_changed_capture_timing_is_not_reusable(self):
        row, request, _ = self.make_case(capture=True)
        request['capture_after'] = 1.15
        self.assertIsNone(self.reusable(row, request))

    def test_legacy_runtime_arguments_must_match(self):
        for argument, replacement in (('-VroomStartSpeed=0', '-VroomStartSpeed=30'),
                                       ('-ResX=1440', '-ResX=1600'),
                                       ('-VroomExitAfter=5', '-VroomExitAfter=9')):
            with self.subTest(argument=argument):
                self.previous['cases'].clear()
                row, request, log = self.make_case(legacy=True)
                log.write_text(log.read_text().replace(argument, replacement))
                self.assertIsNone(self.reusable(row, request))

    def test_missing_command_line_cannot_prove_legacy_arguments(self):
        row, request, log = self.make_case(legacy=True)
        log.write_text('\n'.join(log.read_text().splitlines()[1:]) + '\n')
        self.assertIsNone(self.reusable(row, request))

    def test_requested_flags_can_be_reordered_without_changing_meaning(self):
        row, request, _ = self.make_case(legacy=True)
        request['flags'].reverse()
        self.assertEqual(self.reusable(row, request), row)

    def test_runtime_offscreen_state_cannot_substitute_for_onscreen(self):
        row, request, _ = self.make_case()
        row['offscreen'] = True
        self.assertIsNone(self.reusable(row, request))

    def test_valid_png_capture_is_reusable_for_legacy_and_new_rows(self):
        for legacy in (True, False):
            with self.subTest(legacy=legacy):
                self.previous['cases'].clear()
                row, request, _ = self.make_case(legacy=legacy, capture=True)
                self.assertEqual(self.reusable(row, request), row)

    def test_missing_corrupt_or_wrong_size_png_is_not_reusable(self):
        row, request, _ = self.make_case(capture=True)
        png = self.root / row['capture']
        for body in (b'not a PNG', png_bytes(1600, 900)):
            with self.subTest(proof=len(body)):
                png.write_bytes(body)
                self.assertIsNone(self.reusable(row, request))
        png.unlink()
        self.assertIsNone(self.reusable(row, request))

    def test_incomplete_capture_record_is_not_reusable(self):
        for field in ('capture', 'size'):
            with self.subTest(field=field):
                self.previous['cases'].clear()
                row, request, _ = self.make_case(capture=True)
                row.pop(field)
                self.assertIsNone(self.reusable(row, request))

    def test_foreground_evidence_is_required_for_performance_soak_and_motion(self):
        for name in ('perf-1440', 'soak-track-1', 'motion'):
            with self.subTest(name=name):
                row, request, _ = self.make_case(name)
                self.assertEqual(self.reusable(row, request), row)
                row.pop('foreground')
                self.assertIsNone(self.reusable(row, request))
                row['foreground'] = dict(exit=4, stdout='foreground_after=0', stderr='')
                self.assertIsNone(self.reusable(row, request))

    def test_legacy_foreground_record_is_reusable(self):
        row, request, _ = self.make_case('motion', legacy=True)
        self.assertEqual(self.reusable(row, request), row)

    def test_explicit_locked_session_allows_onscreen_profile_resume(self):
        row, request, _ = self.make_case('perf-1440')
        row['foreground'] = dict(exit=5, locked=True,
                                 stdout='session_locked=1', stderr='',
                                 display_mode='onscreen_locked')
        self.assertEqual(self.reusable(row, request), row)
        for change in (dict(locked=False), dict(stdout=''), dict(exit=4)):
            with self.subTest(change=change):
                original = copy.deepcopy(row['foreground'])
                row['foreground'].update(change)
                self.assertIsNone(self.reusable(row, request))
                row['foreground'] = original

    def test_named_self_checks_must_be_present_in_the_log(self):
        markers = {'progress-data': ['Progress self-test PASS'],
                   'menu-flow': ['Menu self-test PASS', 'Settings self-test PASS'],
                   'drift-feedback': ['Drift feedback PASS'],
                   'options': ['Graphics preview PASS']}
        for name, expected in markers.items():
            with self.subTest(name=name):
                row, request, log = self.make_case(name)
                original = log.read_text()
                self.assertIsNone(self.reusable(row, request))
                log.write_text(original + '\n'.join(expected) + '\n')
                self.assertEqual(self.reusable(row, request), row)
                for missing in expected:
                    with self.subTest(missing=missing):
                        log.write_text(original + '\n'.join(
                            marker for marker in expected if marker != missing) + '\n')
                        self.assertIsNone(self.reusable(row, request))


class ForegroundEvidenceTests(unittest.TestCase):
    def test_successful_activation_is_allowed(self):
        for activation in (dict(exit=0),
                           dict(exit=0, stdout='foreground_after=1', stderr='', locked=False)):
            with self.subTest(activation=activation):
                self.assertTrue(verify_mac.foreground_allowed(activation))

    def test_lock_requires_exit_five_boolean_and_bounded_stdout_marker(self):
        for marker in ('locked=1', 'session_locked=1',
                       'foreground_before=0 session_locked=1 foreground_after=0',
                       'status\nlocked=1\n'):
            with self.subTest(marker=marker):
                activation = dict(exit=5, locked=True, stdout=marker, stderr='')
                self.assertTrue(verify_mac.foreground_allowed(activation))

    def test_non_authoritative_lock_strings_are_rejected(self):
        for stdout in ('', 'locked=0', 'session_locked=0', 'unlocked=1',
                       'not_locked=1', 'locked=10', 'session_locked=10',
                       'session_locked=1unexpected'):
            with self.subTest(stdout=stdout):
                self.assertFalse(verify_mac.foreground_allowed(
                    dict(exit=5, locked=True, stdout=stdout, stderr='')))

    def test_lock_cannot_be_inferred_from_error_code_or_stderr(self):
        for activation in (dict(exit=5), dict(exit=5, locked=True),
                           dict(exit=5, locked=False, stdout='session_locked=1'),
                           dict(exit=5, stdout='session_locked=1'),
                           dict(exit=5, locked=True, stdout='', stderr='session_locked=1'),
                           dict(exit=4, locked=True, stdout='session_locked=1')):
            with self.subTest(activation=activation):
                self.assertFalse(verify_mac.foreground_allowed(activation))

    def test_normalization_preserves_activation_proof_and_marks_active_window(self):
        result = verify_mac.normalize_foreground(0, 'foreground_after=1\n', '')
        self.assertEqual(result['exit'], 0)
        self.assertEqual(result['stdout'].strip(), 'foreground_after=1')
        self.assertEqual(result['stderr'], '')
        self.assertIs(result['locked'], False)
        self.assertEqual(result['display_mode'], 'onscreen_active')
        self.assertTrue(verify_mac.foreground_allowed(result))

    def test_normalization_labels_only_proven_lock_as_onscreen_locked(self):
        for marker in ('session_locked=1', 'locked=1'):
            with self.subTest(marker=marker):
                result = verify_mac.normalize_foreground(5, marker, 'fixture diagnostic')
                self.assertEqual(result['exit'], 5)
                self.assertEqual(result['stdout'], marker)
                self.assertEqual(result['stderr'], 'fixture diagnostic')
                self.assertIs(result['locked'], True)
                self.assertEqual(result['display_mode'], 'onscreen_locked')
                self.assertTrue(verify_mac.foreground_allowed(result))

    def test_normalization_preserves_unconfirmed_failures(self):
        for code, stdout, stderr in ((5, '', ''), (5, 'unlocked=1', ''),
                                     (5, '', 'session_locked=1'),
                                     (4, 'session_locked=1', 'activation failed')):
            with self.subTest(code=code, stdout=stdout, stderr=stderr):
                result = verify_mac.normalize_foreground(code, stdout, stderr)
                self.assertEqual(result['display_mode'], 'onscreen_unconfirmed')
                self.assertFalse(verify_mac.foreground_allowed(result))


class ResumeProfileTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='vroom-resume-profile-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        directory = self.root / 'Saved/Validation'
        directory.mkdir(parents=True)
        self.csv = directory / 'profile.csv'
        self.csv.write_text('fixture: parsed only by the mocked analyzer\n')
        self.previous = identity()
        self.row = dict(name='perf-1440', expected_frames=1800,
                        resolution=[1440, 900], distance=3295, track=None,
                        report=dict(source=dict(path=str(self.csv.relative_to(self.root)))))
        self.previous['performance'] = [self.row]
        for name, result in (('analyze_csv', {'fixture': 'performance'}),
                             ('performance_failures', []),
                             ('analyze_motion', {'fixture': 'motion'}),
                             ('motion_failures', [])):
            patcher = mock.patch.object(verify_mac, name, return_value=result)
            setattr(self, name, patcher.start())
            self.addCleanup(patcher.stop)

    def reusable(self):
        return verify_mac.profile_reusable(
            self.previous, self.row['name'], self.row['expected_frames'],
            self.row['resolution'], self.row['distance'], self.row['track'], self.root)

    def test_missing_csv_cannot_reuse_even_a_successful_saved_report(self):
        self.assertEqual(self.reusable(), self.csv.resolve())
        self.csv.unlink()
        self.assertIsNone(self.reusable())

    def test_reanalyzed_performance_failure_rejects_saved_success(self):
        self.assertEqual(self.reusable(), self.csv.resolve())
        self.performance_failures.return_value = ['Frame p99 exceeds release limit']
        self.assertIsNone(self.reusable())

    def test_soak_requires_preserved_motion_and_explicit_lap_completion(self):
        self.row.update(name='soak-track-1', expected_frames=7800, distance=0, track=0)
        log = self.csv.with_suffix('.log')
        log.write_text('VROOM-TOUR track=0 distanceCovered=4000 complete=1\n')
        motion = dict(track=0, complete=True,
                      report=dict(source=dict(path=str(log.relative_to(self.root)))))
        self.previous['soak_motion'] = [motion]
        self.assertEqual(self.reusable(), self.csv.resolve())
        for missing in ('motion', 'complete', 'log', 'lap-marker'):
            with self.subTest(missing=missing):
                self.previous['soak_motion'] = [copy.deepcopy(motion)]
                log.write_text('VROOM-TOUR track=0 distanceCovered=4000 complete=1\n')
                if missing == 'motion':
                    self.previous['soak_motion'] = []
                elif missing == 'complete':
                    self.previous['soak_motion'][0].pop('complete')
                elif missing == 'log':
                    log.unlink()
                else:
                    log.write_text('VROOM-TOUR track=0 distanceCovered=3999 complete=0\n')
                self.assertIsNone(self.reusable())


class ResumeMainEvidenceTests(unittest.TestCase):
    def test_activation_failure_is_preserved_before_main_raises(self):
        with tempfile.TemporaryDirectory(prefix='vroom-resume-main-test-') as temporary:
            root = Path(temporary) / 'project'
            container = Path(temporary) / 'container'
            ini = root / 'Config/DefaultGame.ini'
            ini.parent.mkdir(parents=True)
            ini.write_text('ProjectVersion=0.30.0\n')
            bundle = root / 'Saved/Packages/Mac/Vroom.app'
            executable = bundle / 'Contents/MacOS/Vroom'
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b'fixture: not an executable')
            data = bundle / 'Contents/UE/Vroom/Content/Paks/Vroom.ucas'
            data.parent.mkdir(parents=True)
            data.write_bytes(b'fixture: not cooked game data')

            def fake_child(command, **kwargs):
                values = dict(argument.split('=', 1)
                              for argument in command[1:] if '=' in argument)
                log = Path(values['-abslog'])
                start_events = ''
                if '-VroomStartTest' in command:
                    def countdown(signals=(0, 1, 2)):
                        clocks, columns = (-2.992, -1.992, .008), (2, 4, 5)
                        return ''.join(
                            f'VROOM: Start lights columns={columns[signal]} raceClock={clocks[signal]:.3f} signal={signal}\n'
                            f'VROOM: Start beep signal={signal} frequency=880 duration=.20 raceClock={clocks[signal]:.3f}\n'
                            for signal in signals)
                    scenario = values.get('-VroomStartScenario')
                    if scenario == 'restart':
                        start_events += countdown((0, 1)) + 'VROOM: Start test restarted during countdown\n'
                    start_events += countdown()
                    if scenario == 'pause':
                        start_events += 'VROOM: Start test resumed after pause\n'
                log.write_text('LogInit: Command Line: ' + ' '.join(command[1:]) + '\n'
                               'LogTemp: Display: VROOM: Version 0.30.0\n'
                               'LogTemp: Display: VROOM: Verification PASS scenario=fixture\n'
                               'Progress self-test PASS\nMenu self-test PASS\n'
                               'Settings self-test PASS\nDrift feedback PASS\nDrift skill denied PASS\n'
                               'Controlled landing quality=1.0\n'
                               'Graphics preview PASS\nOffroad restart PASS\n'
                               'Start integration PASS\nTrack geometry PASS\n' + start_events)
                kwargs['stdout'].write('fake child stdout preserved\n')
                if '-VroomCapture' in values:
                    Path(values['-VroomCapture']).write_bytes(png_bytes(
                        int(values['-ResX']), int(values['-ResY'])))
                child = mock.Mock(pid=4242, returncode=0)
                child.wait.return_value = 0
                child.poll.return_value = 0
                return child

            activation_stdout = 'foreground_before=0 foreground_after=0\n'
            activation_stderr = 'activation denied fixture\n'

            def fake_run(command, **kwargs):
                if command[0] == 'xcrun':
                    return verify_mac.subprocess.CompletedProcess(command, 0, '', '')
                self.assertEqual(Path(command[0]).name, 'activate-mac')
                return verify_mac.subprocess.CompletedProcess(
                    command, 4, activation_stdout, activation_stderr)

            with mock.patch.object(verify_mac, 'ROOT', root), \
                    mock.patch.object(verify_mac, 'CONTAINER', container), \
                    mock.patch.object(sys, 'argv', ['verify_mac.py', '--profile']), \
                    mock.patch.object(verify_mac.subprocess, 'Popen', side_effect=fake_child), \
                    mock.patch.object(verify_mac.subprocess, 'run', side_effect=fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(RuntimeError, 'perf-1440-foreground'):
                    verify_mac.main()

            reports = list((root / 'Saved/Validation').rglob('results.json'))
            self.assertEqual(len(reports), 1)
            summary = json.loads(reports[0].read_text())
            self.assertIs(summary['completed'], False)
            rows = [row for row in summary['cases'] if row['name'] == 'perf-1440']
            self.assertEqual(len(rows), 1)
            row = rows[0]
            self.assertEqual(row['status'], 'FAIL')
            self.assertEqual(row['exit'], 0)
            self.assertIs(row['offscreen'], False)
            self.assertEqual(row['foreground']['exit'], 4)
            self.assertEqual(row['foreground']['stdout'], activation_stdout)
            self.assertEqual(row['foreground']['stderr'], activation_stderr)
            self.assertEqual(row['foreground']['display_mode'], 'onscreen_unconfirmed')
            self.assertTrue((root / row['log']).is_file())
            self.assertEqual((root / row['stdout']).read_text(), 'fake child stdout preserved\n')
            self.assertEqual(row['evidence_sha256']['log'], sha256(root / row['log']))
            self.assertEqual(row['evidence_sha256']['stdout'], sha256(root / row['stdout']))
            self.assertIn('perf-1440-foreground',
                          [failure['name'] for failure in summary['failures']])


if __name__ == '__main__':
    unittest.main()
