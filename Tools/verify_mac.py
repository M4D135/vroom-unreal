#!/usr/bin/env python3
"""Capture reproducible Mac gameplay checks; optionally profile the packaged game.

python3 Tools/verify_mac.py --editor
python3 Tools/verify_mac.py --profile
Screenshots are offscreen; performance captures use real onscreen windows.
Results remain under Saved/Validation, outside the source repository.
"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

from analyze_performance import analyze_csv, analyze_motion

ROOT = Path(__file__).resolve().parents[1]
CONTAINER = Path.home() / 'Library/Containers/com.YourCompany.Vroom/Data'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--editor', action='store_true', help='cockpit checks only, using the compiled editor module')
    parser.add_argument('--profile', action='store_true', help='also run two onscreen 1800-frame profiles and motion telemetry')
    args = parser.parse_args()
    if args.editor and args.profile:
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
    summary = {'version': version, 'date': datetime.now().isoformat(), 'editor': args.editor, 'cases': [], 'performance': []}

    def preserve(path):
        target = saved / path.name
        shutil.copy2(path, target)
        return str(target.relative_to(ROOT))

    def write_results():
        (saved / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')

    def run(name, flags, seconds=5, capture=True, resolution=(1440, 900)):
        log, png = out / (name + '.log'), out / (name + '.png')
        cmd = [str(executable), '-LLM', '-windowed', '-ForceRes', f'-ResX={resolution[0]}', f'-ResY={resolution[1]}',
               '-VroomVerify', f'-abslog={log}', f'-VroomExitAfter={seconds}']
        if capture:
            cmd += ['-RenderOffScreen', '-MetalOffscreenOnly', '-unattended', f'-VroomCapture={png}', '-VroomCaptureAfter=3']
        with (out / (name + '-stdout.log')).open('w') as stream:
            result = subprocess.run(cmd + flags, cwd='/tmp', stdout=stream, stderr=subprocess.STDOUT, timeout=seconds + 90)
        body = log.read_text(errors='replace') if log.exists() else ''
        checks = [line.split('LogTemp: Display: ')[-1] for line in body.splitlines() if 'VROOM: Verification' in line]
        if result.returncode or (not checks and '-ExitAfterCsvProfiling' not in flags) or any('FAIL' in line for line in checks):
            summary.setdefault('failures', []).append({'name': name, 'exit': result.returncode, 'checks': checks, 'log': str(log)})
            write_results()
            raise RuntimeError(f'{name}: exit={result.returncode}, checks={checks}, log={log}')
        row = {'name': name, 'exit': result.returncode, 'checks': checks, 'log': preserve(log), 'offscreen': capture}
        if capture:
            dimensions = struct.unpack('>II', png.read_bytes()[16:24])
            if dimensions != resolution:
                raise RuntimeError(f'{name}: expected {resolution}, got {dimensions}')
            row.update(size=list(dimensions), capture=preserve(png))
        summary['cases'].append(row)
        write_results()
        print(f'{name}: PASS', flush=True)
        return log

    for name, steer, extra in [('neutral', 0, []), ('right', .62, []), ('left', -.62, []),
                               ('small-right', .04, []), ('mid-left', -.21, []),
                               ('inspect-neutral', 0, ['-VroomInspectCockpit'])]:
        run(name, ['-VroomAutoDemo', '-VroomStartDistance=350', '-VroomStartSpeed=0', f'-VroomPreviewSteering={steer}'] + extra)
    if not args.editor:
        run('neutral-16x9', ['-VroomAutoDemo', '-VroomStartDistance=350', '-VroomStartSpeed=0', '-VroomPreviewSteering=0'], resolution=(1600, 900))
        run('cars', ['-VroomAutoDemo', '-VroomStartDistance=55', '-VroomStartSpeed=0', '-VroomPreviewSteering=0', '-VroomChaseCamera'])
        run('options', ['-VroomGraphicsPreview'])
        for scenario in ['slow', 'crash', 'tyre', 'scenery']:
            run('collision-' + scenario, ['-VroomCollisionTest=' + scenario, '-VroomChaseCamera'], seconds=8)
    if args.profile:
        csv_dir = CONTAINER / 'Library/Application Support/Epic/Vroom/Saved/Profiling/CSV'
        for width, height, distance in [(1440, 900, 3295), (2560, 1664, 2190)]:
            before = set(csv_dir.glob('*.csv'))
            run(f'perf-{width}', ['-VroomAutoDemo', f'-VroomStartDistance={distance}', '-VroomStartSpeed=30',
                '-csvCaptureFrames=1800', '-csvGpuStats', '-ExitAfterCsvProfiling'], seconds=60, capture=False, resolution=(width, height))
            profiles = set(csv_dir.glob('*.csv')) - before
            if len(profiles) != 1:
                raise RuntimeError(f'Expected one new profile, got {profiles}')
            profile = profiles.pop()
            report = analyze_csv(profile, 120)
            if report['numeric_frame_rows'] != 1800 or report['invalid_frames_after_warmup']:
                raise RuntimeError(f'Incomplete or invalid performance capture: {report}')
            report['source']['path'] = preserve(profile)
            summary['performance'].append({'resolution': [width, height], 'distance': distance, 'report': report})
            write_results()
            print(json.dumps(summary['performance'][-1]), flush=True)
        motion_log = run('motion', ['-VroomAutoDemo', '-VroomStartDistance=3180', '-VroomStartSpeed=30', '-VroomMotionTrace'], seconds=25, capture=False)
        summary['motion'] = analyze_motion(motion_log, 120)
        summary['motion']['source']['path'] = str((saved / motion_log.name).relative_to(ROOT))
        write_results()
    print(f'RESULTS: {saved / "results.json"}', flush=True)


if __name__ == '__main__':
    main()
