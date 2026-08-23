#!/usr/bin/env python3
"""
Generador de assets del reloj flip fotorrealista (Elecrow 3.5" ESP32-S3, 480x320).

Fase 1: --preview  -> escribe PNGs en preview/ para revision humana. NO toca la placa.
Fase 2: --emit     -> convierte los mismos sprites a arrays C RGB565 byte-swapped
                      en src/assets/  (solo tras aprobar el preview).

El aspecto NO sale de degradados dibujados a mano, sino de un pequeño modelo de
iluminacion por pixel (ver SHADE):

  - cada hoja es una superficie ligeramente curvada, con su mapa de normales;
  - iluminacion Blinn-Phong con una luz principal direccional;
  - reflejo de entorno con Fresnel (cielo claro arriba, suelo oscuro abajo), que
    es lo que produce el canto brillante caracteristico del plastico;
  - las cifras tienen relieve real: la normal se perturba con el gradiente de su
    mascara, asi que el canto de la serigrafia capta luz;
  - la luz es GLOBAL: la posicion de cada celda dentro de la escena entra en el
    calculo, de modo que el brillo cruza las cuatro cifras de forma continua.

Por eso cada digito se genera 4 veces, una por columna de la escena.
"""

import argparse
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PREVIEW_DIR = os.path.join(ROOT, "preview")
ASSETS_DIR = os.path.join(ROOT, "src", "assets")

# ---------------------------------------------------------------- geometria
GEOM = dict(
    SCR_W=480, SCR_H=320,
    CARD_W=212,          # una tarjeta = 2 digitos
    CARD_H=196,
    GAP=10,              # separacion entre la tarjeta de horas y la de minutos
    WIN_Y=48,            # borde superior de la ventana
    DATE_Y=284,
)
GEOM["CELL_W"] = GEOM["CARD_W"] // 2          # 106
GEOM["HALF_H"] = GEOM["CARD_H"] // 2          # 98
GEOM["WIN_W"] = GEOM["CARD_W"] * 2 + GEOM["GAP"]
GEOM["WIN_X"] = (GEOM["SCR_W"] - GEOM["WIN_W"]) // 2

# ---------------------------------------------------------------- iluminacion
SHADE = dict(
    LIGHT_DIR=(-0.42, -0.80, 0.43),   # luz principal: arriba, izquierda, de frente
    AMBIENT=0.30,
    KD=0.66,                          # peso difuso
    KS=0.85, SHINE=48,                # reflejo especular estrecho (la fuente de luz)
    KS_WIDE=0.34, SHINE_WIDE=7,       # reflejo ancho (luz de relleno de la sala)
    FRESNEL=1.00,                     # peso del reflejo de entorno
    SKY=(0xB8, 0xBE, 0xCC),           # lo que refleja el plastico mirando arriba
    GROUND=(0x0A, 0x0A, 0x0C),        # ...y mirando abajo
    CURVE_V=13.0,                     # curvatura vertical de cada hoja, en grados
    CURVE_H=11.0,                      # curvatura horizontal de la tarjeta, en grados
    PLASTIC=(0x3A, 0x3B, 0x41),       # albedo del plastico
    NOISE=2.5,                        # micro-rugosidad (rompe el banding)
)

