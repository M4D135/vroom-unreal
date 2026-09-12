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
# A documented preview calibration, not a conversion recovered from the ST.
ELEVATION_GRADE_PER_RAW_UNIT = 1.0 / 150000.0
ELEVATION_SAMPLE_SPACING_M = 5.0


def elevation_from_events(events: list[dict], length_m: float,
                          grade_scale: float = ELEVATION_GRADE_PER_RAW_UNIT,
                          sample_spacing_m: float = ELEVATION_SAMPLE_SPACING_M):
    """Integrate the ST inclination accumulator and derive a closed 3D preview.

    sub6.asm adds signed F1DEP << (VMSUP-vamo) to degre. F1DEP is
    longitudinal displacement in cm and VMSUP is 5. Thus Up/Down change
    inclination with distance; Flat retains the current inclination.
    Height in meters requires an explicitly provisional calibration and a
    constant grade offset to close the loop. Exact knot tangents are exported
    for piecewise Hermite interpolation, including the periodic seam.
    """
    if not math.isfinite(length_m) or length_m <= 0:
        raise ValueError("Elevation requires a finite, positive lap length")
    if not math.isfinite(grade_scale) or grade_scale <= 0:
        raise ValueError("Elevation grade calibration must be finite and positive")
    if not math.isfinite(sample_spacing_m) or sample_spacing_m <= 0:
        raise ValueError("Elevation sample spacing must be finite and positive")
    relief = sorted((e for e in events if e["type"] == "relief"), key=lambda e: e["s_m"])
    source = {"status": "inclination_reconstructed" if relief else "no_relief_events_in_snapshot",
              "event_count": len(relief), "initial_inclination_raw": 0.0,
              "inclination_formula": "d(degre)/ds_m = direction_sign * 100 * 2^(5 - raw_index)",
              "inclination_units": "ST degre accumulator units; not degrees or radians",
              "segments": []}
    if not relief:
        return source, {"status": "flat_fallback_no_relief_events", "samples_m": [],
                        "grades_m_per_m": [], "closed_loop": True}
    for event in relief:
        if not math.isfinite(event["s_m"]) or not 0 <= event["s_m"] <= length_m:
            raise ValueError("Relief event outside the lap")
        if event["direction"] not in ("flat", "up", "down"):
            raise ValueError("Unknown relief direction")
        if not 0 <= event["raw_curve_index"] <= 5:
            raise ValueError("Relief shift outside the decoded ST index range 0..5")

    position = inclination = height_integral = rate = 0.0
    direction, index, line = "flat", None, None
    segments = source["segments"]

    def append_segment(end: float):
        nonlocal position, inclination, height_integral
        distance = end - position
        if distance <= 0:
            return
        next_inclination = inclination + rate * distance
        next_integral = height_integral + inclination * distance + 0.5 * rate * distance * distance
        segments.append({"s_start_m": position, "s_end_m": end, "duration_m": distance,
                         "source_line": line, "direction": direction, "raw_index": index,
                         "inclination_rate_raw_per_m": rate,
                         "inclination_start_raw": inclination, "inclination_end_raw": next_inclination,
                         "height_integral_start_raw_m": height_integral,
                         "height_integral_end_raw_m": next_integral})
        position, inclination, height_integral = end, next_inclination, next_integral

    for event in relief:
        append_segment(event["s_m"])
        direction, index, line = event["direction"], event["raw_curve_index"], event["source_line"]
        rate = {"flat": 0, "up": 1, "down": -1}[direction] * 100.0 * 2.0 ** (5 - index)
    append_segment(length_m)
    # Subtracting a constant inclination can close height, but cannot repair
    # a discontinuous seam tangent. Do not invent another correction for it.
    if not math.isclose(inclination, 0.0, abs_tol=1e-6):
        raise ValueError("Source inclination does not close; 3D elevation requires further investigation")
    mean_inclination = height_integral / length_m
    source.update({"final_inclination_raw": inclination,
                   "height_integral_per_lap_raw_m": height_integral,
                   "inclination_closes": True,
                   "height_integral_closes": math.isclose(height_integral, 0.0, abs_tol=1e-6)})

    # Include every event boundary: with the exact tangents, each Hermite
    # interval reproduces the integrated quadratic instead of rounding bumps.
    positions = {0.0, length_m}
    positions.update(e["s_m"] for e in relief)
    positions.update(i * sample_spacing_m for i in range(1, math.ceil(length_m / sample_spacing_m)))
    samples, grades = [], []
    segment_index = 0
    for s in sorted(positions):
        while segment_index + 1 < len(segments) and s > segments[segment_index]["s_end_m"]:
            segment_index += 1
        segment = segments[segment_index]
        ds = s - segment["s_start_m"]
        raw_grade = segment["inclination_start_raw"] + segment["inclination_rate_raw_per_m"] * ds
        raw_height = (segment["height_integral_start_raw_m"] + segment["inclination_start_raw"] * ds
                      + 0.5 * segment["inclination_rate_raw_per_m"] * ds * ds)
        z = grade_scale * (raw_height - mean_inclination * s)
        samples.append([round(s, 8), round(z, 8)])
        grades.append(round(grade_scale * (raw_grade - mean_inclination), 12))
    samples[0][1] = samples[-1][1] = 0.0
    grades[-1] = grades[0]
    preview = {"status": "source_inclination_with_calibrated_scale_and_constant_grade_closure",
               "closed_loop": True, "sample_spacing_m": sample_spacing_m,
               "sampling": "regular grid plus every source relief boundary and lap end",
               "grade_per_raw_inclination_unit": grade_scale,
               "closure_inclination_offset_raw": -mean_inclination,
               "closure_grade_offset_m_per_m": -mean_inclination * grade_scale,
               "height_drift_before_closure_m": height_integral * grade_scale,
               "interpolation": "periodic cubic Hermite; grades_m_per_m supplies dz/ds at each samples_m knot",
               "samples_m": samples, "grades_m_per_m": grades,
               "min_sample_height_m": min(p[1] for p in samples),
               "max_sample_height_m": max(p[1] for p in samples),
               "min_grade_m_per_m": min(grades), "max_grade_m_per_m": max(grades)}
    return source, preview


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
                    event["direction_semantics"] = "change of inclination with distance; flat keeps the current inclination"
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
    source_elevation, preview_elevation = elevation_from_events(events, length_cm / 100.0)
    return {"id": track_id, "name": NAMES[track_id], "circuit_name": TRACK_NAMES[track_id],
            "length_m": length_cm / 100.0, "source_file": path.name,
            "source_file_sha256": hashlib.sha256(raw).hexdigest(), "source_header": lines[0].strip(),
            "events": events, "hud_map_points": hud,
            "hud_progress_scale_cm_integer": int(scale_match[1]) // int(scale_match[2]),
            "hud_progress_scale_expression": f"{scale_match[1]}/{scale_match[2]}",
            "preview_points_m": preview, "preview_closed_loop": True,
            "source_elevation": source_elevation, "preview_elevation": preview_elevation,
            "preview_meters_per_hud_pixel": preview_scale,
            "preview_status": "Provisional: uniformly scaled HUD-map XY polyline, uncalibrated pixel aspect, z=0, implicit last-to-first edge. Elevation is a separate calibrated profile when source relief events exist; original physical curvature and altitude remain unverified.",
            "validation": {"cumulative_mtop_matches_clap": True, "hud_point_count": len(hud), "preview_point_count": len(preview)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = {"schema_version": 2, "tracks": [extract_track(args.source_dir / f"cr{i}.asm", i) for i in range(1, 7)],
              "provenance": {"archive_url": ARCHIVE_URL, "archive_sha256": ARCHIVE_SHA256,
                             "catalog_url": "https://www.oldies-but-goodies.fr/test/314",
                             "snapshot": "V15, July 1991 development source snapshot; final-release parity unverified",
                             "track_id_order": "Source-file number, not championship order",
                             "manual_url": "https://www.lankhor.net/jeux/manuel/manuel32-1.pdf"},
              "semantics": {"events": "Preserves source declaration order, including TOP events whose positions may overlap following MTOP blocks; sort by s_m only for downstream lookup.",
                            "curves": "Source index 0..7; sub1.asm adds one, then negates for left. Comment at line 796: 1 tight, 8 wide. Physical inverse-meter curvature remains unknown.",
                            "preview": "Display aid only. Each distinct HUD pixel is a vertex; closed XY polyline length uniformly scaled to CLAP. Duplicate progress samples preserved separately. Independent elevation uses recovered inclination events with an explicit physical calibration and constant grade closure.",
                            "elevation": "sub6.asm: VMSUP=5; degre accumulates signed displacement_cm shifted by 5-index. Flat stops inclination change, not inclination itself. Only cr1.asm supplies relief in this snapshot. See docs/ELEVATION.md for integration, scale and closure adaptations."}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for track in result["tracks"]:
        print(f"{track['id']} {track['name']}: {track['length_m']:.2f} m, {len(track['events'])} events, {len(track['preview_points_m'])} preview vertices")


if __name__ == "__main__":
    main()
