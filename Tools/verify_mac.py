#!/usr/bin/env python3
"""Capture reproducible Mac gameplay checks; optionally profile the packaged game.

python3 Tools/verify_mac.py --editor
python3 Tools/verify_mac.py --profile
Screenshots are offscreen; performance captures use real onscreen windows.
Results remain under Saved/Validation, outside the source repository.
"""
import argparse
import copy
import csv
from datetime import datetime
import json
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
from install_mac import sha256, package_data_paths

from analyze_performance import analyze_csv, analyze_motion
from regression_gates import PERFORMANCE_LIMITS, MOTION_LIMITS, performance_failures, motion_failures

ROOT = Path(__file__).resolve().parents[1]
CONTAINER = Path.home() / 'Library/Containers/com.YourCompany.Vroom/Data'


def normalized_version(value):
    if not isinstance(value, str) or not re.fullmatch(r'\d+(?:\.\d+){0,2}', value):
        raise ValueError('Missing or invalid version')
    parts = [int(part) for part in value.split('.')]
    return tuple(parts + [0] * (3 - len(parts)))


def validate_resume_identity(previous, current):
    """A partial report is resumable; a partial artifact identity never is."""
    if previous.get('editor') is not False or current.get('editor') is not False:
        raise ValueError('Resume is supported only for packaged builds')
    if normalized_version(previous.get('version')) != normalized_version(current.get('version')):
        raise ValueError('Resume version differs from the current package')
    for report in (previous, current):
        if not re.fullmatch(r'[0-9a-f]{64}', str(report.get('executable_sha256', ''))):
            raise ValueError('Resume requires a complete executable SHA256')
        manifest = report.get('package_data_sha256')
        if not isinstance(manifest, dict) or not manifest or any(
                not isinstance(path, str) or not re.fullmatch(r'[0-9a-f]{64}', str(digest))
                for path, digest in manifest.items()):
            raise ValueError('Resume requires a complete package data SHA256 manifest')
    if previous['executable_sha256'] != current['executable_sha256']:
        raise ValueError('Resume executable SHA256 differs')
    if previous['package_data_sha256'] != current['package_data_sha256']:
        raise ValueError('Resume package data file set or SHA256 differs')
    if not previous.get('release_limits') or previous.get('release_limits') != current.get('release_limits'):
        raise ValueError('Resume release limits differ or are missing')


def failed_stage(previous, name):
    return any(failure.get('name') == name or failure.get('name', '').startswith(name + '-')
               for failure in previous.get('failures', []))


def normalize_foreground(exit_code, stdout, stderr):
    locked = bool(re.search(r'(?<!\w)(?:session_)?locked=1(?=\s|$)', stdout))
    mode = 'onscreen_locked' if exit_code == 5 and locked else (
        'onscreen_active' if exit_code == 0 else 'onscreen_unconfirmed')
    return {'exit': exit_code, 'stdout': stdout, 'stderr': stderr, 'status': stdout.strip(),
            'locked': locked, 'display_mode': mode, 'interaction_verified': False}


def foreground_allowed(activation):
    return activation.get('exit') == 0 or (activation.get('exit') == 5 and activation.get('locked') is True and
        bool(re.search(r'(?<!\w)(?:session_)?locked=1(?=\s|$)', activation.get('stdout', ''))))


def evidence_path(value, root):
    if not isinstance(value, str):
        return None
    path = (root / value).resolve()
    try:
        path.relative_to((root / 'Saved/Validation').resolve())
    except ValueError:
        return None
    return path if path.is_file() and path.stat().st_size else None


