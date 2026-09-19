#!/usr/bin/env python3
"""Protect the externally authored environment sources and existing rock footprint."""
from pathlib import Path
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[1]
DATA = runpy.run_path(str(ROOT / 'Tools/environment_asset_data.py'))


class EnvironmentSources(unittest.TestCase):
    def test_sources_are_present_and_match_pinned_hashes(self):
        manifest = DATA['verify']()
        self.assertEqual(manifest['license'], 'CC0-1.0')
        self.assertEqual(manifest['resolution'], '1K')
        self.assertTrue({'Asphalt010', 'Grass004', 'rock_moss_set_02'} <= {s['id'] for s in manifest['sources']})
        for source in manifest['sources']:
            self.assertTrue(source['license'].startswith('https://'))
            for item in source['files']:
                self.assertEqual(len(item['sha256']), 64)
                self.assertTrue(item['url'].startswith('https://'))

    def test_rock_preserves_footprint_uvs_and_front_faces(self):
        # Decoding also checks triangle orientation against the authored normals;
        # a coordinate reflection must never repeat the earlier tyre winding bug.
        positions, uv, indices = DATA['rock_geometry']()
        self.assertEqual(len(indices) // 3, 8000)
        self.assertEqual(len(positions), len(uv))
        for axis, low, high in ((0,-108.06,115.0),(1,-81.74,81.74),(2,-52.0,136.0)):
            self.assertAlmostEqual(min(p[axis] for p in positions), low, places=5)
            self.assertAlmostEqual(max(p[axis] for p in positions), high, places=5)
        self.assertGreater(len(set(uv)), 1000, 'Photogrammetry UVs must not become placeholder triangle UVs')

    def test_sources_fit_the_import_budget(self):
        info = DATA['audit']()
        self.assertLess(info['bytes'], 16_000_000)
        self.assertEqual(info['texture_resolution'], 1024)
        self.assertEqual(info['texture_count'], 9)
        self.assertLessEqual(max(info['lod_triangle_targets']), 2400)

    def test_photographic_trees_keep_their_authored_lods_and_valid_uvs(self):
        trees = runpy.run_path(str(ROOT / 'Tools/bootstrap_tree_assets.py'))
        # The audit checks the pinned .blend/texture/export hashes, all face
        # orientations, nondegenerate triangles, UVs and fixed scene envelopes.
        report = trees['audit']()
        self.assertEqual(report['Broadleaf']['lod_triangles'], [880, 525, 321])
        self.assertEqual(report['Birch']['lod_triangles'], [726, 476, 278])
        self.assertEqual(report['license'], 'CC0-1.0')
        self.assertLessEqual(report['maximum_texture_resolution'], 1024)


if __name__ == '__main__':
    unittest.main()
