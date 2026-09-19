#!/usr/bin/env python3
"""Fetch the pinned CC0 environment sources; --check only verifies local SHA-256.

Nothing is fetched by Unreal at runtime or by the asset bootstrap. Existing
manifest hashes are enforced on subsequent fetches; changed upstream files are
reported instead of silently replacing a validated source.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'Content/ArtSource/realistic'
MANIFEST = DEST / 'manifest.json'
HEADERS = {'User-Agent': 'VroomUnreal-CC0-asset-import/0.33'}
SOURCES = (
    {'id': 'Asphalt010', 'provider': 'ambientCG', 'page': 'https://ambientcg.com/view?id=Asphalt010',
     'author': 'Lennart Demes', 'license': 'https://docs.ambientcg.com/license/',
     'url': 'https://ambientcg.com/get?file=Asphalt010_1K-JPG.zip'},
    {'id': 'Grass004', 'provider': 'ambientCG', 'page': 'https://ambientcg.com/view?id=Grass004',
     'author': 'Lennart Demes', 'license': 'https://docs.ambientcg.com/license/',
     'url': 'https://ambientcg.com/get?file=Grass004_1K-JPG.zip'},
    {'id': 'rock_moss_set_02', 'provider': 'Poly Haven', 'page': 'https://polyhaven.com/a/rock_moss_set_02',
     'author': 'Kless Gyzen', 'license': 'https://polyhaven.com/license',
     'url': 'https://api.polyhaven.com/files/rock_moss_set_02'},
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def download(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=60) as response:
        return response.read()


def verify_sources():
    data = json.loads(MANIFEST.read_text())
    for source in data['sources']:
        for entry in source['files']:
            path = DEST / entry['path']
            if not path.is_file() or sha(path.read_bytes()) != entry['sha256']:
                raise RuntimeError('Missing or modified CC0 source: ' + str(path))
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.check:
        data = verify_sources()
        print('PASS: verified CC0 environment sources:', ', '.join(s['id'] for s in data['sources']))
        return
    previous = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {'sources': []}
    pinned = {f['path']: f['sha256'] for s in previous['sources'] for f in s['files']}
    result = {'schema': 1, 'license': 'CC0-1.0', 'resolution': '1K', 'sources': []}
    for source in SOURCES:
        source = dict(source)
        files = []
        def save(relative, data, url):
            relative = source['id'] + '/' + relative
            digest = sha(data)
            if relative in pinned and pinned[relative] != digest:
                raise RuntimeError('Upstream changed pinned source: ' + relative)
            path = DEST / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists() or sha(path.read_bytes()) != digest:
                path.write_bytes(data)
            files.append({'path': relative, 'url': url, 'bytes': len(data), 'sha256': digest})
        if source['provider'] == 'ambientCG':
            raw = download(source['url'])
            source['archive_sha256'] = sha(raw)
            with tempfile.TemporaryFile() as stream:
                stream.write(raw); stream.seek(0)
                with zipfile.ZipFile(stream) as archive:
                    for name in archive.namelist():
                        if name.endswith(('_Color.jpg', '_NormalDX.jpg', '_Roughness.jpg')):
                            save(Path(name).name, archive.read(name), source['url'])
            if len(files) != 3:
                raise RuntimeError('Expected three PBR maps for ' + source['id'])
        else:
            metadata = json.loads(download(source['url']))
            asset = metadata['gltf']['1k']['gltf']
            save(source['id'] + '_1k.gltf', download(asset['url']), asset['url'])
            for name, entry in asset['include'].items():
                save(name, download(entry['url']), entry['url'])
        source['files'] = files
        result['sources'].append(source)
        print(source['id'], sum(f['bytes'] for f in files), 'bytes')
    DEST.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(result, indent=2) + '\n')
    verify_sources()
    print('PASS:', MANIFEST)


if __name__ == '__main__':
    main()
