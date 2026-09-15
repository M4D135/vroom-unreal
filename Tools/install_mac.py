#!/usr/bin/env python3
"""Install an explicitly validated Mac release and native launcher; retain rollback versions."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
from regression_gates import performance_failures, motion_failures

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def package_data_paths(bundle):
    """Return every packaged data file using paths relative to the app bundle."""
    return {str(path.relative_to(bundle))
            for directory in ('Paks', 'Data')
            for path in (bundle / 'Contents/UE/Vroom/Content' / directory).rglob('*')
            if path.is_file()}


def copy_validation_report(source, destination):
    # A rollback uses the report already stored beside its installed bundle.
    if Path(source).resolve() != Path(destination).resolve():
        shutil.copy2(source, destination)


def check_validation(report, bundle):
    if report.get('editor') or report.get('failures') or report.get('completed') is not True:
        raise ValueError('Only a successful packaged-game validation may be installed')
    if len(report.get('soak_motion', [])) != 6 or len(report.get('performance', [])) < 8:
        raise ValueError('Missing six-circuit soak or both resolution profiles')
    if {row['track'] for row in report['soak_motion']} != set(range(6)) or not report.get('motion'):
        raise ValueError('Missing circuit coverage or final camera regression gate')
    expected_profiles = {'perf-1440': ([1440, 900], None, 1800),
                         'perf-2560': ([2560, 1664], None, 1800)}
    expected_profiles.update({f'soak-track-{track + 1}': ([1440, 900], track, 7800)
                              for track in range(6)})
    profiles = {row.get('name'): row for row in report['performance']}
    if len(profiles) != len(report['performance']):
        raise ValueError('Duplicate performance profile names')
    for name, (resolution, track, frames) in expected_profiles.items():
        row = profiles.get(name)
        if (row is None or row.get('resolution') != resolution or row.get('track') != track or
                row.get('expected_frames', 0) < frames or
                (track is None and row.get('expected_frames') != frames)):
            raise ValueError(f'Missing or incorrect required performance profile: {name}')
    for row in report['performance']:
        minimum = 7800 if row['track'] is not None else 1800
        if row.get('expected_frames', 0) < minimum or performance_failures(row['report'], row['expected_frames']):
            raise ValueError('Performance release gate failed')
    for row in report['soak_motion']:
        if row.get('complete') is not True or motion_failures(row['report'], min_samples=7000):
            raise ValueError('Incomplete lap or camera regression')
    if motion_failures(report['motion']):
        raise ValueError('Final camera regression gate failed')
    required = {'neutral', 'left', 'right', 'options', 'collision-slow', 'collision-crash', 'collision-tyre', 'collision-scenery',
                'progress-data', 'menu-flow', 'pit-service', 'drift-feedback'}
    if not required <= {case['name'] for case in report.get('cases', [])}:
        raise ValueError('Missing required visual/collision cases')
    if any(case.get('exit') != 0 for case in report['cases']):
        raise ValueError('A required game process failed')
    expected = report.get('executable_sha256')
    if not expected or sha256(bundle / 'Contents/MacOS/Vroom') != expected:
        raise ValueError('Binary differs from the validated artifact')
    paks = bundle / 'Contents/UE/Vroom/Content/Paks'
    if not paks.is_dir() or not list(paks.glob('*.ucas')):
        raise ValueError('Incomplete packaged game: cooked data missing')
    if not report.get('package_data_sha256'):
        raise ValueError('Missing packaged data fingerprints')
    if package_data_paths(bundle) != set(report['package_data_sha256']):
        raise ValueError('Packaged data file list differs from validation')
    for relative, digest in report['package_data_sha256'].items():
        if sha256(bundle / relative) != digest:
            raise ValueError(f'Packaged data differs from validation: {relative}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validation', required=True, type=Path)
    parser.add_argument('--bundle', type=Path, default=ROOT / 'Saved/Packages/Mac/Vroom.app')
    parser.add_argument('--force', action='store_true',
                        help='install this bundle even if soak/perf gates failed')
    args = parser.parse_args()
    report = json.loads(args.validation.read_text())
    bundle = args.bundle.resolve()
    if args.force:
        print('WARNING: installing without a completed soak (--force).', flush=True)
        if not (bundle / 'Contents/MacOS/Vroom').is_file():
            raise ValueError('Package executable missing')
        paks = bundle / 'Contents/UE/Vroom/Content/Paks'
        if not paks.is_dir() or not list(paks.glob('*.ucas')):
            raise ValueError('Incomplete packaged game: cooked data missing')
        report['executable_sha256'] = sha256(bundle / 'Contents/MacOS/Vroom')
        report['package_data_sha256'] = {path: sha256(bundle / path) for path in sorted(package_data_paths(bundle))}
        report['forced'] = True
    else:
        check_validation(report, bundle)
    storage = Path.home() / 'Library/Application Support/Vroom'
    releases = storage / 'Releases'
    artifact = hashlib.sha256(json.dumps([report['executable_sha256'], report.get('package_data_sha256')], sort_keys=True).encode()).hexdigest()
    release = releases / (report['version'] + '-' + artifact[:12])
    installed = release / 'Vroom.app'
    releases.mkdir(parents=True, exist_ok=True)
    if installed.exists():
        if not args.force:
            check_validation(report, installed)
    else:
        with tempfile.TemporaryDirectory(prefix='.install-', dir=releases) as temporary:
            stage = Path(temporary) / 'Vroom.app'
            shutil.copytree(bundle, stage, symlinks=True)
            if not args.force:
                check_validation(report, stage)
            release.mkdir(exist_ok=True)
            stage.rename(installed)
    copy_validation_report(args.validation, release / 'validation.json')
    applications = Path.home() / 'Applications'
    applications.mkdir(exist_ok=True)
    launcher = applications / 'Vroom.app'
    identifier = 'fr.estrasse.vroom.launcher'
    if launcher.exists():
        plist = launcher / 'Contents/Info.plist'
        if not plist.is_file() or plistlib.loads(plist.read_bytes()).get('CFBundleIdentifier') != identifier:
            raise ValueError(f'Refusing to replace unrelated application: {launcher}')
    with tempfile.TemporaryDirectory(prefix='.vroom-launcher-', dir=applications) as temporary:
        prepared = Path(temporary) / 'Vroom.app'
        executable = prepared / 'Contents/MacOS/VroomLauncher'
        executable.parent.mkdir(parents=True)
        subprocess.run(['xcrun', 'clang', '-O2', '-fobjc-arc', '-Wno-deprecated-declarations',
                        '-framework', 'Cocoa', str(ROOT / 'Tools/mac_launcher.m'), '-o', str(executable)], check=True)
        (prepared / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleIdentifier': identifier, 'CFBundleExecutable': 'VroomLauncher',
            'CFBundleName': 'Vroom', 'CFBundleDisplayName': 'Vroom', 'CFBundlePackageType': 'APPL',
            'CFBundleShortVersionString': report['version'], 'CFBundleVersion': report['version'],
            'LSUIElement': True, 'NSHighResolutionCapable': True,
        }))
        subprocess.run(['codesign', '--force', '--sign', '-', str(prepared)], check=True)
        if launcher.exists():
            backups = storage / 'Launchers'
            backups.mkdir(exist_ok=True)
            launcher.rename(backups / (datetime.now().strftime('%Y%m%d-%H%M%S') + '.app'))
        prepared.rename(launcher)
    manifest = dict(version=report['version'], bundle=str(installed),
                    executable_sha256=report['executable_sha256'], validation=str(release / 'validation.json'))
    pending = storage / 'active.json.tmp'
    pending.write_text(json.dumps(manifest, indent=2) + '\n')
    pending.replace(storage / 'active.json')
    desktop = Path.home() / 'Desktop/Vroom.command'
    shutil.copy2(ROOT / 'Tools/play_latest.command', desktop)
    desktop.chmod(0o755)
    print(f"Installed Vroom {report['version']}: {launcher}\nManifest: {storage / 'active.json'}")


if __name__ == '__main__':
    main()
