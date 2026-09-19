#!/usr/bin/env python3
"""Run complete AI races on portable approximations of all six track centrelines."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
tracks = json.loads((ROOT / 'Content/Data/tracks.json').read_text())['tracks']
data = [str(len(tracks))]
for track in tracks:
    points = track['preview_points_m']
    data.append(f"{track['length_m']} {len(points)}")
    data.extend(f"{p[0]} {p[1]}" for p in points)
with tempfile.TemporaryDirectory(prefix='vroom-resources-') as folder:
    binary = Path(folder) / 'races'
    subprocess.run([*shlex.split(os.environ.get('CXX', 'clang++')), '-std=c++17', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-pedantic', '-I', str(ROOT / 'Source/Vroom/Public'),
                    str(ROOT / 'Tests/race_resources_probe.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], input='\n'.join(data), text=True, check=True, timeout=100)
