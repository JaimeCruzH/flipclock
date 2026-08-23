#!/usr/bin/env python3
"""
Convierte los iconos del tiempo a assets de LVGL (LV_COLOR_FORMAT_RGB565A8).

Van en su propio blob, aparte del de los digitos del reloj, para poder
regenerar unos sin tocar los otros.

Formato RGB565A8: primero todos los pixeles RGB565 (little endian, 2 bytes) y
despues el plano de alfa (1 byte por pixel). El stride es w*2, que es el de la
parte de color. NO se intercambian los bytes aqui: el display es
RGB565_SWAPPED, pero eso lo convierte LVGL al dibujar (con
LV_DRAW_SW_SUPPORT_RGB565_SWAPPED y ..._RGB565A8 activos, que lo estan).

Solo tres condiciones cambian entre dia y noche (despejado, parcial y
chubascos); las demas comparten icono, y asi el blob baja de ~690 KB a ~460 KB.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import numpy as np
from wx_icons import COND_NAMES, render_icon

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "src", "assets")

BIG, SMALL = 104, 44
HAS_NIGHT = {"clear", "partly", "showers"}


def rgb565a8(img):
    a = np.asarray(img.convert("RGBA"), dtype=np.uint16)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

    out = bytearray()
    px = np.empty(v.size * 2, dtype=np.uint8)
    px[0::2] = (v & 0xFF).ravel()          # little endian: byte bajo primero
    px[1::2] = (v >> 8).ravel()
    out += px.tobytes()
    out += np.asarray(img.convert("RGBA"), dtype=np.uint8)[..., 3].tobytes()
    return bytes(out)


def main():
    os.makedirs(ASSETS, exist_ok=True)
    blob = bytearray()
    entries = {}          # (cond, night, size) -> (offset, w, h)

    for size in (BIG, SMALL):
        for cond in COND_NAMES:
            for night in (False, True):
                if night and cond not in HAS_NIGHT:
                    continue
                img = render_icon(cond, not night, size)
                entries[(cond, night, size)] = (len(blob), size, size)
                blob += rgb565a8(img)

    with open(os.path.join(ASSETS, "wx_assets.bin"), "wb") as fh:
        fh.write(blob)

    with open(os.path.join(ASSETS, "wx_assets.S"), "w") as fh:
        fh.write("/* generado por tools/emit_icons.py - no editar */\n"
                 "    .section .rodata.wx_assets,\"a\",@progbits\n"
                 "    .global wx_assets_blob\n"
                 "    .align 4\n"
                 "wx_assets_blob:\n"
                 '    .incbin "src/assets/wx_assets.bin"\n')

    def dsc(key, indent):
        off, w, h = entries[key]
        return ("%s{ .header = { .magic = LV_IMAGE_HEADER_MAGIC,\n"
                "%s              .cf = LV_COLOR_FORMAT_RGB565A8,\n"
                "%s              .w = %d, .h = %d, .stride = %d },\n"
                "%s  .data_size = %d, .data = wx_assets_blob + %d }"
                % (indent, indent, indent, w, h, w * 2, indent, w * h * 3, off))

    with open(os.path.join(ASSETS, "wx_assets.c"), "w") as fh:
        fh.write('/* generado por tools/emit_icons.py - no editar */\n'
                 '#include "wx_assets.h"\n\n'
                 'extern const uint8_t wx_assets_blob[];\n\n')
        for size, tag in ((BIG, "big"), (SMALL, "small")):
            fh.write("/* [condicion][0 = noche, 1 = dia] */\n"
                     "static const lv_image_dsc_t icon_%s[%d][2] = {\n"
                     % (tag, len(COND_NAMES)))
            for cond in COND_NAMES:
                night_key = (cond, True, size) if (cond, True, size) in entries \
                            else (cond, False, size)
                fh.write("  { /* %s */\n" % cond)
                fh.write(dsc(night_key, "    ") + ",\n")
                fh.write(dsc((cond, False, size), "    ") + "\n")
                fh.write("  },\n")
            fh.write("};\n\n")

        fh.write("const lv_image_dsc_t *wx_icon_big(wx_cond_t c, bool is_day)\n"
                 "{\n"
                 "    if (c < 0 || c >= WX_COND_COUNT) c = WX_PARTLY;\n"
                 "    return &icon_big[c][is_day ? 1 : 0];\n"
                 "}\n\n"
                 "const lv_image_dsc_t *wx_icon_small(wx_cond_t c, bool is_day)\n"
                 "{\n"
                 "    if (c < 0 || c >= WX_COND_COUNT) c = WX_PARTLY;\n"
                 "    return &icon_small[c][is_day ? 1 : 0];\n"
                 "}\n")

    with open(os.path.join(ASSETS, "wx_assets.h"), "w") as fh:
        fh.write("/* generado por tools/emit_icons.py - no editar */\n"
                 "#pragma once\n"
                 "#include <lvgl.h>\n"
                 '#include "../weather_src.h"\n\n'
                 "#define WX_ICON_BIG   %d\n"
                 "#define WX_ICON_SMALL %d\n\n"
                 "const lv_image_dsc_t *wx_icon_big(wx_cond_t c, bool is_day);\n"
                 "const lv_image_dsc_t *wx_icon_small(wx_cond_t c, bool is_day);\n"
                 % (BIG, SMALL))

    print("iconos: %d, blob %.0f KB" % (len(entries), len(blob) / 1024.0))


if __name__ == "__main__":
    main()