def runtime_failures(body, version, flags):
    failures = [line for line in body.splitlines() if 'LogMaterial: Error:' in line or
                ('Failed to compile Material' in line and 'Default Material' in line)]
    versions = re.findall(r'VROOM: Version ([0-9.]+)', body)
    try:
        version_matches = versions and normalized_version(versions[-1]) == normalized_version(version)
    except ValueError:
        version_matches = False
    if not version_matches:
        failures.append('Missing or incorrect executable version')
    checks = [line.split('LogTemp: Display: ')[-1] for line in body.splitlines() if 'VROOM: Verification' in line]
    if (not checks and '-ExitAfterCsvProfiling' not in flags) or any('FAIL' in line for line in checks):
        failures.append('Missing or failed runtime verification')
    return failures, checks


REQUIRED_MARKERS = {
    'progress-data': ['Progress self-test PASS'],
    'menu-flow': ['Menu self-test PASS', 'Settings self-test PASS'],
    'drift-feedback': ['Drift feedback PASS'],
    'options': ['Graphics preview PASS'],
}


def case_reusable(previous, name, request, root=ROOT):
    if failed_stage(previous, name):
        return None
    rows = [row for row in previous.get('cases', []) if row.get('name') == name]
    if len(rows) != 1:
        return None
    row = rows[0]
    if row.get('exit') != 0 or row.get('status', 'PASS') != 'PASS' or row.get('offscreen') != request['capture']:
        return None
    if 'request' in row and row['request'] != request:
        return None
    checks = row.get('checks')
    if not isinstance(checks, list) or any(not isinstance(check, str) or 'FAIL' in check for check in checks) or (
            not checks and '-ExitAfterCsvProfiling' not in request['flags']):
        return None
    log = evidence_path(row.get('log'), root)
    if not log:
        return None
    if 'stdout' in row and not evidence_path(row['stdout'], root):
        return None
    body = log.read_text(errors='replace')
    try:
        failures, _ = runtime_failures(body, previous['version'], request['flags'])
        if failures or any(marker not in body for marker in REQUIRED_MARKERS.get(name, [])):
            return None
        commands = re.findall(r'LogInit: Command Line:\s*(.*)', body)
        tokens = shlex.split(commands[-1]) if commands else []
    except (ValueError, KeyError):
        return None
    expected = request['flags'] + ['-VroomVerify', '-windowed', '-ForceRes',
        f"-ResX={request['resolution'][0]}", f"-ResY={request['resolution'][1]}",
        f"-VroomExitAfter={request['seconds']}"]
    if request['capture']:
        expected += ['-RenderOffScreen', '-MetalOffscreenOnly', '-unattended',
                     f"-VroomCaptureAfter={request['capture_after']}"]
    if any(flag not in tokens for flag in expected):
        return None
    for field, digest in row.get('evidence_sha256', {}).items():
        proof = evidence_path(row.get(field), root)
        if not proof or sha256(proof) != digest:
            return None
    if name.startswith(('perf-', 'soak-')) or name == 'motion':
        if not foreground_allowed(row.get('foreground', {})):
            return None
    if request['capture']:
        png = evidence_path(row.get('capture'), root)
        if not png:
            return None
        header = png.read_bytes()[:24]
        if len(header) != 24 or header[:8] != b'\x89PNG\r\n\x1a\n' or list(struct.unpack('>II', header[16:24])) != request['resolution']:
            return None
        if row.get('size') != request['resolution']:
            return None
    return copy.deepcopy(row)


