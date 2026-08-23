#!/usr/bin/env python3
"""
Iconos del tiempo para la pantalla del pronostico.

Se dibujan con alfa (RGBA) porque van sobre un degradado que cambia con la hora:
hornearlos sobre un fondo fijo, como se hizo con los digitos del reloj, aqui no
sirve. En la placa se emiten como LV_COLOR_FORMAT_RGB565A8.

Estilo: volumen suave con degradados, sin llegar al fotorrealismo de las
tarjetas del reloj — un icono muy trabajado se lee peor de un vistazo, que es
justo lo que se le pide.
"""

import math

from PIL import Image, ImageDraw, ImageFilter

SS = 4  # supersampling

# nombres en el mismo orden que el enum wx_cond_t de weather_src.h
COND_NAMES = ["clear", "partly", "cloudy", "fog", "drizzle",
              "rain", "showers", "snow", "thunder"]

PAL = dict(
    SUN_HI=(255, 214, 92),
    SUN_LO=(247, 160, 42),
    MOON_HI=(238, 242, 250),
    MOON_LO=(188, 199, 219),
    CLOUD_HI=(252, 253, 255),
    CLOUD_LO=(198, 208, 222),
    CLOUD_DARK_HI=(176, 186, 201),
    CLOUD_DARK_LO=(126, 137, 155),
    # Azul oscuro y saturado a proposito: el celeste claro de antes
    # (96,170,235) era casi el mismo tono que el fondo diurno, las gotas
    # desaparecian y chubascos no se distinguia de parcialmente nublado.
    RAIN=(26, 96, 186),
    SNOW=(226, 240, 252),
    BOLT_HI=(255, 214, 92),
    BOLT_LO=(250, 176, 60),
    FOG=(214, 222, 233),
)


def _canvas(size):
    return Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))


def _vgrad_rgba(w, h, c0, c1, mask):
    """Degradado vertical recortado por una mascara de alfa."""
    grad = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(grad)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)],
               fill=tuple(int(round(c0[i] + (c1[i] - c0[i]) * t)) for i in range(3)) + (255,))
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    return out


def _shape_mask(size, draw_fn):
    m = Image.new("L", (size * SS, size * SS), 0)
    draw_fn(ImageDraw.Draw(m))
    return m


def _sun(size, cx, cy, r, rays=True):
    def shape(d):
        if rays:
            for i in range(8):
                a = math.radians(i * 45)
                x0 = cx + math.cos(a) * r * 1.35
                y0 = cy + math.sin(a) * r * 1.35
                x1 = cx + math.cos(a) * r * 1.85
                y1 = cy + math.sin(a) * r * 1.85
                d.line([(x0, y0), (x1, y1)], fill=255, width=int(r * 0.28))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    m = _shape_mask(size, shape)
    return _vgrad_rgba(size * SS, size * SS, PAL["SUN_HI"], PAL["SUN_LO"], m)


def _moon(size, cx, cy, r):
    def shape(d):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
        # el mordisco que convierte el circulo en creciente
        off = r * 0.62
        d.ellipse([cx - r + off, cy - r - r * 0.18,
                   cx + r + off, cy + r - r * 0.18], fill=0)
    m = _shape_mask(size, shape)
    return _vgrad_rgba(size * SS, size * SS, PAL["MOON_HI"], PAL["MOON_LO"], m)


def _cloud_mask(size, cx, cy, w, dark=False):
    """Nube: tres lobulos y una base recta, que es lo que la hace legible."""
    def shape(d):
        r1 = w * 0.30
        r2 = w * 0.24
        r3 = w * 0.20
        base_y = cy + r1 * 0.55
        d.ellipse([cx - w * 0.34 - r3, base_y - r3 * 1.5,
                   cx - w * 0.34 + r3, base_y + r3 * 0.5], fill=255)
        d.ellipse([cx - r1 * 0.15, cy - r1, cx - r1 * 0.15 + r1 * 2, cy + r1], fill=255)
        d.ellipse([cx + w * 0.30 - r2, base_y - r2 * 1.7,
                   cx + w * 0.30 + r2, base_y + r2 * 0.3], fill=255)
        d.rounded_rectangle([cx - w * 0.52, base_y - r3 * 0.6,
                             cx + w * 0.52, base_y + r3 * 0.55],
                            radius=r3 * 0.6, fill=255)
    return _shape_mask(size, shape)


def _cloud(size, cx, cy, w, dark=False):
    m = _cloud_mask(size, cx, cy, w, dark)
    hi = PAL["CLOUD_DARK_HI"] if dark else PAL["CLOUD_HI"]
    lo = PAL["CLOUD_DARK_LO"] if dark else PAL["CLOUD_LO"]
    return _vgrad_rgba(size * SS, size * SS, hi, lo, m)


