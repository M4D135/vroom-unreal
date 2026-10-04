#!/usr/bin/env python3
"""Install an explicitly validated Mac release and native launcher; retain rollback versions."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
from regression_gates import check_release_limits, performance_failures, motion_failures

ROOT = Path(__file__).resolve().parents[1]
LAUNCHER_IDENTIFIER = 'fr.estrasse.vroom.launcher'
GAME_IDENTIFIER = 'com.YourCompany.Vroom'


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


def check_validation(report, bundle, scope='full'):
    if (report.get('forced') or report.get('editor') or report.get('failures') or
            report.get('completed') is not True):
        raise ValueError('Only a successful packaged-game validation may be installed')
    if not re.fullmatch(r'[0-9]+(?:\.[0-9]+){2}', str(report.get('version', ''))):
        raise ValueError('Missing or invalid release version')
    version = tuple(map(int, report['version'].split('.')))
    if report.get('graphics_preset') == 'photorealism' and version < (0, 34, 0):
        raise ValueError('Photorealism profile requires version 0.34 or later')
    if scope not in {'full', 'targeted-gameplay'}:
        raise ValueError(f'Unknown validation scope: {scope}')
    if report.get('scope', 'full') != scope:
        raise ValueError('Report scope differs from the explicitly requested validation scope')
    if scope == 'targeted-gameplay':
        check_targeted_gameplay(report, version)
    else:
        check_full_gameplay(report, version)
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


def check_targeted_gameplay(report, version):
    """Explicit functional release gate; does not certify performance or endurance."""
    if version < (0, 35, 0) or report.get('platform') != 'Mac':
        raise ValueError('Targeted gameplay requires a Mac release >= 0.35.0')
    if (report.get('forced') is not False or report.get('editor') is not False or
            report.get('failures') != [] or report.get('performance_retested') is not False):
        raise ValueError('Targeted gameplay requires explicit successful non-forced declarations')
    if any(key in report for key in ('performance', 'soak_motion', 'motion')):
        raise ValueError('Targeted gameplay cannot reuse or ignore full performance measurements')
    required = {'cockpit-final', 'collision-crash', 'drift-expert', 'drift-keyboard',
                'start-mute', 'start-pause', 'pit-service', 'collision-rub',
                'start-sequence', 'drift-straight', 'traffic-overtake'}
    cases = report.get('cases')
    if not isinstance(cases, list) or any(not isinstance(case, dict) for case in cases):
        raise ValueError('Missing targeted gameplay cases')
    names = [case.get('name') for case in cases]
    if len(names) != len(required) or set(names) != required:
        raise ValueError('Targeted gameplay requires exactly eleven unique scenarios')
    for case in cases:
        if (type(case.get('exit')) is not int or case['exit'] != 0 or case.get('status') != 'PASS' or
                case.get('offscreen') is not False or case.get('foreground') is not True):
            raise ValueError(f'Targeted scenario must pass visibly in the foreground: {case["name"]}')


def check_full_gameplay(report, version):
    """Historical complete gate, retained for full validation and older rollback."""
    limits = check_release_limits(report, allow_legacy=version < (0, 34, 0))['performance']
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
        if row.get('expected_frames', 0) < minimum or performance_failures(row['report'], row['expected_frames'], limits):
            raise ValueError('Performance release gate failed')
    for row in report['soak_motion']:
        if row.get('complete') is not True or motion_failures(row['report'], min_samples=7000):
            raise ValueError('Incomplete lap or camera regression')
    if motion_failures(report['motion']):
        raise ValueError('Final camera regression gate failed')
    required = {'neutral', 'left', 'right', 'options', 'collision-slow', 'collision-crash', 'collision-tyre', 'collision-scenery',
                'progress-data', 'menu-flow', 'pit-service', 'drift-feedback'}
    # Preserve rollback to earlier fully validated releases; these scenarios
    # became mandatory with the 0.33 driving/start correction.
    if tuple(map(int, report['version'].split('.'))) >= (0, 33, 0):
        required.update({'offroad-left', 'offroad-right', 'start-sequence', 'start-restart', 'start-pause', 'start-mute'})
        required.update({'drift-unskilled', 'collision-rub', 'collision-rear'})
        required.update(f'track-audit-{track + 1}' for track in range(6))
    if tuple(map(int, report['version'].split('.'))) >= (0, 34, 0):
        required.update({'drift-expert', 'traffic-overtake'})
    if not required <= {case['name'] for case in report.get('cases', [])}:
        raise ValueError('Missing required visual/collision cases')
    if any(case.get('exit') != 0 or case.get('status') != 'PASS' for case in report['cases']):
        raise ValueError('A required game case failed or has no PASS result')


def path_exists(path):
    return path.exists() or path.is_symlink()


def check_app_destination(path, identifiers):
    """Never displace an unrelated app, even when its filename happens to match."""
    if not path_exists(path):
        return
    plist = path / 'Contents/Info.plist'
    try:
        identifier = plistlib.loads(plist.read_bytes()).get('CFBundleIdentifier')
    except (OSError, ValueError, plistlib.InvalidFileException):
        identifier = None
    if identifier not in identifiers:
        raise ValueError(f'Refusing to replace unrelated application: {path}')


def build_launcher(prepared, version):
    executable = prepared / 'Contents/MacOS/VroomLauncher'
    executable.parent.mkdir(parents=True)
    resources = prepared / 'Contents/Resources'
    resources.mkdir()
    shutil.copy2(ROOT / 'Build/Mac/Application.icns', resources / 'Application.icns')
    subprocess.run(['xcrun', 'clang', '-O2', '-fobjc-arc', '-Wno-deprecated-declarations',
                    '-framework', 'Cocoa', str(ROOT / 'Tools/mac_launcher.m'), '-o', str(executable)], check=True)
    (prepared / 'Contents/Info.plist').write_bytes(plistlib.dumps({
        'CFBundleIdentifier': LAUNCHER_IDENTIFIER, 'CFBundleExecutable': 'VroomLauncher',
        'CFBundleName': 'Vroom', 'CFBundleDisplayName': 'Vroom', 'CFBundlePackageType': 'APPL',
        'CFBundleIconFile': 'Application.icns',
        'CFBundleShortVersionString': version, 'CFBundleVersion': version,
        'LSUIElement': True, 'NSHighResolutionCapable': True,
    }))
    subprocess.run(['codesign', '--force', '--sign', '-', str(prepared)], check=True)


def activate_release(home, prepared, manifest):
    """Keep previous entry points recoverable; publish the manifest only after all are ready."""
    storage = home / 'Library/Application Support/Vroom'
    launcher = home / 'Applications/Vroom.app'
    desktop_app = home / 'Desktop/Vroom.app'
    desktop_command = home / 'Desktop/Vroom.command'
    # Recheck immediately before touching any entry point.
    check_app_destination(launcher, {LAUNCHER_IDENTIFIER})
    check_app_destination(desktop_app, {LAUNCHER_IDENTIFIER, GAME_IDENTIFIER})
    backups = storage / 'Launchers' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backups.mkdir(parents=True)
    replaced = []

    def replace(source, destination, backup_name):
        backup = backups / backup_name if path_exists(destination) else None
        if backup is not None:
            destination.rename(backup)
        # Record even a failed move so the old entry can be restored.
        replaced.append((destination, backup))
        source.rename(destination)

    pending = storage / 'active.json.tmp'
    try:
        with tempfile.TemporaryDirectory(prefix='.vroom-shortcuts-', dir=home / 'Desktop') as temporary:
            temporary = Path(temporary)
            shortcut = temporary / 'Vroom.app'
            shortcut.symlink_to(launcher, target_is_directory=True)
            command = temporary / 'Vroom.command'
            shutil.copy2(ROOT / 'Tools/play_latest.command', command)
            command.chmod(0o755)
            replace(prepared, launcher, 'Applications-Vroom.app')
            replace(shortcut, desktop_app, 'Desktop-Vroom.app')
            replace(command, desktop_command, 'Desktop-Vroom.command')
            active = storage / 'active.json'
            if active.exists():
                shutil.copy2(active, backups / 'active.json')
        pending.write_text(json.dumps(manifest, indent=2) + '\n')
        pending.replace(active)
    except Exception:
        pending.unlink(missing_ok=True)
        for destination, backup in reversed(replaced):
            if destination.is_symlink() or destination.is_file():
                destination.unlink()
            elif destination.is_dir():
                shutil.rmtree(destination)
            if backup is not None:
                backup.rename(destination)
        raise


def install_release(validation, bundle, home=None, scope='full'):
    home = Path.home() if home is None else Path(home)
    report = json.loads(validation.read_text())
    bundle = bundle.resolve()
    check_validation(report, bundle, scope=scope)
    applications = home / 'Applications'
    desktop = home / 'Desktop'
    # Reject conflicting apps before creating a release or touching the active one.
    check_app_destination(applications / 'Vroom.app', {LAUNCHER_IDENTIFIER})
    check_app_destination(desktop / 'Vroom.app', {LAUNCHER_IDENTIFIER, GAME_IDENTIFIER})
    storage = home / 'Library/Application Support/Vroom'
    releases = storage / 'Releases'
    artifact = hashlib.sha256(json.dumps([report['executable_sha256'], report['package_data_sha256']], sort_keys=True).encode()).hexdigest()
    release = releases / (report['version'] + '-' + artifact[:12])
    installed = release / 'Vroom.app'
    releases.mkdir(parents=True, exist_ok=True)
    if installed.exists():
        check_validation(report, installed, scope=scope)
    else:
        with tempfile.TemporaryDirectory(prefix='.install-', dir=releases) as temporary:
            stage = Path(temporary) / 'Vroom.app'
            shutil.copytree(bundle, stage, symlinks=True)
            check_validation(report, stage, scope=scope)
            release.mkdir(exist_ok=True)
            stage.rename(installed)
    stored_validation = release / 'validation.json'
    if stored_validation.exists():
        # Keep an already installed report immutable so an interrupted reinstall
        # cannot invalidate the currently active manifest's report digest.
        check_validation(json.loads(stored_validation.read_text()), installed, scope=scope)
    else:
        copy_validation_report(validation, stored_validation)
    applications.mkdir(exist_ok=True)
    desktop.mkdir(exist_ok=True)
    manifest = dict(version=report['version'], bundle=str(installed), validation_scope=scope,
                    executable_sha256=report['executable_sha256'], validation=str(release / 'validation.json'),
                    validation_sha256=sha256(release / 'validation.json'))
    with tempfile.TemporaryDirectory(prefix='.vroom-launcher-', dir=applications) as temporary:
        prepared = Path(temporary) / 'Vroom.app'
        build_launcher(prepared, report['version'])
        activate_release(home, prepared, manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validation', required=True, type=Path)
    parser.add_argument('--bundle', type=Path, default=ROOT / 'Saved/Packages/Mac/Vroom.app')
    parser.add_argument('--validation-scope', choices=('full', 'targeted-gameplay'), default='full',
                        help='Explicit targeted gameplay gate; full remains the default')
    args = parser.parse_args()
    manifest = install_release(args.validation, args.bundle, scope=args.validation_scope)
    print(f"Installed Vroom {manifest['version']}: {Path.home() / 'Applications/Vroom.app'}\n"
          f"Desktop: {Path.home() / 'Desktop/Vroom.app'}\n"
          f"Manifest: {Path.home() / 'Library/Application Support/Vroom/active.json'}")


if __name__ == '__main__':
    main()