STYLE = dict(
    # --- cifras
    DIGIT_ALBEDO=(0xF2, 0xF1, 0xEC),
    DIGIT_KS=0.13, DIGIT_SHINE=8,     # la serigrafia es mucho mas mate que el plastico
    DIGIT_FRESNEL=0.10,               # y practicamente no refleja el entorno
    DIGIT_AMBIENT=0.66, DIGIT_KD=0.36,  # pintura mate: muy poca variacion con la curvatura
    DIGIT_BUMP=0.25,                  # relieve apenas perceptible del canto de la cifra
    DIGIT_H_RATIO=0.64,
    DIGIT_XSCALE=1.00,
    FONT_CANDIDATES=[
        ("C:/Windows/Fonts/bahnschrift.ttf", "Light SemiCondensed"),
        ("C:/Windows/Fonts/bahnschrift.ttf", None),
        ("C:/Windows/Fonts/ARIALN.TTF", None),
        ("C:/Windows/Fonts/arial.ttf", None),
    ],

    # --- tarjeta
    SEAM_GAP=(0x08, 0x08, 0x0A),      # la ranura entre hojas es un hueco real
    SEAM_SHADOW=0.42,                 # sombra de la hoja alta sobre la baja
    SEAM_SHADOW_H=15,                 # cuantos px baja esa sombra
    EDGE_AO=0.40,                     # oscurecimiento en el borde exterior
    EDGE_AO_W=7,
    CORNER_R=7,

    # --- carcasa
    CASE_ALBEDO=(0x4A, 0x4A, 0x4D),
    CASE_KS=0.42, CASE_SHINE=16,
    CASE_VIGNETTE=0.30,
    BEZEL_R=14,
    RECESS=(0x0B, 0x0B, 0x0D),
    RECESS_PAD=9,
    AXLE=(0x1A, 0x1A, 0x1D),
    DATE_COLOR=(0x9A, 0x9A, 0x9E),
)

SS = 4  # supersampling para texto y curvas


# ---------------------------------------------------------------- utilidades
def norm(v):
    v = np.asarray(v, dtype=np.float32)
    return v / np.linalg.norm(v)


def normalize_map(n):
    return n / np.linalg.norm(n, axis=2, keepdims=True)


def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3 - 2 * t)


FONT_INFO = {}


def load_font(px):
    last = None
    for path, variation in STYLE["FONT_CANDIDATES"]:
        if not os.path.exists(path):
            continue
        try:
            f = ImageFont.truetype(path, px)
            if variation:
                f.set_variation_by_name(variation)
            FONT_INFO["name"] = os.path.basename(path)
            FONT_INFO["variation"] = variation
            return f
        except Exception as e:
            last = e
    raise RuntimeError("no se encontro ninguna fuente utilizable: %s" % last)


def fit_font_for_height(target_h):
    lo, hi, best = 8, target_h * 3, None
    while lo <= hi:
        mid = (lo + hi) // 2
        f = load_font(mid)
        bb = f.getbbox("8")
        if (bb[3] - bb[1]) <= target_h:
            best, lo = f, mid + 1
        else:
            hi = mid - 1
    return best


def to_img(arr):
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")


# ---------------------------------------------------------------- el sombreador
def shade(n, albedo, ks, shine, fres_w=1.0, amb=None, kd=None):
    """
    Blinn-Phong + reflejo de entorno con Fresnel. n: (H,W,3) normales.
    fres_w pondera el reflejo de entorno por pixel: el plastico lo recibe entero,
    la serigrafia casi nada (si no, las cifras parecen cromadas en vez de pintadas).
    """
    L = norm(SHADE["LIGHT_DIR"])
    V = np.array([0.0, 0.0, 1.0], dtype=np.float32)
    Hv = norm(L + V)

    ndl = np.clip(np.tensordot(n, L, axes=([2], [0])), 0, 1)[..., None]
    ndh = np.clip(np.tensordot(n, Hv, axes=([2], [0])), 0, 1)[..., None]
    ndv = np.clip(np.tensordot(n, V, axes=([2], [0])), 0, 1)[..., None]

    if amb is None:
        amb = SHADE["AMBIENT"]
    if kd is None:
        kd = SHADE["KD"]
    out = albedo * (amb + kd * ndl)
    out = out + 255.0 * ks * np.power(ndh, shine)
    out = out + 255.0 * SHADE["KS_WIDE"] * np.power(ndh, SHADE["SHINE_WIDE"]) * 0.35

    # reflejo de entorno: el plastico devuelve cielo o suelo segun hacia donde mire,
    # ponderado por Fresnel (mas reflejo cuanto mas rasante es la vista)
    sky = np.array(SHADE["SKY"], dtype=np.float32)
    ground = np.array(SHADE["GROUND"], dtype=np.float32)
    t = smoothstep((-n[..., 1:2] + 1.0) * 0.5 + 0.18)
    env = ground + (sky - ground) * t
    fres = 0.035 + 0.965 * np.power(1.0 - ndv, 5.0)
    out = out + env * fres * SHADE["FRESNEL"] * fres_w
    return out


