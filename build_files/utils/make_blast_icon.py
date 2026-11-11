# SPDX-FileCopyrightText: 2026 Blender Authors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regenerate Blast's classic icon and documentation PNG (requires Pillow)."""
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

def cube(x,y,size,muted=False):
    top=(186,195,210) if muted else (214,231,252)
    middle=(133,147,166) if muted else (144,180,232)
    dark=(85,97,115) if muted else (61,96,156)
    polygon([(x,y),(x+size*.55,y-size*.3),(x+size,y),(x+size*.45,y+size*.3)],top,middle)
    polygon([(x,y),(x+size*.45,y+size*.3),(x+size*.45,y+size),(x,y+size*.7)],middle,dark)
    polygon([(x+size*.45,y+size*.3),(x+size,y),(x+size,y+size*.7),(x+size*.45,y+size)],middle,dark)
cube(2,12,17)
cube(24,5,5,True)
cube(25,24,4,True)
draw=ImageDraw.Draw(image)
def line(points,color,width):
    draw.line([(round(x*SCALE),round(y*SCALE)) for x,y in points],fill=color,width=round(width*SCALE),joint='curve')
# Yellow keep bracket on the large island; crossed small islands are filtered away.
line([(1,7),(1,30),(18,30)],(35,40,48,255),2.4)
line([(1,7),(1,30),(18,30)],(255,205,68,255),1.05)
for x,y in ((23,5),(23,22)):
    for points in ([(x,y),(x+6,y+5)],[(x+6,y),(x,y+5)]):
        line(points,(40,33,36,255),2.8)
        line(points,(235,104,84,255),1.35)
icon=image.resize((32,32),Image.Resampling.LANCZOS)
path=ROOT/'release/datafiles/icons_classic/mod_blast.dat'
path.write_bytes(struct.pack('<6i',32,32,0,0,32,32)+icon.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes())
icon.save(ROOT/'doc/fork/images/icons/mod_blast.png')
preview=Image.new('RGBA',(256,256),(40,43,49,255))
preview.alpha_composite(image.resize((256,256),Image.Resampling.LANCZOS))
preview_path=ROOT.parent/'icon_work/blast_preview.png'
preview_path.parent.mkdir(parents=True,exist_ok=True)
preview.save(preview_path)
print(path)