def _drops(size, cx, cy, w, n, color, snow=False):
    img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    step = w / (n + 1)
    for i in range(n):
        x = cx - w / 2 + step * (i + 1)
        y = cy + (i % 2) * w * 0.10
        if snow:
            r = w * 0.055
            for k in range(3):
                a = math.radians(k * 60)
                d.line([(x - math.cos(a) * r, y - math.sin(a) * r),
                        (x + math.cos(a) * r, y + math.sin(a) * r)],
                       fill=color + (255,), width=int(w * 0.028) or 1)
        else:
            d.line([(x, y), (x - w * 0.05, y + w * 0.17)],
                   fill=color + (255,), width=int(w * 0.058) or 2)
    return img


def _bolt(size, cx, cy, w):
    def shape(d):
        s = w * 0.30
        d.polygon([(cx + s * 0.25, cy - s),
                   (cx - s * 0.45, cy + s * 0.25),
                   (cx + s * 0.02, cy + s * 0.25),
                   (cx - s * 0.22, cy + s * 1.25),
                   (cx + s * 0.55, cy - s * 0.10),
                   (cx + s * 0.05, cy - s * 0.10)], fill=255)
    m = _shape_mask(size, shape)
    return _vgrad_rgba(size * SS, size * SS, PAL["BOLT_HI"], PAL["BOLT_LO"], m)


def _fog_bars(size, cx, cy, w):
    img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i in range(3):
        y = cy + i * w * 0.17
        half = w * (0.46 - i * 0.05)
        d.rounded_rectangle([cx - half, y - w * 0.035, cx + half, y + w * 0.035],
                            radius=w * 0.035, fill=PAL["FOG"] + (255,))
    return img


def render_icon(cond, is_day, size):
    """Devuelve un RGBA de size x size para la condicion dada."""
    S = size * SS
    img = _canvas(size)
    c = S / 2

    def over(layer):
        img.alpha_composite(layer)

    if cond == "clear":
        if is_day:
            over(_sun(size, c, c, S * 0.24))
        else:
            over(_moon(size, c, c, S * 0.28))

    elif cond == "partly":
        if is_day:
            over(_sun(size, c + S * 0.16, c - S * 0.17, S * 0.17))
        else:
            over(_moon(size, c + S * 0.16, c - S * 0.16, S * 0.19))
        over(_cloud(size, c - S * 0.06, c + S * 0.10, S * 0.62))

    elif cond == "cloudy":
        over(_cloud(size, c + S * 0.08, c - S * 0.06, S * 0.52, dark=False))
        over(_cloud(size, c - S * 0.06, c + S * 0.10, S * 0.64, dark=True))

    elif cond == "fog":
        over(_cloud(size, c, c - S * 0.10, S * 0.62))
        over(_fog_bars(size, c, c + S * 0.22, S * 0.62))

    elif cond in ("drizzle", "rain"):
        over(_cloud(size, c, c - S * 0.10, S * 0.62, dark=(cond == "rain")))
        n = 3 if cond == "drizzle" else 4
        over(_drops(size, c, c + S * 0.20, S * 0.50, n, PAL["RAIN"]))

    elif cond == "showers":
        if is_day:
            over(_sun(size, c + S * 0.18, c - S * 0.20, S * 0.15))
        else:
            # mas arriba y algo mayor que en el caso diurno: el creciente es
            # mucho mas fino que el sol y la nube se lo comia entero
            over(_moon(size, c + S * 0.20, c - S * 0.24, S * 0.19))
        over(_cloud(size, c - S * 0.06, c + S * 0.02, S * 0.60))
        over(_drops(size, c - S * 0.04, c + S * 0.26, S * 0.44, 3, PAL["RAIN"]))

    elif cond == "snow":
        over(_cloud(size, c, c - S * 0.10, S * 0.62))
        over(_drops(size, c, c + S * 0.22, S * 0.50, 3, PAL["SNOW"], snow=True))

    elif cond == "thunder":
        over(_cloud(size, c, c - S * 0.12, S * 0.62, dark=True))
        over(_bolt(size, c, c + S * 0.24, S * 0.62))

    # sombra propia muy suave, para que no floten sobre el degradado
    alpha = img.split()[3]
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sh = alpha.filter(ImageFilter.GaussianBlur(S * 0.030)).point(lambda v: int(v * 0.30))
    shadow.putalpha(sh)
    shadow = shadow.point(lambda v: 0)          # negro puro, solo el alfa importa
    shadow.putalpha(sh)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.alpha_composite(shadow, (0, int(S * 0.020)))
    out.alpha_composite(img)

    return out.resize((size, size), Image.LANCZOS)