def leaf_normals(pos, which):
    """
    Mapa de normales de media tarjeta.
      pos:   columna de la escena, 0..3 (la luz es global, la posicion importa)
      which: 'top' o 'bot'
    Cada hoja se curva sobre su eje horizontal, y la tarjeta entera se curva un
    poco sobre el vertical: de ahi que el brillo se desplace a lo largo de la fila.
    """
    W, H = GEOM["CELL_W"], GEOM["HALF_H"]
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    # posicion horizontal dentro de la TARJETA (0..1), no de la celda
    side = pos % 2
    u = (xx + side * W) / float(2 * W - 1)
    # posicion vertical dentro de la HOJA (0..1)
    v = yy / float(H - 1)

    # la hoja se inclina hacia atras en sus extremos: convexa
    th = np.radians(SHADE["CURVE_V"]) * (2.0 * v - 1.0)
    ph = np.radians(SHADE["CURVE_H"]) * (2.0 * u - 1.0)
    if which == "bot":
        th = np.radians(SHADE["CURVE_V"]) * (2.0 * v - 1.0)

    n = np.stack([np.sin(ph) * np.cos(th),
                  np.sin(th),
                  np.cos(ph) * np.cos(th)], axis=2).astype(np.float32)

    # bisel en los cantos: normal doblada hacia fuera en los ultimos px
    bev = 5
    edge_t = np.clip(np.minimum(v * (H - 1), (1 - v) * (H - 1)) / bev, 0, 1)
    n[..., 1] += (1 - edge_t) * np.where(v < 0.5, -0.55, 0.55)
    return normalize_map(n)


