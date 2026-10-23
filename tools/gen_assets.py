#!/usr/bin/env python3
# Arfuso OS - Générateur d'assets pour le noyau (polices 1bpp, logo palettisé, couleurs)
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'kernel', 'generated')
os.makedirs(OUT, exist_ok=True)

FONT_DIR = '/usr/share/fonts/truetype/dejavu'
FONTS = {
    'sm':  (os.path.join(FONT_DIR, 'DejaVuSans.ttf'),      17),
    'ui':  (os.path.join(FONT_DIR, 'DejaVuSans.ttf'),      24),
    'btn': (os.path.join(FONT_DIR, 'DejaVuSans-Bold.ttf'), 28),
    'ttl': (os.path.join(FONT_DIR, 'DejaVuSans-Bold.ttf'), 42),
    'clk': (os.path.join(FONT_DIR, 'DejaVuSans-Bold.ttf'), 170),
}
BASE_CHARS = "".join(chr(c) for c in range(32, 127)) + "éèêëàâäçîïôöùûüÉÈÊÀÇ’'«»°…→←↑↓•·"
CHARSETS = {
    'sm': BASE_CHARS,
    'ui': BASE_CHARS,
    'btn': BASE_CHARS + "É",
    'ttl': BASE_CHARS,
    'clk': "0123456789h",
}

def render_font(name, path, size, chars):
    font = ImageFont.truetype(path, size)
    asc, desc = font.getmetrics()
    H = asc + desc
    advances, cps, blobs = [], [], []
    for ch in chars:
        adv = max(1, int(round(font.getlength(ch))))
        img = Image.new('1', (adv + 2, H), 0)
        d = ImageDraw.Draw(img)
        d.text((1, 0), ch, font=font, fill=1)
        # pack rows MSB-first, row pitch = adv+2
        w = adv + 2
        px = img.load()
        data = bytearray()
        for y in range(H):
            byte = 0; bit = 0
            for x in range(w):
                byte = (byte << 1) | (1 if px[x, y] else 0)
                bit += 1
                if bit == 8:
                    data.append(byte); byte = 0; bit = 0
            if bit:
                data.append(byte << (8 - bit))
        advances.append(adv)         # largeur de tracé (pas de la "pitch" interne)
        cps.append(ord(ch))
        blobs.append(bytes(data))
    return {'h': H, 'adv': advances, 'cps': cps, 'pitch': None, 'blobs': blobs,
            'advances_w': [a + 2 for a in advances]}

def c_arr(name, data, ctype='uint8_t', per=12):
    lines = []
    for i in range(0, len(data), per):
        lines.append('  ' + ','.join(str(b) for b in data[i:i+per]) + ',')
    return f'static const {ctype} {name}[{len(data)}] = {{\n' + '\n'.join(lines) + '\n};\n'

fonts_h = ['// AUTO-GENERATED - ne pas éditer\n#pragma once\n#include <stdint.h>\n',
           'typedef struct { const uint8_t *bits; const uint8_t *adv; const uint8_t *wpitch; const uint32_t *cps; uint8_t n; uint16_t height; } ArfFont;\n']
for name, (path, size) in FONTS.items():
    f = render_font(name, path, size, CHARSETS[name])
    allbits = bytearray()
    for b in f['blobs']:
        allbits += b
    fonts_h.append(c_arr(f'f_{name}_bits', allbits))
    fonts_h.append(c_arr(f'f_{name}_adv', f['adv']))
    fonts_h.append(c_arr(f'f_{name}_wp', f['advances_w']))
    fonts_h.append(c_arr(f'f_{name}_cps', f['cps'], 'uint32_t', 10))
    fonts_h.append(f'static const ArfFont FONT_{name.upper().replace("SM","SM").replace("UI","UI").replace("BTN","BTN").replace("TTL","TTL").replace("CLK","CLK")} = {{ f_{name}_bits, f_{name}_adv, f_{name}_wp, f_{name}_cps, {len(f["cps"])}, {f["h"]} }};\n')

with open(os.path.join(OUT, 'font_data.h'), 'w') as fh:
    fh.write('\n'.join(fonts_h))
print('font_data.h ok:', os.path.getsize(os.path.join(OUT, 'font_data.h')), 'octets')

# ---- Logo : palettisé 16 couleurs, 180x180, pixels sombres -> transparent ----
LOGO_SIZE = 180
logo = Image.open(os.path.join(ROOT, 'assets', 'logo.png')).convert('RGB')
side = min(logo.size)
logo = logo.crop(((logo.width - side)//2, (logo.height - side)//2,
                  (logo.width + side)//2, (logo.height + side)//2)).resize((LOGO_SIZE, LOGO_SIZE), Image.LANCZOS)
q = logo.quantize(colors=15)
pal_img = q.getpalette()[:15*3]
palette = [(pal_img[i*3], pal_img[i*3+1], pal_img[i*3+2]) for i in range(15)]
# couleur 0xFF = index transparent ; index 0..14 sinon
idx = list(q.getdata())
TRANSPARENT = 255
# référence de fond = pixel du coin, tolérance large
bg = logo.getpixel((2, 2))
def is_bg(p):
    return sum((p[i] - bg[i]) ** 2 for i in range(3)) < 90 ** 2
pxout = bytearray()
for y in range(LOGO_SIZE):
    for x in range(LOGO_SIZE):
        if is_bg(logo.getpixel((x, y))):
            pxout.append(TRANSPARENT)
        else:
            pxout.append(idx[y * LOGO_SIZE + x])
pal24 = bytearray()
for (r, g, b) in palette:
    pal24 += bytes([r, g, b])
logo_h = ['// AUTO-GENERATED\n#pragma once\n#include <stdint.h>\n',
          f'#define LOGO_W {LOGO_SIZE}\n#define LOGO_H {LOGO_SIZE}\n',
          c_arr('logo_pixels', pxout),
          c_arr('logo_palette', pal24)]
with open(os.path.join(OUT, 'logo_data.h'), 'w') as fh:
    fh.write('\n'.join(logo_h))
print('logo_data.h ok:', os.path.getsize(os.path.join(OUT, 'logo_data.h')), 'octets')

# ---- Couleurs extraites des maquettes ----
colors_h = """// AUTO-GENERATED - couleurs des maquettes Arfuso
#pragma once
#define COL_RED     0xFF0C00   // fond verrouillage / connexion
#define COL_BAR     0xB1B1B1   // barre d'état
#define COL_DOCK    0x3A3A3A   // dock latéral
#define COL_BLUE    0x2296E2   // bleu du logo
#define COL_WHITE   0xFFFFFF
#define COL_BLACK   0x000000
#define COL_PLACEH  0x9E9E9E   // texte placeholder
#define COL_DARKTXT 0x212121
"""
with open(os.path.join(OUT, 'colors.h'), 'w') as fh:
    fh.write(colors_h)
print('colors.h ok')
