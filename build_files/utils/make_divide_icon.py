# SPDX-FileCopyrightText: 2026 Blender Authors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regenerate Divide's classic icon and documentation PNG (requires Pillow)."""
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

# A continuous polygon patch partitioned by a regular grid.
def point(u, v):
    return (3+21*u+6*v, 9-6*u+20*v)
for y in range(3):
    for x in range(3):
        polygon([point(x/3,y/3),point((x+1)/3,y/3),point((x+1)/3,(y+1)/3),point(x/3,(y+1)/3)],
                (170-x*10,199-y*8,237),(83+x*7,118-y*4,175))
draw=ImageDraw.Draw(image)
for i in (1,2):
    for points in ([point(i/3,0),point(i/3,1)], [point(0,i/3),point(1,i/3)]):
        pixels=[(round(x*SCALE),round(y*SCALE)) for x,y in points]
        draw.line(pixels,fill=(39,45,56,255),width=round(1.8*SCALE))
        draw.line(pixels,fill=(255,204,71,255),width=round(.9*SCALE))
for u in (1/3,2/3):
    for v in (1/3,2/3):
        x,y=point(u,v)
        draw.ellipse([(round((x-.8)*SCALE),round((y-.8)*SCALE)),
                      (round((x+.8)*SCALE),round((y+.8)*SCALE))], fill=(255,233,158,255))
icon=image.resize((32,32),Image.Resampling.LANCZOS)
path=ROOT/'release/datafiles/icons_classic/mod_divide.dat'
path.write_bytes(struct.pack('<6i',32,32,0,0,32,32)+icon.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes())
icon.save(ROOT/'doc/fork/images/icons/mod_divide.png')
preview=Image.new('RGBA',(256,256),(40,43,49,255))
preview.alpha_composite(image.resize((256,256),Image.Resampling.LANCZOS))
preview_path=ROOT.parent/'icon_work/divide_preview.png'
preview_path.parent.mkdir(parents=True,exist_ok=True)
preview.save(preview_path)
print(path)