def digit_mask(digit):
    """Mascara antialiased de la cifra sobre la tarjeta completa (CELL_W x CARD_H)."""
    W, H = GEOM["CELL_W"], GEOM["CARD_H"]
    f = fit_font_for_height(int(H * STYLE["DIGIT_H_RATIO"]) * SS)
    m = Image.new("L", (W * SS, H * SS), 0)
    d = ImageDraw.Draw(m)
    bb = f.getbbox(str(digit))
    d.text(((W * SS - (bb[2] - bb[0])) // 2 - bb[0],
            (H * SS - (bb[3] - bb[1])) // 2 - bb[1]), str(digit), font=f, fill=255)
    if STYLE["DIGIT_XSCALE"] != 1.0:
        nw = int(W * SS * STYLE["DIGIT_XSCALE"])
        m = m.resize((nw, H * SS), Image.LANCZOS)
        off = (nw - W * SS) // 2
        m = m.crop((off, 0, off + W * SS, H * SS))
    return np.asarray(m.resize((W, H), Image.LANCZOS), dtype=np.float32) / 255.0


_MASK_CACHE = {}


def get_mask(digit):
    if digit not in _MASK_CACHE:
        _MASK_CACHE[digit] = digit_mask(digit)
    return _MASK_CACHE[digit]


def render_half(digit, pos, which):
    """Media tarjeta ya sombreada: (HALF_H, CELL_W, 3) float."""
    W, H = GEOM["CELL_W"], GEOM["HALF_H"]
    side = "l" if pos % 2 == 0 else "r"

    n = leaf_normals(pos, which)
    full_mask = get_mask(digit)
    a = full_mask[0:H] if which == "top" else full_mask[H:H * 2]
    a3 = a[..., None]

    # relieve de la serigrafia: la normal se dobla en el borde de la cifra
    soft = np.asarray(Image.fromarray((a * 255).astype(np.uint8))
                      .filter(ImageFilter.GaussianBlur(1.1)), dtype=np.float32) / 255.0
    gy, gx = np.gradient(soft)
    n = normalize_map(n + np.stack([-gx * STYLE["DIGIT_BUMP"],
                                    -gy * STYLE["DIGIT_BUMP"],
                                    np.zeros_like(gx)], axis=2))

    albedo = (np.array(SHADE["PLASTIC"], dtype=np.float32) * (1 - a3)
              + np.array(STYLE["DIGIT_ALBEDO"], dtype=np.float32) * a3)
    ks = SHADE["KS"] * (1 - a3) + STYLE["DIGIT_KS"] * a3
    shine = SHADE["SHINE"] * (1 - a3) + STYLE["DIGIT_SHINE"] * a3
    fres_w = (1 - a3) + STYLE["DIGIT_FRESNEL"] * a3
    # la pintura mate clara dispersa mucha luz: casi no se oscurece con la curvatura
    amb = SHADE["AMBIENT"] * (1 - a3) + STYLE["DIGIT_AMBIENT"] * a3
    kd = SHADE["KD"] * (1 - a3) + STYLE["DIGIT_KD"] * a3

    out = shade(n, albedo, ks, shine, fres_w, amb, kd)

    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    # oclusion en el borde exterior de la tarjeta (contra la pared del hueco)
    ew = STYLE["EDGE_AO_W"]
    dist = xx if side == "l" else (W - 1 - xx)
    ao = 1.0 - STYLE["EDGE_AO"] * (1.0 - smoothstep(dist / ew))[..., None]
    # ...y en el borde superior/inferior de la tarjeta
    dv = yy if which == "top" else (H - 1 - yy)
    ao = ao * (1.0 - 0.30 * (1.0 - smoothstep(dv / 5.0))[..., None])
    out = out * ao

    # sombra que la hoja de arriba proyecta sobre la de abajo
    if which == "bot":
        sh = 1.0 - STYLE["SEAM_SHADOW"] * np.power(
            np.clip(1.0 - yy / STYLE["SEAM_SHADOW_H"], 0, 1), 1.7)[..., None]
        out = out * sh

    # micro-rugosidad
    rng = np.random.default_rng(digit * 17 + pos * 3 + (0 if which == "top" else 1))
    out = out + rng.normal(0.0, SHADE["NOISE"], out.shape)

    # La ranura del eje pertenece al canto inferior de la hoja ALTA, no a la
    # escena: asi viaja con la hoja durante el volteo en vez de quedarse fija.
    if which == "top":
        out[H - 1, :, :] = np.array(STYLE["SEAM_GAP"], dtype=np.float32)

    return out


def cell_halves(digit, pos):
    """Las dos mitades de una celda, ya como imagenes PIL con esquinas redondeadas."""
    key = (digit, pos)
    if key in _CELL_CACHE:
        return _CELL_CACHE[key]
    W, H = GEOM["CELL_W"], GEOM["HALF_H"]
    side = "l" if pos % 2 == 0 else "r"
    out = []
    for which in ("top", "bot"):
        img = to_img(render_half(digit, pos, which))
        img = round_outer_corner(img, side, which)
        out.append(img)
    _CELL_CACHE[key] = out
    return out


_CELL_CACHE = {}


def round_outer_corner(img, side, which):
    """Redondea solo la esquina exterior de la tarjeta; el resto queda a escuadra."""
    W, H = img.size
    r = STYLE["CORNER_R"]
    m = Image.new("L", (W * SS, H * SS * 2), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, W * SS - 1, H * SS * 2 - 1], radius=r * SS, fill=255)
    if side == "l":
        d.rectangle([W * SS // 2, 0, W * SS - 1, H * SS * 2 - 1], fill=255)
    else:
        d.rectangle([0, 0, W * SS // 2, H * SS * 2 - 1], fill=255)
    m = m.crop((0, 0, W * SS, H * SS)) if which == "top" \
        else m.crop((0, H * SS, W * SS, H * SS * 2))
    m = m.resize((W, H), Image.LANCZOS)
    return Image.composite(img, Image.new("RGB", (W, H), STYLE["RECESS"]), m)


def card_image(value, card_idx):
    """Tarjeta completa de dos digitos, iluminada segun su posicion en la escena."""
    W, H, half = GEOM["CARD_W"], GEOM["CARD_H"], GEOM["HALF_H"]
    img = Image.new("RGB", (W, H))
    for k, dg in enumerate((value // 10, value % 10)):
        pos = card_idx * 2 + k
        top, bot = cell_halves(dg, pos)
        img.paste(top, (k * GEOM["CELL_W"], 0))
        img.paste(bot, (k * GEOM["CELL_W"], half))
    return img



# ---------------------------------------------------------------- la carcasa
def render_case():
    W, H = GEOM["SCR_W"], GEOM["SCR_H"]
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    # la carcasa es casi plana, con un abombamiento muy suave
    u, v = xx / (W - 1), yy / (H - 1)
    n = np.stack([np.sin(np.radians(6.0) * (2 * u - 1)),
                  np.sin(np.radians(5.0) * (2 * v - 1)),
                  np.ones_like(u)], axis=2).astype(np.float32)
    n = normalize_map(n)
    albedo = np.broadcast_to(np.array(STYLE["CASE_ALBEDO"], dtype=np.float32),
                             (H, W, 3)).copy()
    case = shade(n, albedo, STYLE["CASE_KS"], STYLE["CASE_SHINE"])

    # vineta
    r = np.sqrt(((u - 0.5) * 1.6) ** 2 + ((v - 0.5) * 1.1) ** 2)
    case = case * (1.0 - STYLE["CASE_VIGNETTE"] * smoothstep((r - 0.25) / 0.65))[..., None]

    rng = np.random.default_rng(99)
    case = case + rng.normal(0.0, 1.8, case.shape)
    case_img = to_img(case)

    # bisel exterior del marco: canto claro arriba, oscuro abajo
    bez = Image.new("L", (W * 2, H * 2), 0)
    ImageDraw.Draw(bez).rounded_rectangle(
        [6, 6, W * 2 - 7, H * 2 - 7], radius=STYLE["BEZEL_R"] * 2, outline=255, width=5)
    bez = bez.resize((W, H), Image.LANCZOS).filter(ImageFilter.GaussianBlur(0.6))
    hi = Image.new("L", (W, H), 0)
    hi.paste(bez.crop((0, 0, W, H // 2)), (0, 0))
    lo = Image.new("L", (W, H), 0)
    lo.paste(bez.crop((0, H // 2, W, H)), (0, H // 2))
    case_img = Image.composite(Image.new("RGB", (W, H), (0x8E, 0x91, 0x99)), case_img,
                               hi.point(lambda p: int(p * 0.75)))
    case_img = Image.composite(Image.new("RGB", (W, H), (0x18, 0x18, 0x1A)), case_img,
                               lo.point(lambda p: int(p * 0.55)))

    # hueco rebajado
    p = STYLE["RECESS_PAD"]
    x0, y0 = GEOM["WIN_X"] - p, GEOM["WIN_Y"] - p
    x1, y1 = GEOM["WIN_X"] + GEOM["WIN_W"] + p, GEOM["WIN_Y"] + GEOM["CARD_H"] + p
    rec = Image.new("L", (W * 2, H * 2), 0)
    ImageDraw.Draw(rec).rounded_rectangle(
        [x0 * 2, y0 * 2, x1 * 2 - 1, y1 * 2 - 1], radius=20, fill=255)
    rec = rec.resize((W, H), Image.LANCZOS)
    case_img = Image.composite(Image.new("RGB", (W, H), STYLE["RECESS"]), case_img, rec)

    # sombra interior del hueco + canto iluminado en su borde inferior
    inner = Image.new("L", (W, H), 0)
    idr = ImageDraw.Draw(inner)
    for i in range(11):
        idr.rounded_rectangle([x0 + i, y0 + i, x1 - 1 - i, y1 - 1 - i],
                              radius=max(1, 11 - i),
                              outline=int(165 * (1 - i / 11.0) ** 1.4))
    inner = inner.filter(ImageFilter.GaussianBlur(1.3))
    case_img = Image.composite(Image.new("RGB", (W, H), (0, 0, 0)), case_img, inner)

    # ejes / carretes
    d = ImageDraw.Draw(case_img)
    axle_xs = [GEOM["WIN_X"] - 5,
               GEOM["WIN_X"] + GEOM["CARD_W"] + GEOM["GAP"] // 2,
               GEOM["WIN_X"] + GEOM["WIN_W"] + 5]
    cy = GEOM["WIN_Y"] + GEOM["CARD_H"] // 2
    for ax in axle_xs:
        d.rounded_rectangle([ax - 5, cy - 26, ax + 5, cy + 26], radius=4, fill=STYLE["AXLE"])
        for k, yv in enumerate(range(cy - 24, cy + 25, 4)):
            d.line([(ax - 4, yv), (ax + 4, yv)], fill=(0x30, 0x30, 0x35))
            d.line([(ax - 4, yv + 1), (ax + 4, yv + 1)], fill=(0x0F, 0x0F, 0x12))
        d.ellipse([ax - 6, cy - 7, ax + 6, cy + 7], fill=(0x26, 0x26, 0x2A),
                  outline=(0x45, 0x45, 0x4B))
        d.arc([ax - 6, cy - 7, ax + 6, cy + 7], 200, 320, fill=(0x6A, 0x6A, 0x72))
    return case_img


def draw_hooks(img):
    d = ImageDraw.Draw(img)
    for i in range(2):
        cx = GEOM["WIN_X"] + GEOM["CARD_W"] // 2 + i * (GEOM["CARD_W"] + GEOM["GAP"])
        y = GEOM["WIN_Y"]
        d.polygon([(cx - 7, y - 1), (cx + 7, y - 1), (cx, y + 9)], fill=(0x1B, 0x1B, 0x1E))
        d.line([(cx - 7, y - 1), (cx + 7, y - 1)], fill=(0x6E, 0x6E, 0x76))
    return img


# ---------------------------------------------------------------- escena
def compose_scene(hh, mm, date_text="SAB 22 AGO"):
    scene = render_case()
    for idx, val in ((0, hh), (1, mm)):
        card = card_image(val, idx)
        scene.paste(card, (GEOM["WIN_X"] + idx * (GEOM["CARD_W"] + GEOM["GAP"]),
                           GEOM["WIN_Y"]))
    draw_hooks(scene)
    if date_text:
        f = load_font(15)
        d = ImageDraw.Draw(scene)
        bb = d.textbbox((0, 0), date_text, font=f)
        d.text(((GEOM["SCR_W"] - (bb[2] - bb[0])) // 2, GEOM["DATE_Y"]),
               date_text, font=f, fill=STYLE["DATE_COLOR"])
    return scene


def compose_mid_flip(old_val, new_val, still_val, t, idx=1):
    """Fotograma simulado de un volteo, t en 0..1.

    idx es la tarjeta que vuela: 1 = minutos (el caso normal, una vez por
    minuto), 0 = horas. La otra tarjeta se dibuja quieta con still_val.
    """
    scene = render_case()
    old, new = card_image(old_val, idx), card_image(new_val, idx)
    W, H, half = GEOM["CARD_W"], GEOM["CARD_H"], GEOM["HALF_H"]
    y = GEOM["WIN_Y"]
    x = GEOM["WIN_X"] + idx * (W + GEOM["GAP"])
    x_still = GEOM["WIN_X"] + (1 - idx) * (W + GEOM["GAP"])

    scene.paste(new.crop((0, 0, W, half)), (x, y))          # fondo: mitad alta nueva
    scene.paste(old.crop((0, half, W, H)), (x, y + half))   # fondo: mitad baja vieja

    if t < 0.5:
        k = 1.0 - t / 0.5
        leaf = old.crop((0, 0, W, half))
    else:
        k = (t - 0.5) / 0.5
        leaf = new.crop((0, half, W, H))
    shade_amt = 190 * (1 - k)

    lh = max(1, int(half * k))
    leaf = leaf.resize((W, lh), Image.BILINEAR)
    leaf = Image.blend(leaf, Image.new("RGB", leaf.size, (0, 0, 0)), shade_amt / 255.0)
    scene.paste(leaf, (x, y + half - lh) if t < 0.5 else (x, y + half))

    scene.paste(card_image(still_val, 1 - idx), (x_still, y))
    draw_hooks(scene)
    return scene


# ---------------------------------------------------------------- salidas
def do_preview():
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    W, H, half = GEOM["CELL_W"], GEOM["CARD_H"], GEOM["HALF_H"]

    strip = Image.new("RGB", (W * 10, H * 3 + 30), STYLE["RECESS"])
    for i in range(10):
        top, bot = cell_halves(i, i % 4)
        strip.paste(top, (i * W, 0))
        strip.paste(bot, (i * W, half))
    big = Image.new("RGB", (W * 5, H), STYLE["RECESS"])
    for i in range(5):
        top, bot = cell_halves(i, 0)
        big.paste(top, (i * W, 0))
        big.paste(bot, (i * W, half))
    strip.paste(big.resize((W * 10, H * 2), Image.LANCZOS), (0, H + 30))
    strip.save(os.path.join(PREVIEW_DIR, "digits.png"))

    scene = compose_scene(2, 59)
    scene.save(os.path.join(PREVIEW_DIR, "scene.png"))
    scene.resize((GEOM["SCR_W"] * 2, GEOM["SCR_H"] * 2), Image.LANCZOS) \
         .save(os.path.join(PREVIEW_DIR, "scene_2x.png"))

    # el volteo de cada minuto: 02:58 -> 02:59, con la hora quieta
    frames = [compose_mid_flip(58, 59, 2, t, idx=1) for t in (0.28, 0.5, 0.78)]
    sheet = Image.new("RGB", (GEOM["SCR_W"], GEOM["SCR_H"] * 3 + 16), (0, 0, 0))
    for i, fr in enumerate(frames):
        sheet.paste(fr, (0, i * (GEOM["SCR_H"] + 8)))
    sheet.save(os.path.join(PREVIEW_DIR, "mid_flip.png"))

    print("fuente: %s (%s)" % (FONT_INFO.get("name"), FONT_INFO.get("variation")))
    print("preview escrito en %s" % PREVIEW_DIR)


def rgb565_swapped(img):
    """RGB888 -> RGB565 con los dos bytes intercambiados (LV_COLOR_FORMAT_RGB565_SWAPPED)."""
    a = np.asarray(img.convert("RGB"), dtype=np.uint16)
    v = ((a[..., 0] & 0xF8) << 8) | ((a[..., 1] & 0xFC) << 3) | (a[..., 2] >> 3)
    out = np.empty(v.size * 2, dtype=np.uint8)
    out[0::2] = (v >> 8).ravel()      # byte alto primero = swapped
    out[1::2] = (v & 0xFF).ravel()
    return out.tobytes()


def do_emit():
    """
    Los 81 sprites suman ~1,9 MB. Como arrays C en hexadecimal serian ~11 MB de
    fuente y una compilacion larguisima, asi que van a un blob binario que se
    embebe con .incbin: el .c solo contiene los descriptores, que apuntan a
    offsets dentro del blob.
    """
    os.makedirs(ASSETS_DIR, exist_ok=True)
    blob = bytearray()
    entries = []          # (offset, w, h) en el mismo orden que la tabla del .c

    def add(img):
        off = len(blob)
        blob.extend(rgb565_swapped(img))
        entries.append((off, img.size[0], img.size[1]))
        return off

    for pos in range(4):
        for dg in range(10):
            top, bot = cell_halves(dg, pos)
            add(top)
            add(bot)
    case_off = add(render_case())

    with open(os.path.join(ASSETS_DIR, "flip_assets.bin"), "wb") as fh:
        fh.write(blob)

    with open(os.path.join(ASSETS_DIR, "flip_assets.S"), "w") as fh:
        fh.write("/* generado por tools/gen_assets.py - no editar */\n"
                 "    .section .rodata.flip_assets,\"a\",@progbits\n"
                 "    .global flip_assets_blob\n"
                 "    .align 4\n"
                 "flip_assets_blob:\n"
                 '    .incbin "src/assets/flip_assets.bin"\n')

    def dsc(off, w, h, indent):
        return ("%s{ .header = { .magic = LV_IMAGE_HEADER_MAGIC,\n"
                "%s              .cf = LV_COLOR_FORMAT_RGB565_SWAPPED,\n"
                "%s              .w = %d, .h = %d, .stride = %d },\n"
                "%s  .data_size = %d, .data = flip_assets_blob + %d }"
                % (indent, indent, indent, w, h, w * 2, indent, w * h * 2, off))

    with open(os.path.join(ASSETS_DIR, "flip_assets.c"), "w") as fh:
        fh.write('/* generado por tools/gen_assets.py - no editar */\n'
                 '#include "flip_assets.h"\n\n'
                 'extern const uint8_t flip_assets_blob[];\n\n'
                 '/* [columna 0..3][digito 0..9][0 = mitad alta, 1 = mitad baja] */\n'
                 'lv_image_dsc_t img_digit[4][10][2] = {\n')
        i = 0
        for pos in range(4):
            fh.write("  { /* columna %d */\n" % pos)
            for dg in range(10):
                fh.write("    {\n")
                for k in range(2):
                    off, w, h = entries[i]
                    i += 1
                    fh.write(dsc(off, w, h, "      ") + (",\n" if k == 0 else "\n"))
                fh.write("    },\n")
            fh.write("  },\n")
        fh.write("};\n\n")
        off, w, h = entries[-1]
        fh.write("lv_image_dsc_t img_case_bg =\n" + dsc(off, w, h, "    ") + ";\n")

    with open(os.path.join(ASSETS_DIR, "flip_assets.h"), "w") as fh:
        fh.write("/* generado por tools/gen_assets.py - no editar */\n"
                 "#pragma once\n#include <lvgl.h>\n\n"
                 "#define FLIP_CELL_W %d\n#define FLIP_HALF_H %d\n"
                 "#define FLIP_CARD_W %d\n#define FLIP_CARD_H %d\n"
                 "#define FLIP_WIN_X  %d\n#define FLIP_WIN_Y  %d\n"
                 "#define FLIP_GAP    %d\n#define FLIP_DATE_Y %d\n\n"
                 "/* Los descriptores NO son const: flip_assets_use_psram()\n"
                 " * rebasa sus punteros cuando el blob se duplica en PSRAM. */\n"
                 "extern lv_image_dsc_t img_digit[4][10][2];\n"
                 "extern lv_image_dsc_t img_case_bg;\n"
                 % (GEOM["CELL_W"], GEOM["HALF_H"], GEOM["CARD_W"], GEOM["CARD_H"],
                    GEOM["WIN_X"], GEOM["WIN_Y"], GEOM["GAP"], GEOM["DATE_Y"]))

    print("blob: %.2f MB en %d sprites (case_bg en offset %d)"
          % (len(blob) / 1048576.0, len(entries), case_off))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--emit", action="store_true")
    a = ap.parse_args()
    if not (a.preview or a.emit):
        ap.error("usa --preview o --emit")
    if a.preview:
        do_preview()
    if a.emit:
        do_emit()
