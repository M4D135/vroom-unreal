#!/usr/bin/env python3
"""Export the shared artwork to desktop icon formats (Python + Pillow).

Run only when the master PNG changes. Game builds use these versioned exports.
This performs size/format conversion only, with the generated alpha preserved.
"""
from pathlib import Path
import json
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = ROOT / 'Build/Branding/Vroom.png'
    with Image.open(source) as image:
        master = image.convert('RGBA')
    if master.width != master.height or master.width < 1024:
        raise SystemExit('The master icon must be square and at least 1024 px.')
    outputs = {name: ROOT / 'Build' / path for name, path in (
        ('windows', 'Windows/Application.ico'),
        ('mac', 'Mac/Application.icns'),
        ('linux', 'Linux/Vroom.png'))}
    for output in outputs.values():
        output.parent.mkdir(parents=True, exist_ok=True)
    master.save(outputs['windows'], format='ICO',
                sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
    master.save(outputs['mac'], format='ICNS')
    master.resize((512, 512), Image.Resampling.LANCZOS).save(outputs['linux'])
    # UE 5.8's modern Xcode packaging reads the project asset catalogue.
    # Logical size and pixel size differ for @2x; both scales use idiom mac.
    catalogue = ROOT / 'Build/Mac/Resources/Assets.xcassets'
    icons = catalogue / 'AppIcon.appiconset'
    icons.mkdir(parents=True, exist_ok=True)
    images = []
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            filename = f'icon_{size}x{size}' + ('@2x' if scale == 2 else '') + '.png'
            master.resize((size * scale, size * scale), Image.Resampling.LANCZOS).save(icons / filename)
            images.append({'filename': filename, 'idiom': 'mac', 'scale': f'{scale}x', 'size': f'{size}x{size}'})
    info = {'author': 'xcode', 'version': 1}
    (catalogue / 'Contents.json').write_text(json.dumps({'info': info}, indent=2) + '\n')
    (icons / 'Contents.json').write_text(json.dumps({'images': images, 'info': info}, indent=2) + '\n')
    for output in outputs.values():
        print(output)


if __name__ == '__main__':
    main()
