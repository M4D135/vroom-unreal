#!/usr/bin/env python3
"""Regression checks for Unreal CSV frames versus profiler metadata."""

import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "analyze_performance", Path(__file__).resolve().parents[1] / "Tools/analyze_performance.py")
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)


class PerformanceCsvTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "capture.csv"
        # Match the timing indices in the 20260909_054216 capture. Unreal can
        # append stat columns while recording, before repeating the final header.
        self.header = [f"Stat{i}" for i in range(360)]
        self.header[0] = "EVENTS"
        for index, name in ((79, "FrameTime"), (261, "GameThreadTime"), (262, "GPUTime")):
            self.header[index] = name
        self.metadata = [item for i in range(44) for item in (f"[key{i}]", "1")]
        self.metadata[0] = "[HasHeaderRowAtEnd]"
        self.metadata[78:80] = ["[streamingpoolsizemb]", "600"]

    def frame(self, milliseconds, event=""):
        row = ["0"] * len(self.header)
        row[0] = event
        row[79], row[261], row[262] = str(milliseconds), "0.7", "13.5"
        return row

    def analyze(self, rows, warmup):
        with self.path.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream).writerows(rows)
        return ANALYZER.analyze_csv(self.path, warmup)

    def test_numeric_streaming_pool_metadata_is_not_a_600ms_frame(self):
        result = self.analyze([
            self.header, self.frame(1400), self.frame(16), self.frame(16.5) + ["0"] * 3,
            self.header + ["LateStat1", "LateStat2", "LateStat3"], self.metadata,
        ], warmup=1)
        self.assertEqual(result["numeric_frame_rows"], 3)
        self.assertEqual(result["warmup_discarded_frames"], 1)
        self.assertEqual(result["analyzed_frames"], 2)
        self.assertEqual(result["non_data_rows_skipped"], 2)
        self.assertEqual(result["frame_time"]["max_ms"], 16.5)
        self.assertEqual(result["frame_time"]["mean_ms"], 16.25)
        self.assertEqual(result["frame_time"]["over_20_ms_count"], 0)
        self.assertTrue(all(count == 0 for count in result["invalid_metric_samples_after_warmup"].values()))
        self.assertEqual(result["gpu_time"]["samples"], 2)
        self.assertEqual(result["game_thread_time"]["samples"], 2)

    def test_real_hitch_and_incomplete_metrics_are_preserved(self):
        # A bracketed event name is valid frame data, not a metadata marker.
        # Missing GPU/thread columns must not hide the genuine 600-ms frame.
        hitch = self.frame(600, "[checkpoint]")[:80]
        result = self.analyze([self.header, self.frame(16), hitch, self.metadata], warmup=0)
        self.assertEqual(result["numeric_frame_rows"], 2)
        self.assertEqual(result["analyzed_frames"], 2)
        self.assertEqual(result["frame_time"]["max_ms"], 600)
        self.assertEqual(result["frame_time"]["over_20_ms_count"], 1)
        self.assertEqual(result["invalid_metric_samples_after_warmup"],
                         {"FrameTime": 0, "GPUTime": 1, "GameThreadTime": 1})

    def test_metadata_cannot_supply_header_or_consume_warmup(self):
        preamble = ["[label]", "FrameTime"]
        result = self.analyze([
            preamble, self.header, self.metadata, self.frame(1200), self.frame(17),
        ], warmup=1)
        self.assertEqual(result["numeric_frame_rows"], 2)
        self.assertEqual(result["warmup_discarded_frames"], 1)
        self.assertEqual(result["analyzed_frames"], 1)
        self.assertEqual(result["frame_time"]["max_ms"], 17)
        self.assertEqual(result["missing_columns"], [])


if __name__ == "__main__":
    unittest.main()