def profile_reusable(previous, name, frame_count, resolution, distance, track, root=ROOT):
    """Re-evaluate the original CSV/motion evidence with today's unchanged gates."""
    if failed_stage(previous, name):
        return None
    rows = [row for row in previous.get('performance', []) if row.get('name') == name]
    if len(rows) != 1:
        return None
    row = rows[0]
    if any(row.get(key) != value for key, value in {'expected_frames': frame_count,
            'resolution': list(resolution), 'distance': distance, 'track': track}.items()):
        return None
    csv_path = evidence_path(row.get('report', {}).get('source', {}).get('path'), root)
    if not csv_path or (row.get('csv_sha256') and sha256(csv_path) != row['csv_sha256']):
        return None
    try:
        report = analyze_csv(csv_path, 120)
        if performance_failures(report, frame_count):
            return None
        if track is not None:
            motions = [item for item in previous.get('soak_motion', []) if item.get('track') == track]
            if len(motions) != 1 or motions[0].get('complete') is not True:
                return None
            log = evidence_path(motions[0].get('report', {}).get('source', {}).get('path'), root)
            if not log or motion_failures(analyze_motion(log, 120), min_samples=7000):
                return None
            if not re.search(rf'VROOM-TOUR track={track} [^\n]*complete=1', log.read_text(errors='replace')):
                return None
    except (ValueError, KeyError, OSError, csv.Error):
        return None
    return csv_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--editor', action='store_true', help='cockpit checks only, using the compiled editor module')
    parser.add_argument('--profile', action='store_true', help='also run two onscreen 1800-frame profiles and motion telemetry')
    parser.add_argument('--soak', action='store_true', help='also drive all six full circuits for at least 120 seconds each, with performance and camera gates')
    parser.add_argument('--resume', type=Path, help='reuse proven PASS stages from this results.json only for the identical package')
    args = parser.parse_args()
    previous = json.loads(args.resume.read_text()) if args.resume else {}
    if args.resume and not (args.profile or args.soak):
        requested = previous.get('requested')
        if not isinstance(requested, dict):
            parser.error('Legacy reports require explicit --profile and/or --soak when resuming.')
        args.profile, args.soak = requested.get('profile', False), requested.get('soak', False)
    if args.resume and args.editor:
        parser.error('Resume is supported only for packaged builds.')
    if args.editor and (args.profile or args.soak):
        parser.error('Performance must be measured on the package, without --editor.')
    version = re.search(r'ProjectVersion=([^\n]+)', (ROOT / 'Config/DefaultGame.ini').read_text())[1].strip()
    tag = datetime.now().strftime('%Y%m%d-%H%M%S') + ('-editor' if args.editor else '-package')
    out = CONTAINER / 'Documents/VroomVerification' / tag
    saved = ROOT / 'Saved/Validation' / ('v' + version) / tag
    out.mkdir(parents=True, exist_ok=True)
    saved.mkdir(parents=True, exist_ok=True)
    executable = ROOT / ('Tools/run_mac.command' if args.editor else 'Saved/Packages/Mac/Vroom.app/Contents/MacOS/Vroom')
    if not executable.is_file():
        raise RuntimeError(f'Build missing: {executable}')
    summary = {'version': version, 'date': datetime.now().isoformat(), 'editor': args.editor, 'completed': False, 'cases': [], 'performance': []}
    summary['requested'] = {'profile': args.profile, 'soak': args.soak}
    summary['release_limits'] = {'performance': PERFORMANCE_LIMITS, 'motion': MOTION_LIMITS}
    if not args.editor:
        bundle = executable.parents[2]
        summary['executable_sha256'] = sha256(executable)
        summary['package_data_sha256'] = {relative: sha256(bundle / relative)
                                          for relative in sorted(package_data_paths(bundle))}
    if args.resume:
        validate_resume_identity(previous, summary)
        summary['history'] = copy.deepcopy(previous.get('history', [])) + [{
            'report': str(args.resume.resolve()), 'date': previous.get('date'),
            'completed': previous.get('completed', False), 'failures': copy.deepcopy(previous.get('failures', [])),
            'report_sha256': sha256(args.resume)}]
    activation_tool = saved / 'activate-mac'
    if args.profile or args.soak:
        subprocess.run(['xcrun', 'clang', '-O2', '-fobjc-arc', '-Wno-deprecated-declarations',
                        '-framework', 'Cocoa', '-framework', 'ApplicationServices',
                        str(ROOT / 'Tools/activate_mac.m'), '-o', str(activation_tool)], check=True)

    def preserve(path):
        target = saved / path.name
        shutil.copy2(path, target)
        return str(target.relative_to(ROOT))

    def write_results():
        temporary = saved / 'results.json.tmp'
        temporary.write_text(json.dumps(summary, indent=2) + '\n')
        temporary.replace(saved / 'results.json')

    def gate(name, failures):
        if failures:
            summary.setdefault('failures', []).append({'name': name, 'reasons': failures})
            for row in summary['cases']:
                if row['name'] == name or name.startswith(row['name'] + '-'):
                    row['status'] = 'FAIL'
            write_results()
            raise RuntimeError(f'{name}: ' + '; '.join(failures))

    def request_for(flags, seconds, capture, resolution, capture_after=3):
        return {'flags': flags, 'seconds': seconds, 'capture': capture,
                'resolution': list(resolution), 'capture_after': capture_after}

    def run(name, flags, seconds=5, capture=True, resolution=(1440, 900), capture_after=3, allow_resume=True):
        request = request_for(flags, seconds, capture, resolution, capture_after)
        cached = case_reusable(previous, name, request) if allow_resume else None
        if cached:
            for field in ('log', 'stdout', 'capture'):
                if field in cached:
                    cached[field] = preserve(evidence_path(cached[field], ROOT))
            cached['evidence_sha256'] = {field: sha256(ROOT / cached[field])
                                       for field in ('log', 'stdout', 'capture') if field in cached}
            if cached.get('foreground', {}).get('display_mode') == 'onscreen_locked':
                summary['locked_session_measurements'] = True
            cached.update(status='PASS', request=request, reused_from=str(args.resume.resolve()))
            summary['cases'].append(cached)
            write_results()
            print(f'{name}: PASS (reused evidence)', flush=True)
            return evidence_path(cached['log'], ROOT)
        log, png = out / (name + '.log'), out / (name + '.png')
        cmd = [str(executable), '-LLM', '-windowed', '-ForceRes', f'-ResX={resolution[0]}', f'-ResY={resolution[1]}',
               '-VroomVerify', f'-abslog={log}', f'-VroomExitAfter={seconds}']
        if capture:
            cmd += ['-RenderOffScreen', '-MetalOffscreenOnly', '-unattended', f'-VroomCapture={png}', f'-VroomCaptureAfter={capture_after}']
        activation, execution_error, exit_code = None, None, None
        stdout_log = out / (name + '-stdout.log')
        with stdout_log.open('w') as stream:
            process = subprocess.Popen(cmd + flags, cwd='/tmp', stdout=stream, stderr=subprocess.STDOUT)
            try:
                if not capture and (name.startswith(('perf-', 'soak-')) or name == 'motion'):
                    try:
                        result = subprocess.run([str(activation_tool), str(process.pid), str(executable.parents[2])],
                                                capture_output=True, text=True, timeout=15)
                        activation = normalize_foreground(result.returncode, result.stdout, result.stderr)
                    except (subprocess.TimeoutExpired, OSError) as error:
                        def as_text(value):
                            return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
                        activation = {'exit': None, 'stdout': as_text(getattr(error, 'stdout', '')),
                                      'stderr': as_text(getattr(error, 'stderr', '')), 'error': str(error)}
                exit_code = process.wait(timeout=seconds + 90)
            except subprocess.TimeoutExpired as error:
                execution_error = str(error)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
        body = log.read_text(errors='replace') if log.exists() else ''
        failures, checks = runtime_failures(body, version, flags)
        row = {'name': name, 'exit': exit_code, 'checks': checks, 'offscreen': capture,
               'status': 'EXECUTED', 'request': request, 'stdout': preserve(stdout_log)}
        if log.exists():
            row['log'] = preserve(log)
        if activation is not None:
            row['foreground'] = activation
            row['display_mode'] = activation.get('display_mode', 'onscreen_unconfirmed')
            if activation.get('display_mode') == 'onscreen_locked':
                row['interaction_verified'] = False
                summary['locked_session_measurements'] = True
                print(f'{name}: session locked; measuring the onscreen render loop, interaction not verified', flush=True)
        if execution_error:
            row['execution_error'] = execution_error
        row['evidence_sha256'] = {field: sha256(ROOT / row[field]) for field in ('stdout', 'log') if field in row}
        # Preserve child output and activation stdout/stderr/code BEFORE any gate
        # can raise, including version/material failures and activation timeouts.
        summary['cases'].append(row)
        write_results()
        gate(name, failures + ([f'Execution exit={exit_code}: {execution_error or "runtime failed"}'] if exit_code != 0 else []))
        if activation is not None:
            gate(name + '-foreground', [] if foreground_allowed(activation)
                 else ['Unable to activate the benchmark window without a verified session lock'])
        if capture:
            gate(name, [] if png.exists() and png.stat().st_size >= 24 else ['Missing capture evidence'])
            header = png.read_bytes()[:24]
            dimensions = struct.unpack('>II', header[16:24])
            gate(name, [] if header[:8] == b'\x89PNG\r\n\x1a\n' and dimensions == resolution
                 else [f'Invalid PNG capture; expected {resolution}, got {dimensions}'])
            row.update(size=list(dimensions), capture=preserve(png))
            row['evidence_sha256']['capture'] = sha256(png)
        gate(name, [f'Missing {marker}' for marker in REQUIRED_MARKERS.get(name, []) if marker not in body])
        row['status'] = 'PASS'
        write_results()
        print(f'{name}: PASS', flush=True)
        return log

    if not args.editor:
        for name, flag, expected in [
                ('progress-data', '-VroomProgressTest', ['Progress self-test PASS']),
                ('menu-flow', '-VroomMenuTest', ['Menu self-test PASS', 'Settings self-test PASS'])]:
            log = run(name, [flag], capture=False)
            text = log.read_text(errors='replace')
            gate(name, [f'Missing {check}' for check in expected if check not in text])
        run('pit-service', ['-VroomPitTest', '-VroomChaseCamera'], seconds=9)
        drift_log = run('drift-feedback', ['-VroomDriftTest', '-VroomChaseCamera', '-VroomStartDistance=350',
                                         '-VroomStartSpeed=22'], capture_after=1.15)
        gate('drift-feedback', [] if 'Drift feedback PASS' in drift_log.read_text(errors='replace') else ['Missing drift feedback pass'])
        for scenario in ['slow', 'crash', 'tyre', 'scenery']:
            run('collision-' + scenario, ['-VroomCollisionTest=' + scenario, '-VroomChaseCamera'], seconds=8)

    for name, steer, extra in [('neutral', 0, []), ('right', .62, []), ('left', -.62, []),
                               ('small-right', .04, []), ('mid-left', -.21, []),
                               ('inspect-neutral', 0, ['-VroomInspectCockpit'])]:
        run(name, ['-VroomAutoDemo', '-VroomStartDistance=350', '-VroomStartSpeed=0', f'-VroomPreviewSteering={steer}'] + extra)
    if not args.editor:
        run('neutral-16x9', ['-VroomAutoDemo', '-VroomStartDistance=350', '-VroomStartSpeed=0', '-VroomPreviewSteering=0'], resolution=(1600, 900))
        run('cars', ['-VroomAutoDemo', '-VroomStartDistance=55', '-VroomStartSpeed=0', '-VroomPreviewSteering=0', '-VroomChaseCamera'])
        options_log = run('options', ['-VroomGraphicsPreview'])
        gate('options-panel', [] if 'Graphics preview PASS' in options_log.read_text(errors='replace')
             else ['Graphics panel was not confirmed open'])
        for panel in ['qualifying', 'results', 'arcade', 'pit', 'controls']:
            run('session-' + panel, ['-VroomSessionPreview=' + panel])
    if args.profile or args.soak:
        csv_dir = CONTAINER / 'Library/Application Support/Epic/Vroom/Saved/Profiling/CSV'
        profiles_to_run = [(1440, 900, 3295, None), (2560, 1664, 2190, None)] if args.profile else []
        if args.soak:
            profiles_to_run += [(1440, 900, 0, track) for track in range(6)]
        for width, height, distance, track in profiles_to_run:
            frame_count = 7800 if track is not None else 1800
            name = f'soak-track-{track + 1}' if track is not None else f'perf-{width}'
            flags = ['-VroomAutoDemo', f'-VroomStartDistance={distance}', '-VroomStartSpeed=30',
                     f'-csvCaptureFrames={frame_count}', '-csvGpuStats', '-ExitAfterCsvProfiling',
                     '-LogCmds=LogPSOHitching Verbose']
            if track is not None:
                flags += [f'-VroomTrack={track}', '-VroomTourDemo', '-VroomMotionTrace']
            seconds = 165 if track is not None else 60
            cached_profile = profile_reusable(previous, name, frame_count, (width, height), distance, track)
            if cached_profile and not case_reusable(previous, name, request_for(flags, seconds, False, (width, height))):
                cached_profile = None
            before = set(csv_dir.glob('*.csv'))
            profile_log = run(name, flags, seconds=seconds, capture=False, resolution=(width, height),
                              allow_resume=cached_profile is not None)
            if cached_profile:
                profile = cached_profile
            else:
                profiles = set(csv_dir.glob('*.csv')) - before
                gate(name + '-csv', [] if len(profiles) == 1 else [f'Expected one new profile, got {sorted(map(str, profiles))}'])
                profile = profiles.pop()
            report = analyze_csv(profile, 120)
            report['source']['path'] = preserve(profile)
            summary['performance'].append({'name': name, 'expected_frames': frame_count, 'resolution': [width, height], 'track': track,
                                           'distance': distance, 'report': report, 'csv_sha256': sha256(profile),
                                           'display_mode': summary['cases'][-1].get('display_mode', 'onscreen_active'),
                                           'interaction_verified': False})
            gate(name, performance_failures(report, frame_count))
            if track is not None:
                motion = analyze_motion(profile_log, 120)
                motion['source']['path'] = str((saved / profile_log.name).relative_to(ROOT))
                completed = bool(re.search(rf'VROOM-TOUR track={track} [^\n]*complete=1', profile_log.read_text(errors='replace')))
                summary.setdefault('soak_motion', []).append({'track': track, 'complete': completed, 'report': motion})
                gate(name + '-coverage', [] if completed else ['The assisted car did not complete a full lap'])
                gate(name + '-camera', motion_failures(motion, min_samples=7000))
            write_results()
            print(json.dumps(summary['performance'][-1]), flush=True)
        old_motion = evidence_path(previous.get('motion', {}).get('source', {}).get('path'), ROOT)
        reuse_motion = bool(old_motion and not motion_failures(analyze_motion(old_motion, 120)))
        motion_log = run('motion', ['-VroomAutoDemo', '-VroomStartDistance=3180', '-VroomStartSpeed=30', '-VroomMotionTrace'], seconds=25,
                         capture=False, allow_resume=reuse_motion)
        summary['motion'] = analyze_motion(motion_log, 120)
        summary['motion']['source']['path'] = str((saved / motion_log.name).relative_to(ROOT))
        gate('motion', motion_failures(summary['motion']))
        write_results()
    if not args.editor:
        gate('artifact-unchanged', ['Executable changed during validation']
             if sha256(executable) != summary['executable_sha256'] else [])
        gate('data-file-list-unchanged', ['Packaged data files added or removed during validation']
             if package_data_paths(bundle) != set(summary['package_data_sha256']) else [])
        gate('data-unchanged', [f'Data changed during validation: {path}'
             for path, digest in summary['package_data_sha256'].items() if sha256(bundle / path) != digest])
    summary['completed'] = True
    write_results()
    print(f'RESULTS: {saved / "results.json"}', flush=True)


if __name__ == '__main__':
    main()
