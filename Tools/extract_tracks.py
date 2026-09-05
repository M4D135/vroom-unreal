#!/usr/bin/env python3
"""Extract preserved July 1991 Vroom track facts and a provisional 3D preview.

Only Python's standard library is required. No original program is executed.
The preview is an approximation reconstructed from the small HUD map, not the
original road projection or calibrated vehicle-space geometry.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

ARCHIVE_URL = "https://www.oldies-but-goodies.fr/games/vroom/download/Atari%20ST/Vroom-ST-sources-juillet-91.zip"
ARCHIVE_SHA256 = "cf97c64b7950d7ffdb115b9dc49167f9a6cec1da51738f276e5a26d7db09efda"
NAMES = {1: "Autriche", 2: "France", 3: "USA", 4: "Suède", 5: "Japon", 6: "Royaume-Uni"}
TRACK_NAMES = {1: "Zeltweg", 2: "Le Castellet", 3: "Watkins Glen", 4: "Anderstorp", 5: "Mont Fuji", 6: "Silverstone"}
CALL = re.compile(r"^\s+(MTOP|TOP)\s+(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)")
YX = re.compile(r"^\s+YX\s+(-?\d+)\s*,\s*(-?\d+)")


def preview_from_hud(points: list[list[float]], length_m: float):
    # Pixel duplicates encode progress in the original HUD; keep them in the
    # source data but remove consecutive duplicates in the geometry preview.
    unique = []
    for point in points:
        if not unique or point != unique[-1]:
            unique.append(point)
    if len(unique) > 1 and unique[0] == unique[-1]:
        unique.pop()
    if len(unique) < 3:
        raise ValueError("A closed preview requires at least three unique HUD points")
    pixel_length = sum(math.dist(p, unique[(i + 1) % len(unique)]) for i, p in enumerate(unique))
    scale = length_m / pixel_length
    origin = unique[0]
    # Deliberately retain the map's coordinate handedness and jagged outline.
    # Unreal may interpolate this loop for display, which is also provisional.
    output = [[round((p[0] - origin[0]) * scale, 6), round((p[1] - origin[1]) * scale, 6), 0.0] for p in unique]
    return output, scale


def extract_track(path: Path, track_id: int):
    raw = path.read_bytes()
    try:
        content = raw.decode("utf-8")
    except UnicodeDecodeError:
        content = raw.decode("latin1")
    lines = content.splitlines()
    lap_match = re.search(r"^CLAP\s+DC\.L\s+(\d+)", content, re.MULTILINE)
    scale_match = re.search(r"^CSCA\s+DC\.W\s+(\d+)/(\d+)", content, re.MULTILINE)
    if not lap_match or not scale_match:
        raise ValueError(f"Missing CLAP or CSCA in {path}")
    length_cm = int(lap_match[1])
    displacement_cm = 0
    events = []
    hud = []
    end_marker_cm = None
    for line_no, line in enumerate(lines, 1):
        if line.lstrip().startswith("*"):
            continue
        match = CALL.match(line)
        if match:
            macro = match[1]
            raw_curve, action, distance_cm, unused, flags = map(int, match.groups()[1:])
            if macro == "MTOP":
                displacement_cm += distance_cm
                position_cm = displacement_cm
            else:
                position_cm = displacement_cm + distance_cm
            event = {"source_line": line_no, "macro": macro, "s_m": position_cm / 100.0,
                     "distance_argument_cm": distance_cm, "raw_arguments": [raw_curve, action, distance_cm, unused, flags],
                     "raw_curve_index": raw_curve, "action_byte": ((32 * flags) | action) & 255}
            if raw_curve == -1 and action == -1:
                event["type"] = "end"
                end_marker_cm = position_cm
            else:
                event["type"] = {0: "curve", 1: "relief", 2: "vertical_object", 3: "lanes"}[action & 3]
                if event["type"] == "curve":
                    direction = action & 12
                    if direction not in (0, 4, 8):
                        raise ValueError(f"Unknown curve action {path}:{line_no}")
                    event["direction"] = {0: "straight", 4: "right", 8: "left"}[direction]
                    event["signed_curve_type"] = 0 if direction == 0 else (raw_curve + 1) * (1 if direction == 4 else -1)
                    event["curvature_per_m"] = 0.0 if direction == 0 else None
                elif event["type"] == "relief":
                    event["direction"] = {1: "flat", 5: "up", 9: "down"}.get(action & 31, "unknown")
                    event["slope"] = None
                    event["altitude_m"] = None
                elif event["type"] == "lanes":
                    event["lanes"] = {15: 3, 31: 2, 11: 1, 27: 2}.get(action & 31)
            events.append(event)
        match = YX.match(line)
        if match:
            row_offset, x_pixel = map(int, match.groups())
            if row_offset < 0:
                continue
            if row_offset % 160 != 0:
                raise ValueError(f"Unexpected HUD row offset {path}:{line_no}")
            hud.append({"source_line": line_no, "x_px": x_pixel, "y_px": row_offset / 160.0})
    if end_marker_cm != length_cm:
        raise ValueError(f"MTOP cumulative length {end_marker_cm} != CLAP {length_cm}: {path}")
    if any(e["s_m"] < 0 or e["s_m"] > length_cm / 100.0 for e in events):
        raise ValueError(f"Out-of-bounds topology event: {path}")
    preview, preview_scale = preview_from_hud([[p["x_px"], p["y_px"]] for p in hud], length_cm / 100.0)
    return {"id": track_id, "name": NAMES[track_id], "circuit_name": TRACK_NAMES[track_id],
            "length_m": length_cm / 100.0, "source_file": path.name,
            "source_file_sha256": hashlib.sha256(raw).hexdigest(), "source_header": lines[0].strip(),
            "events": events, "hud_map_points": hud,
            "hud_progress_scale_cm_integer": int(scale_match[1]) // int(scale_match[2]),
            "hud_progress_scale_expression": f"{scale_match[1]}/{scale_match[2]}",
            "preview_points_m": preview, "preview_closed_loop": True,
            "preview_meters_per_hud_pixel": preview_scale,
            "preview_status": "Provisional: uniformly scaled HUD-map polyline, uncalibrated pixel aspect, z=0, implicit last-to-first edge. No original road curvature or altitude reconstructed.",
            "validation": {"cumulative_mtop_matches_clap": True, "hud_point_count": len(hud), "preview_point_count": len(preview)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = {"schema_version": 1, "tracks": [extract_track(args.source_dir / f"cr{i}.asm", i) for i in range(1, 7)],
              "provenance": {"archive_url": ARCHIVE_URL, "archive_sha256": ARCHIVE_SHA256,
                             "catalog_url": "https://www.oldies-but-goodies.fr/test/314",
                             "snapshot": "V15, July 1991 development source snapshot; final-release parity unverified",
                             "track_id_order": "Source-file number, not championship order",
                             "manual_url": "https://www.lankhor.net/jeux/manuel/manuel32-1.pdf"},
              "semantics": {"events": "Preserves source declaration order, including TOP events whose positions may overlap following MTOP blocks; sort by s_m only for downstream lookup.",
                            "curves": "Source index 0..7; sub1.asm adds one, then negates for left. Comment at line 796: 1 tight, 8 wide. Physical inverse-meter curvature remains unknown.",
                            "preview": "Display aid only. Each distinct HUD pixel is a vertex; closed polyline length uniformly scaled to CLAP. Duplicate progress samples preserved separately. No smoothing or invented height data."}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for track in result["tracks"]:
        print(f"{track['id']} {track['name']}: {track['length_m']:.2f} m, {len(track['events'])} events, {len(track['preview_points_m'])} preview vertices")


if __name__ == "__main__":
    main()
