#!/usr/bin/env python3
"""Summarize an Unreal CSV capture and optional VROOM-MOTION log as JSON.

Times in Unreal's FrameTime/GPUTime/GameThreadTime columns are milliseconds.
Warmup removes the first N numeric frames (default 120), never metadata rows.
Motion yaw is degrees; its wrapped differences are normalized to [-180, 180).
CSV and motion warmups are independent: this tool does not time-align captures.
Only the Python standard library is required.
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import statistics
import sys


def source_info(path: Path) -> dict:
    stat = path.stat()
    return {
        "path": str(path.resolve()),
        "size_bytes": stat.st_size,
        "modified_utc": datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(),
    }


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def timing_stats(values: list[float]) -> dict | None:
    if not values:
        return None
    return {
        "samples": len(values),
        "mean_ms": statistics.fmean(values),
        "p95_ms": percentile(values, 0.95),
        "p99_ms": percentile(values, 0.99),
        "max_ms": max(values),
        "over_20_ms_count": sum(value > 20 for value in values),
        "over_40_ms_count": sum(value > 40 for value in values),
    }


def motion_stats(values: list[float]) -> dict | None:
    if not values:
        return None
    absolute = [abs(value) for value in values]
    return {
        "samples": len(values),
        "min_signed": min(values),
        "max_signed": max(values),
        "mean_absolute": statistics.fmean(absolute),
        "p95_absolute": percentile(absolute, 0.95),
        "p99_absolute": percentile(absolute, 0.99),
        "max_absolute": max(absolute),
    }


def is_csv_metadata(row: list[str]) -> bool:
    # Unreal writes alternating [key],value pairs after the repeated header.
    # A value such as [streamingpoolsizemb],600 may occupy the FrameTime index.
    # Check the whole structure, not just a bracketed EVENTS field, and do this
    # before looking for a header or counting numeric frames for warmup.
    return bool(row) and len(row) % 2 == 0 and all(
        re.fullmatch(r"\[[^\[\]]+\]", key.strip()) for key in row[::2]
    )


def analyze_csv(path: Path, warmup: int) -> dict:
    # Unreal EVENTS can exceed csv's default 128-KiB field limit.
    csv.field_size_limit(64 * 1024 * 1024)
    columns = ("FrameTime", "GPUTime", "GameThreadTime")
    values: dict[str, list[float]] = {column: [] for column in columns}
    invalid_metrics = {column: 0 for column in columns}
    indices: dict[str, int] = {}
    numeric_frames = non_data_rows = invalid_frames = discarded_warmup = 0
    with path.open(encoding="utf-8-sig", errors="replace", newline="") as stream:
        for row in csv.reader(stream):
            if is_csv_metadata(row):
                non_data_rows += 1
                continue
            if "FrameTime" in row:
                if not indices:
                    indices = {column: row.index(column) for column in columns if column in row}
                else:
                    non_data_rows += 1  # Unreal repeats its header at the end.
                continue
            if not indices or len(row) <= indices["FrameTime"]:
                non_data_rows += 1
                continue
            try:
                frame_time = float(row[indices["FrameTime"]])
            except ValueError:
                non_data_rows += 1
                continue
            numeric_frames += 1
            if numeric_frames <= warmup:
                discarded_warmup += 1
                continue
            if not math.isfinite(frame_time) or frame_time <= 0:
                invalid_frames += 1
                invalid_metrics["FrameTime"] += 1
                continue
            values["FrameTime"].append(frame_time)
            for column in columns[1:]:
                if column not in indices:
                    continue
                try:
                    value = float(row[indices[column]])
                except (ValueError, IndexError):
                    invalid_metrics[column] += 1
                    continue
                if not math.isfinite(value) or value < 0:
                    invalid_metrics[column] += 1
                    continue
                values[column].append(value)
    if not indices:
        raise ValueError(f"No FrameTime header found in {path}")
    if not values["FrameTime"]:
        raise ValueError(f"No valid frames remain after --warmup {warmup} in {path}")
    mean_frame_time = statistics.fmean(values["FrameTime"])
    return {
        "source": source_info(path),
        "warmup_requested_frames": warmup,
        "warmup_discarded_frames": discarded_warmup,
        "numeric_frame_rows": numeric_frames,
        "non_data_rows_skipped": non_data_rows,
        "invalid_frames_after_warmup": invalid_frames,
        "invalid_metric_samples_after_warmup": invalid_metrics,
        "missing_columns": [column for column in columns if column not in indices],
        "analyzed_frames": len(values["FrameTime"]),
        "analyzed_duration_seconds": math.fsum(values["FrameTime"]) / 1000,
        "mean_fps": 1000 / mean_frame_time,
        "frame_time": timing_stats(values["FrameTime"]),
        "gpu_time": timing_stats(values["GPUTime"]),
        "game_thread_time": timing_stats(values["GameThreadTime"]),
    }


def analyze_motion(path: Path, warmup: int) -> dict:
    pairs = re.compile(r"([A-Za-z][A-Za-z0-9_]*)=([^\s]+)")
    trace_samples = analyzed_samples = malformed = nonfinite = nonpositive_dt = 0
    invalid_lines: list[int] = []
    backsteps: list[dict] = []
    backstep_count = 0
    largest_backstep = 0.0
    previous: dict[str, float] | None = None
    previous_rate: float | None = None
    previous_interval: float | None = None
    yaw_deltas: list[float] = []
    yaw_rates: list[float] = []
    yaw_accelerations: list[float] = []
    peak_acceleration: dict | None = None
    with path.open(encoding="utf-8-sig", errors="replace") as stream:
        for line_number, line in enumerate(stream, 1):
            if "VROOM-MOTION " not in line:
                continue
            trace_samples += 1
            if trace_samples <= warmup:
                continue
            payload = line.split("VROOM-MOTION ", 1)[1]
            try:
                sample = {name: float(value) for name, value in pairs.findall(payload)}
                if not {"dt", "s", "yaw"} <= sample.keys():
                    raise ValueError("missing dt, s or yaw")
            except ValueError:
                malformed += 1
                sample = None
            if sample is not None and not all(math.isfinite(value) for value in sample.values()):
                nonfinite += 1
                sample = None
            if sample is not None and sample["dt"] <= 0:
                nonpositive_dt += 1
                sample = None
            if sample is None:
                if len(invalid_lines) < 10:
                    invalid_lines.append(line_number)
                # Never bridge bad telemetry to invent a velocity/acceleration.
                previous = previous_rate = previous_interval = None
                continue
            analyzed_samples += 1
            if previous is not None:
                distance_delta = sample["s"] - previous["s"]
                if distance_delta < -1e-6:
                    backstep_count += 1
                    largest_backstep = max(largest_backstep, -distance_delta)
                    if len(backsteps) < 10:
                        backsteps.append({"line": line_number, "delta_m": distance_delta,
                                          "distance_m": sample["s"]})
                yaw_delta = (sample["yaw"] - previous["yaw"] + 180) % 360 - 180
                yaw_rate = yaw_delta / sample["dt"]
                yaw_deltas.append(yaw_delta)
                yaw_rates.append(yaw_rate)
                if previous_rate is not None:
                    # Rates describe adjacent intervals; their midpoints are
                    # separated by the mean of those intervals' durations.
                    interval = (previous_interval + sample["dt"]) / 2
                    acceleration = (yaw_rate - previous_rate) / interval
                    yaw_accelerations.append(acceleration)
                    if peak_acceleration is None or abs(acceleration) > peak_acceleration["absolute_deg_per_s2"]:
                        peak_acceleration = {"line": line_number, "distance_m": sample["s"],
                                             "signed_deg_per_s2": acceleration,
                                             "absolute_deg_per_s2": abs(acceleration)}
                previous_rate = yaw_rate
                previous_interval = sample["dt"]
            previous = sample
    if trace_samples == 0:
        raise ValueError(f"No VROOM-MOTION samples found in {path}; enable -VroomMotionTrace")
    if analyzed_samples == 0:
        raise ValueError(f"No valid motion samples remain after --warmup {warmup} in {path}")
    return {
        "source": source_info(path),
        "warmup_requested_samples": warmup,
        "warmup_discarded_samples": min(trace_samples, warmup),
        "trace_samples": trace_samples,
        "analyzed_samples": analyzed_samples,
        "malformed_samples_after_warmup": malformed,
        "nonfinite_samples_after_warmup": nonfinite,
        "nonpositive_dt_samples_after_warmup": nonpositive_dt,
        "all_analyzed_telemetry_valid": malformed == nonfinite == nonpositive_dt == 0,
        "first_invalid_lines": invalid_lines,
        "distance_backsteps_count": backstep_count,
        "largest_distance_backstep_m": largest_backstep,
        "first_distance_backsteps": backsteps,
        "yaw_delta_degrees": motion_stats(yaw_deltas),
        "yaw_rate_degrees_per_second": motion_stats(yaw_rates),
        "yaw_acceleration_degrees_per_second_squared": motion_stats(yaw_accelerations),
        "peak_yaw_acceleration": peak_acceleration,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv_profile", type=Path, help="Unreal CSV profiler capture")
    parser.add_argument("--warmup", type=int, default=120, help="leading samples to discard (default: 120)")
    parser.add_argument("--motion-log", type=Path, help="game log captured with -VroomMotionTrace")
    parser.add_argument("--output", type=Path, help="also save this JSON report to a file")
    args = parser.parse_args()
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")
    try:
        result = {
            "schema_version": 1,
            "percentile_method": "linear interpolation at (sample_count - 1) * percentile",
            "performance": analyze_csv(args.csv_profile, args.warmup),
        }
        if args.motion_log:
            result["motion"] = analyze_motion(args.motion_log, args.warmup)
            result["motion_alignment"] = "Independent warmup; CSV frames and log samples are not time-aligned."
        report = json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
        if args.output:
            args.output.write_text(report, encoding="utf-8")
    except (OSError, ValueError, csv.Error) as error:
        parser.exit(2, f"error: {error}\n")
    print(report, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
