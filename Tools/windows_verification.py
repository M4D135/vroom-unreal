#!/usr/bin/env python3
"""Native Windows package checks; analysis/manifest helpers are portable.

Invoked by verify_windows.ps1. This is preparation until run on Windows.
The game runs onscreen. Screenshots are separate from timing captures.
"""
from __future__ import annotations
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

from analyze_performance import analyze_csv, analyze_motion, is_csv_metadata
from regression_gates import performance_failures, motion_failures, MOTION_LIMITS


def version_tuple(value):
    if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?', value):
        raise ValueError('Expected a numeric release version, e.g. 0.30.0')
    parts = tuple(map(int, value.split('.')))
    return parts + (0,) if len(parts) == 2 else parts


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def validate_manifest(manifest, root, archive, expected_version):
    errors = []
    if manifest.get('schema_version') != 1:
        errors.append('Unsupported build manifest schema')
    if manifest.get('configuration') != 'Development':
        errors.append('Verification requires a Development package with logs and CSV profiling')
    if version_tuple(str(manifest.get('version', ''))) != version_tuple(expected_version):
        errors.append('Build manifest version differs from ExpectedVersion')
    version = re.search(r'^ProjectVersion=(.+)$', (root / 'Config/DefaultGame.ini').read_text(), re.M)
    if not version or version_tuple(version[1].strip()) != version_tuple(expected_version):
        errors.append('Checkout version differs from ExpectedVersion')
    def contained(base, relative):
        p = (base / relative).resolve()
        if not p.is_relative_to(base.resolve()):
            raise ValueError('Manifest path escapes its root')
        return p
    executable = contained(archive, manifest['executable'])
    if not executable.is_file() or sha256(executable) != manifest.get('executable_sha256', '').lower():
        errors.append('Package executable is missing or differs from the build manifest')
    files = manifest.get('source_files', [])
    if not files:
        errors.append('Build manifest lacks source/asset fingerprints')
    recorded = {row['path'] for row in files}
    current = {p.relative_to(root).as_posix() for folder in ('Source','Config','Content')
               for p in (root/folder).rglob('*') if p.is_file()}
    if current - recorded:
        errors.append('Source/assets added since package build: ' + ', '.join(sorted(current-recorded)))
    for row in files:
        p = contained(root, row['path'])
        if not p.is_file() or sha256(p) != row['sha256'].lower():
            errors.append('Source/asset changed since package build: ' + row['path'])
    return executable, errors


def unreal_command_line(executable, arguments):
    # Windows CreateProcess receives this string directly, without cmd.exe.
    # FParse needs -key="value with spaces" rather than "-key=value ...".
    parts = [subprocess.list2cmdline([str(executable)])]
    for argument in arguments:
        if any(c in argument for c in ('"', '\r', '\n', '\x00')):
            raise ValueError('Unreal arguments must not contain quotes/newlines/NUL')
        if any(c.isspace() for c in argument):
            key, sep, value = argument.partition('=')
            if sep and key.startswith('-'):
                argument = key + '=' + subprocess.list2cmdline([value])
            else:
                argument = subprocess.list2cmdline([argument])
        parts.append(argument)
    return ' '.join(parts)


def runtime_evidence(text):
    adapters = {int(i): name.strip() for i, name in re.findall(
        r'Found D3D12 adapter (\d+): (.+?) \(VendorId:', text)}
    chosen = re.findall(r'Chosen D3D12 Adapter Id = (\d+)', text)
    chosen_id = int(chosen[-1]) if chosen else None
    settings = re.findall(r'VROOM: Graphics applied ([^\r\n]+)', text)
    applied = dict(re.findall(r'(\w+)=([-\d.]+)', settings[-1])) if settings else {}
    mode = int(applied['DLSS']) if 'DLSS' in applied else None
    modes = {0: 'native', 1: 'dlaa', 2: 'quality', 3: 'balanced', 4: 'performance'}
    return {
        'selected_adapter_id': chosen_id,
        'selected_gpu': adapters.get(chosen_id),
        'rhi': 'D3D12' if chosen_id is not None else None,
        'graphics_applied': applied,
        'dlss_game_reported_effective_mode': modes.get(mode),
        'evidence_lines': [line for line in text.splitlines() if re.search(
            r'(Chosen D3D12 Adapter|Found D3D12 adapter|Driver Version|Feature Level|Ray tracing|VROOM: Graphics applied|LogDLSS|DLSS activation refused)', line, re.I)],
    }


