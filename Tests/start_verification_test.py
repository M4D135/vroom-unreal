"""Exercise the actual Mac release gate with complete and damaged runtime logs."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools'))
from verify_mac import start_signal_failures

RESTART = 'VROOM: Start test restarted during countdown\n'
RESUMED = 'VROOM: Start test resumed after pause\n'


def countdown(signals=(0, 1, 2)):
    clocks = (-2.992, -1.992, .008)
    columns = (2, 4, 5)
    frequencies = (880, 1175, 1760)
    lines = []
    for signal in signals:
        lines.append(f'LogTemp: Display: VROOM: Start lights columns={columns[signal]} '
                     f'raceClock={clocks[signal]:.3f} signal={signal}\n')
        lines.append(f'LogTemp: Display: VROOM: Start beep signal={signal} '
                     f'frequency={frequencies[signal]} duration=0.20 raceClock={clocks[signal]:.3f}\n')
    return ''.join(lines)


class StartVerificationTests(unittest.TestCase):
    def assert_rejected(self, body, name='start-sequence'):
        self.assertTrue(start_signal_failures(body, name), (name, body))

    def test_complete_countdown_and_muted_countdown_pass(self):
        for name in ('start-sequence', 'start-mute'):
            with self.subTest(name=name):
                # Mute suppresses output volume, not the underlying scheduled
                # start events. The runtime separately verifies MasterVolume=0.
                self.assertEqual(start_signal_failures(countdown(), name), [])

    def test_light_switching_off_after_green_is_not_an_extra_start_event(self):
        body = countdown() + 'VROOM: Start lights columns=0 raceClock=0.408 signal=3\n'
        self.assertEqual(start_signal_failures(body, 'start-sequence'), [])

    def test_valid_interrupted_prefix_then_full_restart_passes(self):
        for prefix in ((0,), (0, 1)):
            with self.subTest(prefix=prefix):
                self.assertEqual(start_signal_failures(countdown(prefix) + RESTART + countdown(),
                                                      'start-restart'), [])

    def test_pause_requires_resume_evidence(self):
        self.assertEqual(start_signal_failures(countdown() + RESUMED, 'start-pause'), [])
        self.assert_rejected(countdown(), 'start-pause')

    def test_missing_or_empty_events_fail(self):
        self.assert_rejected('VROOM: Start integration PASS\n')
        for kind in ('beep', 'lights'):
            for signal in range(3):
                with self.subTest(kind=kind, signal=signal):
                    body = ''.join(line + '\n' for line in countdown().splitlines()
                                   if not (f'Start {kind} ' in line and f'signal={signal}' in line))
                    self.assert_rejected(body)

    def test_duplicates_and_reordering_fail_for_both_outputs(self):
        for kind in ('beep', 'lights'):
            for signal in range(3):
                with self.subTest(kind=kind, signal=signal):
                    event = next(line for line in countdown().splitlines()
                                 if f'Start {kind} ' in line and f'signal={signal}' in line)
                    self.assert_rejected(countdown() + event + '\n')
            events = [line for line in countdown().splitlines() if f'Start {kind} ' in line]
            other = [line for line in countdown().splitlines() if f'Start {kind} ' not in line]
            self.assert_rejected('\n'.join(other + list(reversed(events))) + '\n')

    def test_signals_must_match_their_race_clock_for_beeps_and_lights(self):
        bad_clocks = {0: '-1.500', 1: '0.100', 2: '-0.001'}
        for kind in ('beep', 'lights'):
            for signal, clock in bad_clocks.items():
                with self.subTest(kind=kind, signal=signal):
                    body = []
                    for line in countdown().splitlines():
                        if f'Start {kind} ' in line and f'signal={signal}' in line:
                            fields = line.split()
                            fields = [f'raceClock={clock}' if field.startswith('raceClock=') else field
                                      for field in fields]
                            line = ' '.join(fields)
                        body.append(line)
                    self.assert_rejected('\n'.join(body) + '\n')

    def test_missing_or_malformed_clock_is_failed_evidence_not_a_crash(self):
        for kind in ('beep', 'lights'):
            for replacement in ('', 'raceClock=nan', 'raceClock=-', 'raceClock='):
                with self.subTest(kind=kind, replacement=replacement):
                    body = []
                    for line in countdown().splitlines():
                        if f'Start {kind} ' in line and 'signal=1' in line:
                            line = line.replace('raceClock=-1.992', replacement)
                        body.append(line)
                    self.assert_rejected('\n'.join(body) + '\n')

    def test_restart_requires_one_marker_and_complete_final_countdown(self):
        self.assert_rejected(countdown(), 'start-restart')
        self.assert_rejected(RESTART + countdown(), 'start-restart')
        self.assert_rejected(countdown((0, 1)) + RESTART + countdown((0, 1)), 'start-restart')
        self.assert_rejected(countdown((0,)) + RESTART + countdown((0,)) + RESTART + countdown(),
                             'start-restart')

    def test_unrequested_restarts_are_rejected_even_when_there_are_two(self):
        for name in ('start-sequence', 'start-pause', 'start-mute'):
            for count in (1, 2, 3):
                with self.subTest(name=name, count=count):
                    body = (countdown((0,)) + RESTART) * count + countdown() + RESUMED
                    self.assert_rejected(body, name)

    def test_other_runtime_cases_do_not_require_start_signals(self):
        self.assertEqual(start_signal_failures('', 'offroad-left'), [])
        self.assertEqual(start_signal_failures('VROOM: Verification PASS', 'neutral'), [])


if __name__ == '__main__':
    unittest.main()
