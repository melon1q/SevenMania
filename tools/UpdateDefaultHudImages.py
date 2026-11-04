"""Bake static HUD PNGs and patch the existing default skin without rebuilding its layout.

Authoring dependency: Pillow (also used by CreateHudImageTestSkin.py).
The game/build only needs the checked-in PNGs and .semaskin, not Python/Pillow.
"""
import argparse
import io
import json
import math
from pathlib import Path
import zipfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['pgreat', 'great', 'good', 'bad', 'poor', 'mine']
LABELS = ['GREAT', 'GREAT', 'GOOD', 'BAD', 'POOR', 'MINE']
COLORS = ['#ffd614', '#4ade80', '#38bdf8', '#fb923c', '#f87171', '#ff4040']


def update(package, image_directory, preview=None):
    # Read first; a failed input never replaces the source package.
    with zipfile.ZipFile(package) as archive:
        entries = archive.infolist()
        files = {entry.filename: archive.read(entry) for entry in entries}
    manifest = json.loads(files['manifest.json'])
    play = json.loads(files['screens/play.json'])
    hud = next(e for e in play['elements'] if e.get('binding') == 'play.judgementCombo')
    scale = 2
    font = ImageFont.truetype(str(ROOT / 'Assets/Fonts/Oxanium-Regular.ttf'), 80 * scale)
    labels = LABELS + list('0123456789')
    boxes = [font.getbbox(text) for text in labels]
    top = min(box[1] for box in boxes)
    bottom = max(box[3] for box in boxes)
    height = math.ceil((bottom - top) / scale) + 16
    widths = [math.ceil(max(font.getlength(t) for t in group) / scale) + 16
              for group in (LABELS, list('0123456789'))]
    images = []
    image_directory.mkdir(parents=True, exist_ok=True)
    for index, text in enumerate(labels):
        judgement = index < 6
        name = NAMES[index] if judgement else str(index - 6)
        width = widths[0 if judgement else 1]
        image = Image.new('RGBA', (width * scale, height * scale))
        draw = ImageDraw.Draw(image)
        box = boxes[index]
        x = (image.width - (box[2] - box[0])) / 2 - box[0]
        y = 8 * scale - top
        # Same black outer / white inner outline as the native HUD style.
        draw.text((x, y), text, font=font, fill='white', stroke_width=6 * scale, stroke_fill='black')
        draw.text((x, y), text, font=font, fill=COLORS[index] if judgement else '#f5f8fc',
                  stroke_width=3 * scale, stroke_fill='white')
        if index == 0:
            mask = Image.new('L', image.size)
            ImageDraw.Draw(mask).text((x, y), text, font=font, fill=255)
            rainbow = Image.new('RGBA', image.size)
            rainbow_draw = ImageDraw.Draw(rainbow)
            colors = ['#ff2020', '#ffff20', '#20ff20', '#20ffff', '#2020ff', '#ff20ff']
            for band, color in enumerate(colors):
                left = round(x + box[0] + (box[2] - box[0]) * band / 6)
                right = round(x + box[0] + (box[2] - box[0]) * (band + 1) / 6)
                rainbow_draw.rectangle((left, 0, right, image.height), fill=color)
            image.paste(rainbow, (0, 0), mask)
        image = image.resize((width, height), Image.Resampling.LANCZOS)
        images.append(image)
        data = io.BytesIO()
        image.save(data, format='PNG')
        group = 'judgement' if judgement else 'combo'
        relative = f'assets/images/hud/{group}/{name}.png'
        files[relative] = data.getvalue()
        loose = image_directory / group / f'{name}.png'
        loose.parent.mkdir(exist_ok=True)
        loose.write_bytes(data.getvalue())
        manifest['resourcePaths'][f'default-hud-{name}'] = relative
    files['licenses/Oxanium-OFL.txt'] = (ROOT / 'Assets/Fonts/Licenses/Oxanium-OFL.txt').read_bytes()
    hud['hud'] = dict(judgementMode='image', comboMode='image',
                     judgements={name: f'default-hud-{name}' for name in NAMES},
                     digits=[f'default-hud-{n}' for n in range(10)],
                     judgementWidth=widths[0], judgementHeight=height,
                     digitWidth=widths[1], digitHeight=height, spacing=0, gap=12, displayMs=850)
    # Font HUD currently normalizes its origin to (660,520), regardless of JSON y.
    # Use that effective origin so entering image mode does not shift the HUD down.
    hud.update(x=660, y=520, width=600, height=100, align='center', verticalAlign='center')
    # All other resources/elements/metadata, including the user's layout, stay intact.
    manifest['skinVersion'] = '1.1.0'
    files['manifest.json'] = json.dumps(manifest, ensure_ascii=False, indent=2).encode('utf-8')
    files['screens/play.json'] = json.dumps(play, ensure_ascii=False, indent=2).encode('utf-8')
    temporary = package.with_suffix('.semaskin.tmp')
    try:
        with zipfile.ZipFile(temporary, 'w') as archive:
            for entry in entries:
                archive.writestr(entry, files.pop(entry.filename))
            for name, data in sorted(files.items()):
                entry = zipfile.ZipInfo(name, (2026, 10, 31, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, data)
        temporary.replace(package)
    finally:
        temporary.unlink(missing_ok=True)
    if preview:
        sheet = Image.new('RGBA', (max(widths) * 2, height * 7), '#101820')
        for i, image in enumerate(images[:6]):
            sheet.alpha_composite(image, ((i % 2) * max(widths), (i // 2) * height))
        for i, image in enumerate(images[6:]):
            sheet.alpha_composite(image, ((i % 5) * widths[1], (3 + i // 5) * height))
        preview.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(preview)
    print(f'{package}: 6 judgement + 10 digit PNGs, image/image defaults, {widths=} {height=}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, default=ROOT / 'Assets/Skins/DefaultReference.semaskin')
    parser.add_argument('--images', type=Path, default=ROOT / 'Assets/Skins/DefaultHudImages')
    parser.add_argument('--preview', type=Path)
    args = parser.parse_args()
    update(args.package, args.images, args.preview)
