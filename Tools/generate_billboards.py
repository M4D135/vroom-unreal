"""Reproducible 4:1 painted trackside signs. Requires Pillow; no remote assets."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import random

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Content/ArtSource'
FONTS = Path('/Users/Shared/Epic Games/UE_5.8/Engine/Content/Slate/Fonts')


def font(size, italic=False):
    name = 'Roboto-BoldItalic.ttf' if italic else 'Roboto-Bold.ttf'
    candidates = [FONTS / name, Path(name), Path('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf')]
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    raise RuntimeError('Provide Roboto-Bold.ttf / Roboto-BoldItalic.ttf beside the script or in the font search path.')


def label(draw, text, box, fill, italic=False, max_size=300):
    x, y, width, height = box
    size = max_size
    while size > 12:
        f = font(size, italic)
        b = draw.textbbox((0, 0), text, font=f)
        if b[2]-b[0] <= width and b[3]-b[1] <= height:
            draw.text((x+(width-b[2]+b[0])/2-b[0], y+(height-b[3]+b[1])/2-b[1]), text, font=f, fill=fill)
            return
        size -= 2


def finish(image, index):
    draw = ImageDraw.Draw(image, 'RGBA')
    # Sparse paint wear, stable at every resolution; lettering stays dominant.
    rng = random.Random(1991+index)
    for _ in range(450):
        x, y = rng.randrange(2048), rng.randrange(512)
        draw.line((x,y,x+rng.randint(1,7),y), fill=(245,240,220,rng.randint(8,22)), width=1)
    draw.rectangle((2,2,2045,509), outline=(20,24,30,75), width=5)
    draw.line((7,7,2040,7),fill=(255,255,245,65),width=2)
    OUT.mkdir(parents=True,exist_ok=True)
    image.save(OUT / f'ad{index:02d}.png', optimize=True)


def main():
    image = Image.new('RGB',(2048,512),(235,230,211)); d = ImageDraw.Draw(image)
    d.polygon([(60,75),(190,75),(190,324),(337,324),(337,415),(60,415)],fill=(14,56,122))
    d.polygon([(221,75),(337,75),(337,290),(221,290)],fill=(206,36,34))
    label(d,'LANKHOR',(387,62,1580,270),(17,45,93))
    d.rectangle((395,354,1956,365),fill=(197,35,38))
    label(d,'LA PASSION DU JEU',(425,392,1500,66),(119,33,39),max_size=69)
    finish(image,1)

    image = Image.new('RGB',(2048,512),(24,27,25)); d = ImageDraw.Draw(image)
    yellow=(250,205,34)
    for x in (45,145,245):
        d.polygon([(x,104),(x+66,104),(x+6,341),(x-60,341)],fill=yellow)
    label(d,'VROOM',(380,45,1570,313),yellow,True,385)
    d.rectangle((56,395,1992,405),fill=yellow)
    label(d,'GRAND PRIX  •  1991',(430,430,1250,48),(237,232,211),max_size=64)
    finish(image,2)

    image = Image.new('RGB',(2048,512),(12,88,62)); d = ImageDraw.Draw(image)
    for x in range(-120,260,95):
        d.polygon([(x+80,35),(x+132,35),(x-15,476),(x-67,476)],fill=(236,231,211))
    label(d,'MOTORSPORT',(260,65,1710,264),(248,246,227),True,250)
    d.rectangle((289,377,1964,389),fill=(190,211,94))
    label(d,'RACING  /  PERFORMANCE',(380,422,1500,55),(225,233,206),max_size=64)
    finish(image,3)

    image = Image.new('RGB',(2048,512),(185,30,30)); d = ImageDraw.Draw(image)
    for offset in (0,1856):
        for x in range(3):
            for y in range(8):
                d.rectangle((offset+x*64,y*64,offset+(x+1)*64,y*64+64),fill=(236,232,215) if (x+y)%2 else (28,32,33))
    label(d,'RACING',(236,56,1576,281),(249,243,224),True,360)
    d.rectangle((270,382,1778,393),fill=(249,243,224))
    label(d,'THE GRAND PRIX EXPERIENCE',(262,425,1524,49),(249,243,224),max_size=60)
    finish(image,4)


if __name__ == '__main__':
    main()
