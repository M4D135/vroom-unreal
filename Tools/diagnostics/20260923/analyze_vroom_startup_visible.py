#!/usr/bin/env python3
"""Analyze five visible Windows startup checks; does not launch any process.

Requires Python 3.10+ and the checked-out Tools helpers. Receipts must include
started_utc/finished_utc (timezone-aware ISO 8601) for each native process.
This targeted report is not a full gameplay suite or six-circuit soak report.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import importlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib


CASE_NAMES = ('startup-normal', 'startup-frames', 'menu-flow', 'start-sequence', 'performance')
WIDTH, HEIGHT, FRAMES, WARMUP = 1920, 1080, 1800, 120
# Exactly the native Windows runner's unchanged 60-Hz budget.
LIMITS = dict(min_mean_fps=59.0, max_p99_ms=18.0,
              max_frame_ms=(1000 / 60) * 2 + .17, max_over_20_ms_fraction=.005)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-dir', type=Path, required=True)
    parser.add_argument('--archive-directory', type=Path, required=True)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--expected-version', default='0.34.0')
    parser.add_argument('--expected-gpu', default='RTX 5060')
    return parser.parse_args(argv)


def timestamp(value):
    if not isinstance(value, str):
        raise ValueError('Missing process timestamp')
    result = datetime.fromisoformat(value.replace('Z', '+00:00'))
    if result.tzinfo is None:
        raise ValueError('Process timestamp must include a UTC offset')
    return result.timestamp()


def absolute_path(value):
    if not isinstance(value, str) or not value or not Path(value).is_absolute():
        raise ValueError('Expected an absolute artifact path')
    return Path(value).resolve()


def same_path(left, right):
    try:
        return absolute_path(left) == Path(right).resolve()
    except (ValueError, OSError):
        return False


def png_dimensions(path):
    """Check the PNG container, chunk CRCs and dimensions without Pillow."""
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Invalid PNG signature')
    offset, dimensions, has_image, ended = 8, None, False, False
    while offset + 12 <= len(data):
        length = struct.unpack_from('>I', data, offset)[0]
        end = offset + length + 12
        if end > len(data):
            raise ValueError('Truncated PNG chunk')
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:end - 4]
        crc = struct.unpack_from('>I', data, end - 4)[0]
        if zlib.crc32(kind + payload) & 0xffffffff != crc:
            raise ValueError('PNG chunk CRC mismatch')
        if offset == 8:
            if kind != b'IHDR' or length != 13:
                raise ValueError('PNG lacks a valid IHDR')
            dimensions = struct.unpack('>II', payload[:8])
        if kind == b'IDAT':
            has_image = has_image or bool(length)
        offset = end
        if kind == b'IEND':
            ended = length == 0 and offset == len(data)
            break
    if not dimensions or not has_image or not ended:
        raise ValueError('PNG lacks complete image data or IEND')
    return dimensions


def argument_failures(case):
    args = case.get('arguments')
    if not isinstance(args, list) or not all(isinstance(a, str) for a in args):
        return ['Missing or invalid original argument list'], {}
    failures, values = [], {}
    for arg in args:
        key, separator, value = arg.partition('=')
        key = key.lower()
        if key in values:
            failures.append('Duplicate argument: ' + key)
        values[key] = value if separator else None
    for key in ('-d3d12', '-windowed', '-forceres', '-savetouserdir', '-nosplash', '-vroomverify'):
        if key not in values or values[key] is not None:
            failures.append('Required visible-run flag missing or malformed: ' + key)
    for key, value in (('-resx', '1920'), ('-resy', '1080'), ('-vroomgraphicspreset', '3')):
        if values.get(key) != value:
            failures.append('Unexpected or missing argument: ' + key + '=' + value)
    for key in ('-renderoffscreen', '-metaloffscreenonly', '-unattended', '-nullrhi',
                '-fullscreen', '-d3d11', '-dx11', '-vulkan', '-opengl'):
        if key in values:
            failures.append('Incompatible with visible D3D12 test: ' + key)
    if not same_path(values.get('-abslog'), case.get('log', '')):
        failures.append('Log path differs from -abslog')
    name = case.get('name')
    required = {'menu-flow': ('-vroommenutest',), 'start-sequence': ('-vroomstarttest',),
                'performance': ('-vroomautodemo', '-csvgpustats', '-csvnoprocessingthread',
                                '-exitaftercsvprofiling', '-vroommotiontrace')}.get(name, ())
    for key in required:
        if key not in values or values[key] is not None:
            failures.append('Required scenario flag missing or malformed: ' + key)
    if name == 'performance' and values.get('-csvcaptureframes') != str(FRAMES):
        failures.append('Performance capture must request exactly 1800 frames')
    if name != 'performance' and '-csvcaptureframes' in values:
        failures.append('Unexpected CSV profiling in a visual scenario')
    if name != 'startup-frames' and '-vroomstartupcapturedir' in values:
        failures.append('Startup readbacks must be isolated to startup-frames')
    if name in ('performance', 'menu-flow'):
        if case.get('capture') is not None or '-vroomcapture' in values:
            failures.append('Unexpected screenshot in ' + name)
    elif not same_path(values.get('-vroomcapture'), case.get('capture', '')):
        failures.append('Capture path differs from -VroomCapture')
    return failures, values


def analyze_case(case, helpers, metrics, gates, expected_version, expected_gpu):
    name = case.get('name')
    row = dict(name=name, arguments=case.get('arguments'), status='FAIL',
               exit=case.get('exit'), timed_out=case.get('timed_out'), artifacts={})
    failures, values = argument_failures(case)
    if type(case.get('exit')) is not int or case['exit'] != 0:
        failures.append('Native game did not exit successfully: ' + str(case.get('exit')))
    if case.get('timed_out') is not False:
        failures.append('Native game timeout state missing or true')
    if case.get('error') is not None:
        failures.append('Runner error: ' + str(case['error']))
    started = finished = None
    try:
        started, finished = timestamp(case.get('started_utc')), timestamp(case.get('finished_utc'))
        if finished < started:
            raise ValueError('Process finish precedes process start')
        row.update(started_utc=case['started_utc'], finished_utc=case['finished_utc'])
    except (TypeError, ValueError) as error:
        failures.append(str(error))

    def artifact(value, label, png=False):
        try:
            path = absolute_path(value)
            if not path.is_file() or not path.stat().st_size:
                raise ValueError('Missing or empty file')
            item = dict(path=str(path), sha256=helpers.sha256(path), bytes=path.stat().st_size)
            # NTFS is finer than this; two seconds also accommodates copied
            # artifacts from a filesystem with coarse timestamp resolution.
            if started is None or finished is None:
                raise ValueError('Freshness unconfirmed: missing process timestamps')
            item['mtime_utc'] = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).isoformat()
            if not started - 2 <= path.stat().st_mtime <= finished + 2:
                raise ValueError('Artifact timestamp is outside its native process interval')
            if png:
                item['size'] = list(png_dimensions(path))
                if item['size'] != [WIDTH, HEIGHT]:
                    raise ValueError('Actual PNG dimensions differ from 1920x1080')
            row['artifacts'][label] = item
            return path
        except (OSError, ValueError, TypeError, struct.error) as error:
            failures.append(label + ': ' + str(error))
            return None

    log = artifact(case.get('log'), 'log')
    text = log.read_text(encoding='utf-8-sig', errors='replace') if log else ''
    args = case.get('arguments') if isinstance(case.get('arguments'), list) else []
    reasons, row['checks'] = helpers.gameplay_failures(text, str(name), expected_version, args)
    failures.extend(reasons)
    if name != 'performance' and not re.search(r'VROOM: Verification PASS(?:\s|$)', text):
        failures.append('Explicit runtime Verification PASS missing')
    if not re.search(r'VROOM: Temporary graphics preset=3(?:\s|$)', text):
        failures.append('Temporary graphics preset 3 unconfirmed')
    runtime = row['runtime'] = helpers.runtime_evidence(text)
    if expected_gpu.casefold() not in (runtime['selected_gpu'] or '').casefold():
        failures.append('Selected GPU missing or not ' + expected_gpu)
    if runtime['rhi'] != 'D3D12':
        failures.append('D3D12 runtime unconfirmed')
    if runtime['dlss_game_reported_effective_mode'] != 'native':
        failures.append('Effective native graphics/DLSS state unconfirmed')
    for key, expected in (('tsrHistory', 100), ('fps', 60)):
        try:
            matches = float(runtime['graphics_applied'].get(key, 'nan')) == expected
        except (TypeError, ValueError):
            matches = False
        if not matches:
            failures.append('Effective graphics state must confirm ' + key + '=' + str(expected))

    releases = re.findall(r'VROOM: Startup cover released after HUD presentation frame=(\d+)', text)
    row['startup_cover_release_frames'] = [int(frame) for frame in releases]
    if name in ('startup-normal', 'startup-frames') and len(releases) != 1:
        failures.append('Expected exactly one startup cover release after HUD presentation')
    captures = case.get('startup_captures')
    if not isinstance(captures, list):
        failures.append('startup_captures must be an explicit array')
        captures = []
    events = re.findall(r'VROOM: Startup capture frame=(\d+) presentation=([01])(?:\s|$)', text)
    if name == 'startup-frames':
        expected_names = [f'startup-{index:02d}.png' for index in range(4)]
        if len(captures) != 4 or sorted(Path(p).name for p in captures if isinstance(p, str)) != expected_names:
            failures.append('Expected exactly four distinct startup-00.png through startup-03.png')
        if [int(frame) for frame, _ in events] != list(range(4)):
            failures.append('Expected exactly four ordered startup capture log markers (0..3)')
        if text.count('VROOM: Startup capture ') != 4:
            failures.append('Unexpected or malformed startup capture marker count')
        row['startup_capture_events'] = [dict(frame=int(frame), presentation=int(state)) for frame, state in events]
        capture_dir = values.get('-vroomstartupcapturedir')
        try:
            directory = absolute_path(capture_dir)
            actual = sorted(p.name for p in directory.glob('startup-*.png'))
            if actual != expected_names:
                failures.append('Startup directory does not contain exactly the four expected PNGs')
        except (ValueError, OSError) as error:
            failures.append('Startup capture directory: ' + str(error))
            directory = None
        for index, value in enumerate(captures):
            path = artifact(value, 'startup_' + str(index), png=True)
            if path and (directory is None or path.parent != directory):
                failures.append('Startup PNG is outside requested capture directory')
        if len(releases) == 1:
            release_frame = int(releases[0])
            if any(int(state) != int(int(frame) >= release_frame) for frame, state in events):
                failures.append('Startup capture presentation states disagree with cover release frame')
    elif captures or events or 'VROOM: Startup capture ' in text:
        failures.append('Unexpected startup readback artifacts outside startup-frames')
    if name in ('startup-normal', 'startup-frames', 'start-sequence'):
        artifact(case.get('capture'), 'capture', png=True)
    if name == 'performance':
        csv_path = artifact(case.get('csv'), 'csv')
        if csv_path:
            try:
                report = row['performance'] = metrics.analyze_csv(csv_path, WARMUP)
                metadata = row['csv_metadata'] = helpers.csv_metadata(csv_path)
                failures.extend(gates.performance_failures(report, FRAMES, LIMITS))
                for key in ('gpu_time', 'game_thread_time'):
                    if report[key] is None or report[key]['samples'] != report['analyzed_frames']:
                        failures.append('GPU/CPU timing counters missing or incomplete: ' + key)
                for key in ('GPUTime', 'GameThreadTime'):
                    if report['invalid_metric_samples_after_warmup'][key]:
                        failures.append('Invalid timing samples: ' + key)
                for axis, expected in (('resx', WIDTH), ('resy', HEIGHT)):
                    if metadata.get('systemresolution.' + axis, '').strip('"') != str(expected):
                        failures.append('Actual CSV resolution unconfirmed: ' + axis)
            except (OSError, ValueError, KeyError, TypeError) as error:
                failures.append('Performance analysis: ' + str(error))
        if log:
            try:
                row['motion'] = metrics.analyze_motion(log, WARMUP)
                failures.extend(gates.motion_failures(row['motion'], min_samples=1000))
            except (OSError, ValueError, KeyError, TypeError) as error:
                failures.append('Motion analysis: ' + str(error))
    elif case.get('csv') is not None:
        failures.append('Unexpected CSV outside performance scenario')
    row['failures'] = failures
    row['status'] = 'FAIL' if failures else 'PASS'
    return row


def main(argv=None):
    args = parse_args(argv)
    root, archive, receipt_path = args.project_dir.resolve(), args.archive_directory.resolve(), args.receipt.resolve()
    output = receipt_path.parent / 'results.json'
    summary = dict(scope='targeted-startup-validation', version=args.expected_version, platform='Windows',
                   analyzed_at=datetime.now(timezone.utc).isoformat(), completed=False,
                   automated_checks_passed=False, visual_review_required=True,
                   full_suite_validated=False, six_circuit_soaks_validated=False,
                   receipt=str(receipt_path), project_dir=str(root), archive_directory=str(archive),
                   expected_gpu=args.expected_gpu, expected_dlss='native', temporary_graphics_preset=3,
                   resolution=[WIDTH, HEIGHT], target_fps=60, frames=FRAMES, warmup=WARMUP,
                   release_limits=dict(performance=dict(LIMITS)), cases=[], failures=[],
                   limitations=['Only five targeted startup/menu/start/performance cases are covered.',
                                'This is not a full gameplay suite or six-circuit soak validation.',
                                'Visible PNGs require human review; automated checks do not certify visual quality.',
                                'Interactive Session ID is supplied by the native runner receipt.',
                                'Artifact freshness uses native timestamps with two-second filesystem tolerance.'])
    try:
        sys.path.insert(0, str(root / 'Tools'))
        helpers = importlib.import_module('windows_verification')
        metrics = importlib.import_module('analyze_performance')
        gates = importlib.import_module('regression_gates')
        summary['release_limits']['motion'] = dict(gates.MOTION_LIMITS)
        receipt = json.loads(receipt_path.read_text(encoding='utf-8-sig'))
        if not isinstance(receipt, dict):
            raise ValueError('Receipt must be a JSON object')
        summary['receipt_sha256'] = helpers.sha256(receipt_path)
        global_failures = []
        if receipt.get('scope') != summary['scope']:
            global_failures.append('Receipt scope is not targeted-startup-validation')
        if receipt.get('expected_version') != args.expected_version:
            global_failures.append('Receipt ExpectedVersion mismatch')
        for field, path in (('project_dir', root), ('archive_directory', archive)):
            if not same_path(receipt.get(field), path):
                global_failures.append('Receipt path mismatch: ' + field)
        if receipt.get('error') is not None:
            global_failures.append('Runner error: ' + str(receipt['error']))
        summary['session_id'] = receipt.get('session_id')
        if type(receipt.get('session_id')) is not int or receipt['session_id'] != 2:
            global_failures.append('Expected the visible interactive Windows Session 2')
        for field in ('package_validation_before', 'package_validation_after'):
            if receipt.get(field) is not True:
                global_failures.append('Native runner did not confirm ' + field)
        manifest_path = archive / 'vroom-build.json'
        try:
            actual_hash = helpers.sha256(manifest_path)
            summary['manifest_sha256'] = actual_hash
            for field in ('manifest_sha256_before', 'manifest_sha256_after'):
                value = receipt.get(field)
                if not isinstance(value, str) or not re.fullmatch(r'[a-fA-F0-9]{64}', value) or value.lower() != actual_hash:
                    global_failures.append('Manifest missing or changed: ' + field)
            manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
            executable, manifest_failures = helpers.validate_manifest(manifest, root, archive, args.expected_version)
            summary['executable'] = str(executable)
            global_failures.extend(manifest_failures)
            if helpers.sha256(manifest_path) != actual_hash:
                global_failures.append('Manifest changed during analysis')
        except (OSError, ValueError, TypeError, KeyError) as error:
            global_failures.append('Manifest validation: ' + str(error))
        cases = receipt.get('cases')
        if not isinstance(cases, list):
            cases = []
            global_failures.append('Receipt cases must be an array')
        names = [case.get('name') for case in cases if isinstance(case, dict)]
        if len(cases) != 5 or Counter(str(name) for name in names) != Counter(CASE_NAMES):
            global_failures.append('Expected exactly the five named cases, without duplicates')
        if global_failures:
            summary['failures'].append(dict(name='receipt-and-package', reasons=global_failures))
        for case in cases:
            if not isinstance(case, dict):
                continue
            try:
                row = analyze_case(case, helpers, metrics, gates, args.expected_version, args.expected_gpu)
            except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
                row = dict(name=case.get('name'), status='FAIL', failures=['Malformed case evidence: ' + str(error)])
            summary['cases'].append(row)
            if row['failures']:
                summary['failures'].append(dict(name=row['name'], reasons=row['failures']))
        summary['completed'] = not summary['failures']
        summary['automated_checks_passed'] = not summary['failures']
    except (OSError, ValueError, TypeError, KeyError, ImportError) as error:
        summary['failures'].append(dict(name='analysis', reasons=[str(error)]))
    output.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(('AUTOMATED CHECKS PASS' if summary['automated_checks_passed'] else 'AUTOMATED CHECKS FAIL') + ': ' + str(output))
    for failure in summary['failures']:
        print(str(failure['name']) + ': ' + '; '.join(failure['reasons']), file=sys.stderr)
    return 0 if summary['automated_checks_passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
