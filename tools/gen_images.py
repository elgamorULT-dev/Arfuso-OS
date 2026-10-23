#!/usr/bin/env python3
"""Convertit les images jointes (PJ11 lancement, PJ2 fond d'ecran) en BMP 24 bits
   1024x768 pour l'ISO Arfuso : BOOT.BMP (ecran de lancement) et FOND.BMP (fond)."""
import os, struct, sys
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..")
UP = "/home/user/uploads"

def to_bmp24(src, dst, w=1024, h=768):
    im = Image.open(src).convert("RGB")
    im = im.resize((w, h), Image.LANCZOS)
    row = w * 3
    pad = (4 - row % 4) % 4
    datasize = (row + pad) * h
    offbits = 54
    filesize = offbits + datasize
    with open(dst, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<IHHI", filesize, 0, 0, offbits))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, datasize,
                            2835, 2835, 0, 0))  # h négatif = top-down
        pxg = im.load()
        for y in range(h):
            line = bytearray()
            for x in range(w):
                r, g, b = pxg[x, y]
                line += bytes((b, g, r))
            line += b"\x00" * pad
            f.write(line)
    print(f"  -> {os.path.basename(dst)} ({filesize} o)")

os.makedirs(os.path.join(ROOT, "iso_root"), exist_ok=True)
to_bmp24(os.path.join(UP, "Sans titre 22_20261022155631.png"),
         os.path.join(ROOT, "iso_root", "FOND.BMP"))
to_bmp24(os.path.join(UP, "Sans titre 19_20261021155107.png"),
         os.path.join(ROOT, "iso_root", "BOOT.BMP"))
