# SPDX-FileCopyrightText: 2026 Blender Authors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regenerate Join's classic icon and documentation PNG (requires Pillow)."""
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

def cube(x,y,size):
    polygon([(x,y),(x+size*.55,y-size*.3),(x+size,y),(x+size*.45,y+size*.3)],
            (210,226,249),(136,171,218))
    polygon([(x,y),(x+size*.45,y+size*.3),(x+size*.45,y+size),(x,y+size*.7)],
            (147,179,222),(75,108,159))
    polygon([(x+size*.45,y+size*.3),(x+size,y),(x+size,y+size*.7),(x+size*.45,y+size)],
            (105,146,204),(53,83,133))
cube(2,6,8)
cube(2,21,8)
draw=ImageDraw.Draw(image)
def line(points,color,width):
    draw.line([(round(x*SCALE),round(y*SCALE)) for x,y in points],fill=color,width=round(width*SCALE),joint='curve')
for points in [[(11,9),(15,15),(20,15)],[(11,25),(15,15)]]:
    line(points,(35,40,48,255),2.9)
    line(points,(255,204,71,255),1.35)
polygon([(17,12),(21,15),(17,18)],(255,228,132),(222,163,37))
# One receiving mesh container; its two pieces remain visibly separate.
line([(22,4),(30,4),(30,29),(22,29)],(35,40,48,255),2.7)
line([(22,4),(30,4),(30,29),(22,29)],(255,204,71,255),1.1)
cube(22,8,6)
cube(22,20,6)
icon=image.resize((32,32),Image.Resampling.LANCZOS)
path=ROOT/'release/datafiles/icons_classic/mod_join.dat'
path.write_bytes(struct.pack('<6i',32,32,0,0,32,32)+icon.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes())
icon.save(ROOT/'doc/fork/images/icons/mod_join.png')
preview=Image.new('RGBA',(256,256),(40,43,49,255))
preview.alpha_composite(image.resize((256,256),Image.Resampling.LANCZOS))
preview_path=ROOT.parent/'icon_work/join_preview.png'
preview_path.parent.mkdir(parents=True,exist_ok=True)
preview.save(preview_path)
print(path)
