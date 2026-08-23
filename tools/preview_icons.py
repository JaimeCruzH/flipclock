#!/usr/bin/env python3
"""Hoja de contacto de los iconos del tiempo, sobre los dos fondos reales."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw
from wx_icons import render_icon, COND_NAMES

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BIG, SMALL = 104, 44
LABELS = {"clear":"Despejado","partly":"Parcial","cloudy":"Nublado","fog":"Niebla",
          "drizzle":"Llovizna","rain":"Lluvia","showers":"Chubascos",
          "snow":"Nieve","thunder":"Tormenta"}

def bg(w, h, night):
    top = (32, 34, 78) if night else (86, 156, 214)
    bot = (92, 74, 132) if night else (176, 206, 232)
    img = Image.new("RGB", (w, h)); d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(top[i] + (bot[i]-top[i])*t) for i in range(3)))
    return img

cols = len(COND_NAMES)
cw = BIG + 24
sheet = Image.new("RGB", (cw*cols, (BIG+SMALL+70)*2), (0,0,0))
for row, night in enumerate((False, True)):
    y0 = row * (BIG+SMALL+70)
    sheet.paste(bg(cw*cols, BIG+SMALL+70, night), (0, y0))
    d = ImageDraw.Draw(sheet)
    for i, c in enumerate(COND_NAMES):
        x = i*cw + 12
        sheet.paste(render_icon(c, not night, BIG), (x, y0+8), render_icon(c, not night, BIG))
        sheet.paste(render_icon(c, not night, SMALL), (x+(BIG-SMALL)//2, y0+BIG+14),
                    render_icon(c, not night, SMALL))
        d.text((x+4, y0+BIG+SMALL+24), LABELS[c], fill=(255,255,255))
    d.text((6, y0+BIG+SMALL+44), "DIA" if not night else "NOCHE", fill=(255,255,0))

os.makedirs(os.path.join(ROOT,"preview"), exist_ok=True)
out = os.path.join(ROOT, "preview", "wx_icons.png")
sheet.save(out)
sheet.resize((sheet.width*2, sheet.height*2), Image.LANCZOS).save(
    os.path.join(ROOT, "preview", "wx_icons_2x.png"))
print("escrito", out, sheet.size)
