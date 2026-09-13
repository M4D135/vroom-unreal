#!/usr/bin/env python3
"""Recheck preserved source facts and expose adaptation gaps. Does not emulate Amiga."""
import argparse
import hashlib
import json
from pathlib import Path
from extract_tracks import extract_track

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/reference-comparison.json')
    args = parser.parse_args()
    current = json.loads((ROOT / 'Content/Data/tracks.json').read_text())
    report = {'schema': 1, 'original_executed': False, 'source_kind': 'preserved July 1991 development snapshot',
              'reference_edition': 'Vroom Amiga; exact executable comparison pending', 'tracks': [], 'steering': []}
    for index, stored in enumerate(current['tracks'], 1):
        path = args.source / f'cr{index}.asm'
        extracted = extract_track(path, index)
        equal = all(extracted[key] == stored[key] for key in ('length_m', 'events', 'hud_map_points'))
        report['tracks'].append({'id': index, 'name': stored['name'], 'length_m': stored['length_m'],
                                 'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                                 'source_facts_match': equal,
                                 'geometry_status': 'smoothed HUD map reconstruction; original corner radii unverified'})
        if not equal:
            raise ValueError(f'Track {index} differs from the preserved source facts')
    source = args.source / 'vr2.asm'
    report['driving_source_sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
    report['driving_source_sections'] = {'arcade_steering': 'XBCMP3..XBCMP4A', 'tyre_braking': 'SPEDC0..SPEDC1'}
    # Magnitudes after converting original XB units to metres and PAL VBL to seconds.
    # Signs differ between source XB and Unreal lateral coordinates. We compare magnitudes.
    for speed in (40, 80):
        for steer in (.25, .62, 1):
            original = steer * 271 * (speed * 5.4) / 2048 * 50 / 256
            report['steering'].append({'speed_kmh': speed * 3.6, 'steer_fraction': steer,
                                       'source_unquantized_mps': round(original, 6),
                                       'modern_steady_mps': round(original * 1.12, 6),
                                       'modern_gain_percent': 12})
    report['tyre_braking'] = [{'health_fraction': condition,
                              'source_brake_factor': (condition + 1) / 2,
                              'modern_brake_factor': .65 + .35 * condition}
                             for condition in (1, .5, 0)]
    report['repeatable_original_capture_protocol'] = [
        {'case': 'neutral', 'duration_seconds': 10, 'commands': 'straight, full throttle, automatic'},
        {'case': 'steering', 'duration_seconds': 3, 'commands': '144 km/h: neutral .5s, right62% 1s, neutral .5s, left62% 1s'},
        {'case': 'curve', 'track': 'Japan', 'commands': 'record first right corner entry/exit distances, speed, wheel pose, F10 state'},
        {'case': 'crash', 'commands': 'same track obstacle at 80/160 km/h: impact, tyre loss, recovery duration, reset position'},
        {'case': 'pit', 'commands': 'stop at box; tyre then fuel service; compare lock and elapsed race time'},
    ]
    report['remaining_evidence'] = ['No Amiga emulator or configured Kickstart found on this Mac.',
                                    'Original real-time footage with matching input timing is still required.',
                                    'Snapshot source checks do not prove retail Amiga behavior.',
                                    'Boost drift, modern AI, scenery and three-lap format are deliberate adaptations.']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print(f'Source checks: six circuits match. Adaptation deltas written to {args.output}. Original execution: not performed.')


if __name__ == '__main__':
    main()
