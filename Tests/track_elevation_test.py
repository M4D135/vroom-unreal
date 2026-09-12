#!/usr/bin/env python3
"""Portable checks for recovered track events and the explicit elevation preview.

Run from any directory: python3 Tests/track_elevation_test.py
No Unreal runtime or original executable is needed.
"""
import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("extract_tracks", ROOT / "Tools/extract_tracks.py")
EXTRACT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACT)


def event(s, direction, index=5, line=1):
    return {"type": "relief", "s_m": s, "direction": direction,
            "raw_curve_index": index, "source_line": line}


def ramp_events():
    # Inclination rises for 10 m, remains constant for 10 m, then falls to 0.
    return [event(10, "up"), event(20, "flat"), event(30, "down"), event(40, "flat")]


def hermite(s, left, right, left_grade, right_grade):
    span = right[0] - left[0]
    t = (s - left[0]) / span
    z = ((2*t**3 - 3*t**2 + 1) * left[1]
         + (t**3 - 2*t**2 + t) * span * left_grade
         + (-2*t**3 + 3*t**2) * right[1]
         + (t**3 - t**2) * span * right_grade)
    grade = ((6*t*t - 6*t) * left[1] / span
             + (3*t*t - 4*t + 1) * left_grade
             + (-6*t*t + 6*t) * right[1] / span
             + (3*t*t - 2*t) * right_grade)
    return z, grade


