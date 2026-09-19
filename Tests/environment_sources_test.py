#!/usr/bin/env python3
"""Protect the externally authored environment sources and existing rock footprint."""
from pathlib import Path
import math
import runpy
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
DATA = runpy.run_path(str(ROOT / 'Tools/environment_asset_data.py'))


class EnvironmentSources(unittest.TestCase):
    def test_distance_mask_preserves_foliage_alpha_and_reaches_zero_coverage(self):
        # Execute the graph-building helper without launching Unreal. The
        # engine's expected dither coverage is clamp(mask - clip + .5, 0, 1):
        # preserving .35/.45 clips would leave invisible card pixels alive.
        environment = runpy.run_path(str(ROOT / 'Tools/bootstrap_environment_assets.py'))
        class Node:
            def __init__(self, kind):
                self.kind, self.properties, self.inputs = kind, {}, {}
            def set_editor_property(self, name, value):
                self.properties[name] = value
        outputs = {}
        def connect(source, target, pin=''):
            target.inputs[pin] = (source, '')
        def select(source, output, target, pin):
            target.inputs[pin] = (source, output)
            return True
        api = SimpleNamespace(BlendMode=SimpleNamespace(BLEND_MASKED='masked'),
            MaterialExpressionPerInstanceFadeAmount='fade', MaterialExpressionMultiply='multiply',
            MaterialExpressionCustom='coverage', CustomInput=lambda: Node('input'),
            CustomMaterialOutputType=SimpleNamespace(CMOT_FLOAT1='float1'),
            MaterialProperty=SimpleNamespace(MP_OPACITY_MASK='mask'),
            MaterialEditingLibrary=SimpleNamespace(connect_material_expressions=select))
        def expression(material, kind, **properties):
            node = Node(kind)
            node.properties.update(properties)
            return node
        helpers = dict(expression=expression, connect=connect,
                       output=lambda node, prop: outputs.update({prop: node}),
                       require=lambda ok, message: self.assertTrue(ok, message))
        for alpha in (None, Node('texture')):
            material = Node('material')
            with patch.dict(sys.modules, {'unreal': api}):
                environment['apply_instance_fade'](material, helpers, alpha, 'A' if alpha else '')
            self.assertTrue(material.properties['dithered_lod_transition'])
            self.assertTrue(material.properties['dither_opacity_mask'])
            node = outputs['mask']
            if alpha:
                coverage = node.inputs['A'][0]
                self.assertEqual(coverage.inputs['Alpha'], (alpha, 'A'))
                self.assertIn('0.450000', coverage.properties['code'])
                self.assertEqual(node.inputs['B'][0].kind, 'fade')
            else:
                self.assertEqual(node.kind, 'fade')
            clip = material.properties['opacity_mask_clip_value']
            self.assertEqual(max(0, min(1, 0 - clip + .5)), 0)
            self.assertEqual(max(0, min(1, 1 - clip + .5)), 1)
        with patch.dict(sys.modules, {'unreal': api}):
            environment['apply_instance_fade'](Node('material'), helpers, Node('needles'), opacity_clip=.35)
        coverage = outputs['mask'].inputs['A'][0]
        self.assertIn('0.350000', coverage.properties['code'])
        # Evaluate the emitted scalar expression: filtered needle interiors
        # must remain opaque before culling, unlike raw alpha * distance fade.
        expression = coverage.properties['code'].removeprefix('return ').removesuffix(';')
        for alpha, expected in ((0, 0), (.2, 0), (.4, 1), (.5, 1), (1, 1)):
            value = eval(expression, {'__builtins__': {}},
                         {'Alpha': alpha, 'saturate': lambda x: max(0, min(1, x))})
            self.assertEqual(value, expected)
            self.assertEqual(value * 0, 0)
            self.assertEqual(value * .5, expected * .5)

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

    def test_evergreen_lods_preserve_roots_and_reduce_distant_cost(self):
        trees = runpy.run_path(str(ROOT / 'Tools/bootstrap_tree_assets.py'))
        report = trees['audit_branched_trees']()
        # Protect the visible crown/trunk contract and distant rendering budget,
        # independently of which individual twigs the generator retains.
        for species, info in report.items():
            lods = trees['branched_tree_lods'](species)
            self.assertEqual(lods, trees['branched_tree_lods'](species))
            self.assertEqual(len(lods), 3)
            self.assertLessEqual(info['lod_triangles'][0], 1100)
            self.assertLessEqual(info['lod_triangles'][2], 220)
            heights = []
            for lod in lods:
                points = lod['positions']
                self.assertEqual(min(p[2] for p in points), 0.0)
                self.assertLessEqual(max(math.hypot(p[0], p[1]) for p in points),
                                     info['radius_limit_cm'] + 1.e-6)
                heights.append(max(p[2] for p in points))
                for material in (0, 1):
                    self.assertTrue(any(face[0] == material for face in lod['triangles']))
                roots = [p for p in points if p[2] < 1.0]
                self.assertTrue(roots)
                self.assertLessEqual(max(math.hypot(p[0], p[1]) for p in roots), 22.1)
            self.assertLess(max(heights) - min(heights), 8.0,
                            'LOD switching must not visibly change a tree height')


if __name__ == '__main__':
    unittest.main()