def csv_metadata(path):
    result = {}
    with path.open(encoding='utf-8-sig', errors='replace', newline='') as stream:
        for row in csv.reader(stream):
            if is_csv_metadata(row):
                result.update({k.strip('[]').lower(): v for k, v in zip(row[::2], row[1::2])})
    return result


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project-dir', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--archive-directory', type=Path)
    p.add_argument('--expected-version', required=True)
    p.add_argument('--width', type=int, default=3840); p.add_argument('--height', type=int, default=2160)
    p.add_argument('--profile', action='store_true'); p.add_argument('--soak', action='store_true')
    p.add_argument('--target-fps', type=int, choices=(60,120), default=60,
                   help='Validation target; does not silently overwrite saved graphics settings')
    p.add_argument('--frames', type=int, default=1800)
    p.add_argument('--warmup', type=int, default=120)
    p.add_argument('--min-mean-fps', type=float)
    p.add_argument('--max-p99-ms', type=float)
    p.add_argument('--max-frame-ms', type=float)
    p.add_argument('--max-over-20-ms-fraction', type=float, default=.005)
    p.add_argument('--expected-gpu', default='RTX 5090', help='Substring of the selected adapter, not enumerated adapters')
    p.add_argument('--expected-dlss', choices=('any','native','dlaa','quality','balanced','performance'), default='any')
    a = p.parse_args(argv)
    version_tuple(a.expected_version)
    if not (320 <= a.width <= 16384 and 200 <= a.height <= 16384):
        p.error('Invalid capture dimensions')
    if a.warmup < 0 or a.frames <= max(120, a.warmup * 2):
        p.error('Frames must exceed 120 and twice Warmup')
    for value in (a.min_mean_fps,a.max_p99_ms,a.max_frame_ms):
        if value is not None and (not math.isfinite(value) or value <= 0):
            p.error('Performance limits must be finite positive values')
    if not 0 <= a.max_over_20_ms_fraction <= 1:
        p.error('MaxOver20MsFraction must be between 0 and 1')
    return a


