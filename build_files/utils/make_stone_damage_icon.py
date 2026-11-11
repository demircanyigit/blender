# SPDX-FileCopyrightText: 2026 Blender Authors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regenerate Stone Damage's classic icon and documentation PNG (requires Pillow)."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
SCALE = 8
image = Image.new('RGBA', (32 * SCALE, 32 * SCALE))
def polygon(points, top, bottom, outline=(35, 40, 48, 255)):
    coords = [(round(x * SCALE), round(y * SCALE)) for x, y in points]
    mask = Image.new('L', image.size)
    ImageDraw.Draw(mask).polygon(coords, fill=255)
    gradient = Image.new('RGBA', image.size)
    draw = ImageDraw.Draw(gradient)
    lo, hi = min(y for x, y in coords), max(y for x, y in coords)
    for y in range(lo, hi + 1):
        t = (y-lo) / max(hi-lo, 1)
        color = tuple(round(a*(1-t)+b*t) for a,b in zip(top,bottom)) + (255,)
        draw.line([(0,y),(32*SCALE,y)], fill=color)
    image.paste(gradient, (0,0), mask)
    ImageDraw.Draw(image).line(coords+[coords[0]], fill=outline, width=round(.8*SCALE), joint='curve')

# An isometric stone with a missing upper-right corner and exposed fracture facets.
polygon([(3,10),(15,4),(25,8),(20,10),(18,14),(14,17)], (215,224,237),(144,162,185))
polygon([(3,10),(14,17),(14,29),(3,23)], (153,177,210),(83,113,156))
polygon([(14,17),(18,14),(22,17),(26,16),(26,23),(14,29)], (122,153,199),(59,88,139))
polygon([(20,10),(25,8),(23,12),(26,16),(22,17),(18,14)], (245,218,152),(172,143,97))
polygon([(20,10),(23,12),(18,14)], (255,237,190),(191,171,130))
polygon([(27,4),(30,6),(28,9),(25,7)], (224,232,243),(127,151,182))
polygon([(28,11),(30,13),(28,16),(26,13)], (248,218,143),(165,135,83))
draw=ImageDraw.Draw(image)
for points in [[(6,13),(6,21),(11,24)],[(16,19),(16,26)],[(8,9),(15,6),(20,8)]]:
    draw.line([(int(x*SCALE),int(y*SCALE)) for x,y in points],fill=(230,239,252,145),width=round(.55*SCALE))
icon=image.resize((32,32),Image.Resampling.LANCZOS)
path=ROOT/'release/datafiles/icons_classic/mod_stone_damage.dat'
path.write_bytes(struct.pack('<6i',32,32,0,0,32,32)+icon.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes())
icon.save(ROOT/'doc/fork/images/icons/mod_stone_damage.png')
preview=Image.new('RGBA',(256,256),(40,43,49,255))
preview.alpha_composite(image.resize((256,256),Image.Resampling.LANCZOS))
preview_path=ROOT.parent/'icon_work/stone_damage_preview.png'
preview_path.parent.mkdir(parents=True,exist_ok=True)
preview.save(preview_path)
print(path)
