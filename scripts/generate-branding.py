#!/usr/bin/env python3
"""Generate Pyrolight icons from assets/logo-no-text.png (requires Pillow).

Legacy Moonlight asset paths stay intact to simplify upstream merges.
Run from any working directory: python3 scripts/generate-branding.py
"""
import base64
from io import BytesIO
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    image = Image.open(ROOT / 'assets/logo-no-text.png').convert('RGBA')
    image = image.crop(image.getbbox())
    side = round(max(image.size) * 1.10)
    logo = Image.new('RGBA', (side, side))
    logo.paste(image, ((side - image.width) // 2, (side - image.height) // 2))
    icon = logo.resize((1024, 1024), Image.Resampling.LANCZOS)
    icon.save(ROOT / 'assets/icon.png', optimize=True)
    icon.save(ROOT / 'app/moonlight.ico', sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
    icon.save(ROOT / 'app/moonlight.icns')
    icon.resize((64, 64), Image.Resampling.LANCZOS).save(ROOT / 'app/moonlight_wix.png')
    icon.resize((256, 256), Image.Resampling.LANCZOS).save(ROOT / 'app/deploy/steamlink/moonlight.png')
    png = BytesIO()
    icon.resize((512, 512), Image.Resampling.LANCZOS).save(png, format='PNG', optimize=True)
    data = base64.b64encode(png.getvalue()).decode('ascii')
    # Qt's QSvgRenderer (stream window) and desktop icon loaders use this self-contained SVG.
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
           'width="512" height="512" viewBox="0 0 512 512">\n'
           '<title>Pyrolight</title>\n'
           f'<image width="512" height="512" xlink:href="data:image/png;base64,{data}"/>\n</svg>\n')
    (ROOT / 'app/res/moonlight.svg').write_text(svg)


if __name__ == '__main__':
    main()