class ElevationTests(unittest.TestCase):
    def test_flat_stops_inclination_change_without_resetting_inclination(self):
        source, preview = EXTRACT.elevation_from_events(ramp_events(), 100, grade_scale=0.001)
        plateau = next(s for s in source["segments"] if s["s_start_m"] == 20)
        self.assertEqual(plateau["duration_m"], 10)
        self.assertEqual(plateau["inclination_start_raw"], 1000)
        self.assertEqual(plateau["inclination_end_raw"], 1000)
        self.assertEqual(source["height_integral_per_lap_raw_m"], 20000)
        self.assertEqual(source["final_inclination_raw"], 0)
        self.assertFalse(source["height_integral_closes"])
        self.assertAlmostEqual(preview["closure_grade_offset_m_per_m"], -0.2)
        self.assertAlmostEqual(preview["max_grade_m_per_m"], 0.8)

    def test_source_shift_strength_and_direction(self):
        def source_for(index):
            return EXTRACT.elevation_from_events(
                [event(10, "down", index), event(20, "up", index), event(30, "flat", index)], 50)[0]
        weak, strong = source_for(5), source_for(4)
        self.assertEqual(weak["segments"][1]["inclination_end_raw"], -1000)
        self.assertEqual(strong["segments"][1]["inclination_end_raw"], -2000)
        self.assertEqual(strong["height_integral_per_lap_raw_m"],
                         2 * weak["height_integral_per_lap_raw_m"])

    def test_unordered_top_events_are_sorted_without_mutating_source_order(self):
        events = ramp_events()[::-1]
        original = list(events)
        source, _ = EXTRACT.elevation_from_events(events, 100)
        self.assertEqual(events, original)
        self.assertEqual([s["s_start_m"] for s in source["segments"]], [0, 10, 20, 30, 40])

    def test_hermite_retains_small_features_and_periodic_tangent(self):
        source, profile = EXTRACT.elevation_from_events(ramp_events(), 100,
                                                       grade_scale=0.001, sample_spacing_m=7)
        samples, grades = profile["samples_m"], profile["grades_m_per_m"]
        self.assertEqual(samples[0], [0, 0])
        self.assertEqual(samples[-1], [100, 0])
        self.assertEqual(grades[0], grades[-1])
        for boundary in [10, 20, 30, 40]:
            self.assertIn(boundary, [p[0] for p in samples])
        # Analytic ramp height before closure at 17.5 m: 100*(7.5^2)/2.
        i = next(i for i in range(len(samples)-1) if samples[i][0] <= 17.5 < samples[i+1][0])
        z, grade = hermite(17.5, samples[i], samples[i+1], grades[i], grades[i+1])
        self.assertAlmostEqual(z, 2.8125 - 0.2*17.5, places=8)
        self.assertAlmostEqual(grade, 0.75 - 0.2, places=8)
        # The altitude area correction closes height while retaining source
        # second derivative. End slopes must be the same even though not zero.
        self.assertAlmostEqual(hermite(samples[1][0], samples[0], samples[1], grades[0], grades[1])[1], grades[1])
        self.assertAlmostEqual(hermite(samples[-2][0], samples[-2], samples[-1], grades[-2], grades[-1])[1], grades[-2])
        self.assertTrue(source["inclination_closes"])

    def test_no_events_does_not_invent_relief(self):
        source, profile = EXTRACT.elevation_from_events([], 100)
        self.assertEqual(source["status"], "no_relief_events_in_snapshot")
        self.assertEqual(profile["status"], "flat_fallback_no_relief_events")
        self.assertEqual(profile["samples_m"], [])
        self.assertEqual(profile["grades_m_per_m"], [])

    def test_nonperiodic_or_invalid_source_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "does not close"):
            EXTRACT.elevation_from_events([event(10, "up")], 100)
        for bad in [event(101, "flat"), event(10, "unknown"), event(10, "up", 6)]:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                EXTRACT.elevation_from_events([bad], 100)
        for bad_length in [0, -1, math.nan, math.inf]:
            with self.subTest(length=bad_length), self.assertRaises(ValueError):
                EXTRACT.elevation_from_events([], bad_length)

    def test_parser_uses_action_bytes_and_centimeters_not_comments(self):
        fixture = """* Synthetic source fixture
DIS      SET 0
      MTOP  0,0,0,0,0
            TOP 5,5,1000,0,0      Down (deliberately incorrect comment)
            TOP 5,1,2000,0,0
            TOP 5,9,3000,0,0      Up (deliberately incorrect comment)
            TOP 5,1,4000,0,0
      MTOP -1,-1,10000,0,0
CLAP     DC.L 10000
CSCA     DC.W 10000/4
            YX 0,0
            YX 0,10
            YX 1600,0
"""
        with tempfile.TemporaryDirectory(prefix="vroom-track-test-") as directory:
            path = Path(directory) / "cr1.asm"
            path.write_text(fixture)
            track = EXTRACT.extract_track(path, 1)
        relief = [e for e in track["events"] if e["type"] == "relief"]
        self.assertEqual([e["s_m"] for e in relief], [10, 20, 30, 40])
        self.assertEqual([e["direction"] for e in relief], ["up", "flat", "down", "flat"])
        self.assertEqual(track["source_elevation"]["height_integral_per_lap_raw_m"], 20000)

    def test_preserved_austria_data_and_runtime_contract(self):
        data = json.loads((ROOT / "Content/Data/tracks.json").read_text())
        self.assertEqual(data["schema_version"], 2)
        austria = next(t for t in data["tracks"] if t["id"] == 1)
        self.assertEqual(austria["source_file"], "cr1.asm")
        self.assertEqual(austria["length_m"], 5911)
        source = austria["source_elevation"]
        self.assertEqual(source["event_count"], 47)
        self.assertEqual(source["final_inclination_raw"], 0)
        self.assertEqual(source["height_integral_per_lap_raw_m"], 6835000)
        for track in data["tracks"]:
            with self.subTest(track=track["id"]):
                reconstructed_source, reconstructed_preview = EXTRACT.elevation_from_events(track["events"], track["length_m"])
                self.assertEqual(reconstructed_source, track["source_elevation"])
                self.assertEqual(reconstructed_preview, track["preview_elevation"])
                profile = track["preview_elevation"]
                samples, grades = profile["samples_m"], profile["grades_m_per_m"]
                self.assertEqual(len(samples), len(grades))
                if track["id"] != 1:
                    self.assertEqual(samples, [])
                    continue
                self.assertEqual(samples[0], [0, 0])
                self.assertEqual(samples[-1], [track["length_m"], 0])
                self.assertEqual(grades[0], grades[-1])
                self.assertTrue(all(math.isfinite(v) for p in samples for v in p))
                self.assertTrue(all(math.isfinite(g) for g in grades))
                self.assertTrue(all(0 < b[0]-a[0] <= 5.000001 for a, b in zip(samples, samples[1:])))
                self.assertGreater(profile["max_sample_height_m"]-profile["min_sample_height_m"], 25)
                self.assertLess(max(abs(g) for g in grades), 0.13)


if __name__ == "__main__":
    unittest.main(verbosity=2)
