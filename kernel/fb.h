/* Arfuso OS - Pilote graphique (framebuffer VESA) */
#pragma once
#include "sys.h"
#include "generated/font_data.h"

extern int FB_W, FB_H;

void fb_init(void);
void fb_flush(void);

void px(int x, int y, u32 c);
void rect_fill(int x, int y, int w, int h, u32 c);
void rr_fill(int x, int y, int w, int h, int r, u32 c);
void circle_fill(int cx, int cy, int r, u32 c);
void ring(int cx, int cy, int r_out, int r_in, u32 c);
void thick_line(int x0, int y0, int x1, int y1, int th, u32 c);
void wifi_arcs(int cx, int cy, int r, u32 c);
void poly_fill(const int *vx, const int *vy, int n, u32 c);

/* Logo Arfuso (assets générés) */
void draw_logo(int x, int y, int size);

/* Texte UTF-8 (polices bitmap générées : FONT_SM, FONT_UI, FONT_BTN, FONT_TTL, FONT_CLK) */
int  text_w(const ArfFont *f, const char *s);
void text(const ArfFont *f, int x, int y, const char *s, u32 c);
void text_center(const ArfFont *f, int cx, int y, const char *s, u32 c);
void text_right(const ArfFont *f, int x2, int y, const char *s, u32 c);