def main(argv=None):
    a = parse_args(argv)
    if os.name != 'nt':
        raise RuntimeError('Native Windows required; no verification was run')
    root = a.project_dir.resolve(); archive = (a.archive_directory or root/'Saved/Packages/Win64').resolve()
    manifest_path = archive/'vroom-build.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    executable, failures = validate_manifest(manifest,root,archive,a.expected_version)
    if failures:
        raise RuntimeError('; '.join(failures))
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S')
    out = root/'Saved/Validation'/('v'+a.expected_version)/(stamp+'-windows'); out.mkdir(parents=True)
    budget = 1000/a.target_fps
    limits = dict(min_mean_fps=a.min_mean_fps or a.target_fps-1,
                  max_p99_ms=a.max_p99_ms or budget*1.08,
                  max_frame_ms=a.max_frame_ms or budget*2+.17,
                  max_over_20_ms_fraction=a.max_over_20_ms_fraction)
    summary = dict(version=a.expected_version,platform='Windows',date=datetime.now(timezone.utc).isoformat(),
                   source_build_manifest=manifest,executable=str(executable),cases=[],performance=[],
                   expected_gpu=a.expected_gpu,expected_dlss=a.expected_dlss,
                   release_limits=dict(performance=limits,motion=MOTION_LIMITS,target_fps=a.target_fps),
                   limitations=['Onscreen images still require human visual review.',
                                'DLSS effective mode is the game API result; no frame generation is integrated.'])
    def save():
        (out/'results.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    def gate(name, reasons):
        if reasons:
            summary.setdefault('failures',[]).append(dict(name=name,reasons=reasons));save()
            raise RuntimeError(name+': '+'; '.join(reasons))
    def run(name,flags,seconds=6,capture=True):
        log=out/(name+'.log'); png=out/(name+'.png')
        args=['-d3d12','-windowed','-ForceRes',f'-ResX={a.width}',f'-ResY={a.height}',
              '-VroomVerify','-NoSplash',f'-abslog={log}',f'-VroomExitAfter={seconds}']
        if capture:args += [f'-VroomCapture={png}','-VroomCaptureAfter=3']
        args += flags
        row=dict(name=name,arguments=args,offscreen=False,log=str(log));summary['cases'].append(row);save()
        try:
            with (out/(name+'-stdout.log')).open('w') as stream:
                proc=subprocess.run(unreal_command_line(executable,args),executable=str(executable),
                    cwd=executable.parent,stdout=stream,stderr=subprocess.STDOUT,timeout=seconds+120)
        except subprocess.TimeoutExpired:
            gate(name,['Native game process timed out; its process was stopped'])
        row['exit']=proc.returncode
        text=log.read_text(encoding='utf-8-sig',errors='replace') if log.exists() else ''
        checks=[line for line in text.splitlines() if 'VROOM: Verification ' in line]
        row['checks']=checks; row['runtime']=runtime_evidence(text)
        versions=re.findall(r'VROOM: Version ([\d.]+)',text)
        reasons=[]
        if proc.returncode:reasons.append('Nonzero game exit: '+str(proc.returncode))
        if not versions or version_tuple(versions[-1])!=version_tuple(a.expected_version):reasons.append('Runtime version missing/mismatched')
        if (not checks and '-ExitAfterCsvProfiling' not in flags) or any('FAIL' in c for c in checks):reasons.append('Missing or failed game verification')
        gpu=row['runtime']['selected_gpu']
        if not gpu or a.expected_gpu.lower() not in gpu.lower():reasons.append('Selected GPU missing or not '+a.expected_gpu)
        if row['runtime']['rhi']!='D3D12':reasons.append('D3D12 runtime not confirmed')
        mode=row['runtime']['dlss_game_reported_effective_mode']
        if mode is None:reasons.append('Effective graphics/DLSS state missing')
        if a.expected_dlss!='any' and mode!=a.expected_dlss:reasons.append('Effective DLSS mode differs from requested gate')
        if capture:
            if not png.is_file():reasons.append('Screenshot missing')
            else:
                data=png.read_bytes()
                if len(data)<24 or data[:8]!=b'\x89PNG\r\n\x1a\n':reasons.append('Invalid PNG')
                else:
                    size=struct.unpack('>II',data[16:24]);row.update(capture=str(png),size=list(size))
                    if size!=(a.width,a.height):reasons.append('Actual screenshot resolution differs from requested dimensions')
        gate(name,reasons);save();print(name+': PASS',flush=True);return log
    for name,steer in [('neutral',0),('small-right',.04),('mid-left',-.21),('right',.62),('left',-.62)]:
        run(name,['-VroomAutoDemo','-VroomStartDistance=350','-VroomStartSpeed=0',f'-VroomPreviewSteering={steer}'])
    run('cars',['-VroomAutoDemo','-VroomStartDistance=55','-VroomStartSpeed=0','-VroomChaseCamera'])
    run('options',['-VroomGraphicsPreview'])
    for scenario in ('slow','crash','tyre','scenery'):
        run('collision-'+scenario,['-VroomCollisionTest='+scenario,'-VroomChaseCamera'],seconds=8)
    profile_dirs={root/'Saved/Profiling/CSV',executable.parents[2]/'Saved/Profiling/CSV',
                  Path(os.environ['LOCALAPPDATA'])/'Vroom/Saved/Profiling/CSV'}
    def profiles():
        return {p.resolve() for directory in profile_dirs if directory.exists() for p in directory.glob('*.csv')}
    cases=[('performance',None,a.frames)] if a.profile else []
    if a.soak:cases += [(f'soak-track-{t+1}',t,max(a.frames,130*a.target_fps))for t in range(6)]
    for name,track,frames in cases:
        before=profiles()
        flags=['-VroomAutoDemo','-VroomStartDistance=0','-VroomStartSpeed=30',f'-csvCaptureFrames={frames}',
               '-csvGpuStats','-ExitAfterCsvProfiling','-VroomMotionTrace']
        if track is not None:flags += [f'-VroomTrack={track}','-VroomTourDemo']
        log=run(name,flags,seconds=int(frames/max(15,a.target_fps*.5))+90,capture=False)
        created=profiles()-before;gate(name,[] if len(created)==1 else ['Expected exactly one new native CSV profile'])
        path=created.pop();report=analyze_csv(path,a.warmup);metadata=csv_metadata(path)
        target=out/(name+'.csv');target.write_bytes(path.read_bytes());report['source']['path']=str(target)
        row=dict(name=name,track=track,report=report,metadata=metadata);summary['performance'].append(row)
        reasons=performance_failures(report,frames,limits)
        if report['gpu_time'] is None or report['game_thread_time'] is None:reasons.append('GPU/CPU timing counters missing')
        for axis,value in [('resx',a.width),('resy',a.height)]:
            raw=metadata.get('systemresolution.'+axis)
            if raw is None or raw.strip('"')!=str(value):reasons.append('Actual CSV output resolution unconfirmed: '+axis)
        gate(name,reasons)
        motion=analyze_motion(log,a.warmup);row['motion']=motion
        gate(name+'-camera',motion_failures(motion,min_samples=min(1000,frames-a.warmup)))
        save()
    summary['completed']=True;save();print('RESULTS: '+str(out/'results.json'))

if __name__=='__main__':
    try:main()
    except (ValueError,RuntimeError,KeyError,OSError) as error:
        print('VERIFICATION FAILED: '+str(error),file=sys.stderr);sys.exit(1)
