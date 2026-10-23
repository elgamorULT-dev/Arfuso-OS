/* Arfuso OS - Pilote graphique framebuffer avec double tampon */
#include "fb.h"
#include "mouse.h"
#include "cursor.h"
#include "generated/logo_data.h"

int FB_W = 0, FB_H = 0;
static u32 fb_pitch = 0;            /* octets par ligne à l'écran */
static volatile u8 *fbp = 0;        /* mémoire vidéo physique */
static u8 fb_bpp = 32;

#define MAX_W 1280
#define MAX_H 1024
static u32 bb[MAX_W * MAX_H];       /* tampon mémoire (0x00RRGGBB par pixel) */

/* Infos du mode VESA fournies par le chargeur (bloc en 0x0600) */
void fb_init(void) {
    const u8 *vbe = (const u8 *)0x600;
    fb_pitch  = *(const u16 *)(vbe + 0x10);
    FB_W      = *(const u16 *)(vbe + 0x12);
    FB_H      = *(const u16 *)(vbe + 0x14);
    fb_bpp    = *(const u8  *)(vbe + 0x19);
    fbp       = (volatile u8 *)(uintptr_t)(*(const u32 *)(vbe + 0x28));
    if (FB_W <= 0 || FB_W > MAX_W) FB_W = MAX_W;
    if (FB_H <= 0 || FB_H > MAX_H) FB_H = MAX_H;
    memset(bb, 0, (u64)FB_W * FB_H * 4);
}

void fb_flush(void) {
    if (!fbp) return;
    if (fb_bpp == 32) {
        for (int y = 0; y < FB_H; y++) {
            memcpy((void *)(fbp + (u64)y * fb_pitch), &bb[y * FB_W], (u32)FB_W * 4);
        }
    } else if (fb_bpp == 24) {
        for (int y = 0; y < FB_H; y++) {
            volatile u8 *row = fbp + (u64)y * fb_pitch;
            for (int x = 0; x < FB_W; x++) {
                u32 c = bb[y * FB_W + x];
                row[x * 3 + 0] = c & 0xFF;
                row[x * 3 + 1] = (c >> 8) & 0xFF;
                row[x * 3 + 2] = (c >> 16) & 0xFF;
            }
        }
    } else { /* 16 bits 565 */
        for (int y = 0; y < FB_H; y++) {
            volatile u16 *row = (volatile u16 *)(fbp + (u64)y * fb_pitch);
            for (int x = 0; x < FB_W; x++) {
                u32 c = bb[y * FB_W + x];
                row[x] = ((c >> 3) & 0x1F) | (((c >> 10) & 0x3F) << 5) | (((c >> 19) & 0x1F) << 11);
            }
        }
    }
    /* Curseur souris : dessiné directement en mémoire vidéo, par-dessus tout */
    if (CURSOR_ON) {
        for (int j = 0; j < CURSOR_H; j++)
            for (int i = 0; i < CURSOR_W; i++) {
                char ch = CURSOR_ROWS[j][i];
                if (ch == ' ') continue;
                int x = MX + i, y = MY + j;
                if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) continue;
                u32 c = (ch == 'X') ? 0x000000 : 0xFFFFFF;
                u64 off = (u64)y * fb_pitch;
                if (fb_bpp == 32) {
                    *(volatile u32 *)(fbp + off + (u64)x * 4) = c;
                } else if (fb_bpp == 24) {
                    volatile u8 *q = fbp + off + (u64)x * 3;
                    q[0] = c & 0xFF; q[1] = (c >> 8) & 0xFF; q[2] = (c >> 16) & 0xFF;
                } else {
                    *(volatile u16 *)(fbp + off + (u64)x * 2) =
                        ((c >> 3) & 0x1F) | (((c >> 10) & 0x3F) << 5) | (((c >> 19) & 0x1F) << 11);
                }
            }
    }
}

/* ---- Primitives de dessin ---- */
void px(int x, int y, u32 c) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
    bb[y * FB_W + x] = c;
}

void rect_fill(int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            bb[(y + j) * FB_W + (x + i)] = c;
}

void rr_fill(int x, int y, int w, int h, int r, u32 c) {
    rect_fill(x + r, y, w - 2 * r, h, c);
    rect_fill(x, y + r, w, h - 2 * r, c);
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            int d2 = (r - i) * (r - i) + (r - j) * (r - j);
            if (d2 <= r * r + r) {
                px(x + i, y + j, c);
                px(x + w - 1 - i, y + j, c);
                px(x + i, y + h - 1 - j, c);
                px(x + w - 1 - i, y + h - 1 - j, c);
            }
        }
}

void circle_fill(int cx, int cy, int r, u32 c) {
    for (int j = -r; j <= r; j++) {
        int dj = j * j;
        for (int i = -r; i <= r; i++)
            if (i * i + dj <= r * r) px(cx + i, cy + j, c);
    }
}

void ring(int cx, int cy, int r_out, int r_in, u32 c) {
    for (int j = -r_out; j <= r_out; j++) {
        int dj = j * j;
        for (int i = -r_out; i <= r_out; i++) {
            int d = i * i + dj;
            if (d <= r_out * r_out && d >= r_in * r_in) px(cx + i, cy + j, c);
        }
    }
}

void thick_line(int x0, int y0, int x1, int y1, int th, u32 c) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy) ? (dx > 0 ? dx : -dx) : (dy > 0 ? dy : -dy);
    if (steps < 1) steps = 1;
    for (int s = 0; s <= steps; s++)
        circle_fill(x0 + dx * s / steps, y0 + dy * s / steps, th / 2, c);
}

/* Symbole wifi : point + 3 arcs (cone de +/-45° vers le haut) */
void wifi_arcs(int cx, int cy, int r, u32 c) {
    circle_fill(cx, cy, r / 7 + 1, c);
    for (int k = 1; k <= 3; k++) {
        int ro = r * k / 3, ri = ro - r / 9 - 1;
        for (int j = -ro; j <= 0; j++)
            for (int i = -ro; i <= ro; i++) {
                int d = i * i + j * j;
                if (d <= ro * ro && d >= ri * ri && (-j) >= (i > 0 ? i : -i))
                    px(cx + i, cy + j, c);
            }
    }
}

/* Remplissage de polygone (scanline) */
void poly_fill(const int *vx, const int *vy, int n, u32 c) {
    int ymin = vy[0], ymax = vy[0];
    for (int i = 1; i < n; i++) {
        if (vy[i] < ymin) ymin = vy[i];
        if (vy[i] > ymax) ymax = vy[i];
    }
    for (int y = ymin; y <= ymax; y++) {
        int xs[16], m = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            if ((vy[i] <= y && vy[j] > y) || (vy[j] <= y && vy[i] > y)) {
                xs[m++] = vx[i] + (y - vy[i]) * (vx[j] - vx[i]) / (vy[j] - vy[i]);
            }
        }
        for (int a = 0; a < m - 1; a++)
            for (int b = a + 1; b < m; b++)
                if (xs[b] < xs[a]) { int t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
        for (int a = 0; a + 1 < m; a += 2)
            for (int x = xs[a]; x <= xs[a + 1]; x++) px(x, y, c);
    }
}

/* ---- Logo Arfuso ---- */
void draw_logo(int x, int y, int size) {
    if (size < 8) size = 8;
    for (int j = 0; j < size; j++) {
        int sy = j * LOGO_H / size;
        for (int i = 0; i < size; i++) {
            u8 idx = logo_pixels[sy * LOGO_W + i * LOGO_W / size];
            if (idx == 255) continue;
            u32 c = ((u32)logo_palette[idx * 3] << 16) |
                    ((u32)logo_palette[idx * 3 + 1] << 8) |
                    (u32)logo_palette[idx * 3 + 2];
            px(x + i, y + j, c);
        }
    }
}

/* ---- Texte (UTF-8, polices bitmap 1bpp générées) ---- */

static int glyph_index(const ArfFont *f, u32 cp) {
    for (int i = 0; i < (int)f->n; i++)
        if (f->cps[i] == cp) return i;
    return -1;
}

static const u8 *glyph_bits(const ArfFont *f, int idx) {
    const u8 *p = f->bits;
    for (int j = 0; j < idx; j++) {
        int pitch = (f->wpitch[j] + 7) / 8;
        p += (u32)pitch * f->height;
    }
    return p;
}

static u32 next_cp(const char **s) {
    const u8 *p = (const u8 *)*s;
    u32 cp;
    if (p[0] < 0x80) { cp = p[0]; (*s) += 1; }
    else if (p[0] < 0xE0) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); (*s) += 2; }
    else { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); (*s) += 3; }
    return cp;
}

int text_w(const ArfFont *f, const char *s) {
    int w = 0;
    while (*s) {
        u32 cp = next_cp(&s);
        int i = glyph_index(f, cp);
        if (i >= 0) w += f->adv[i];
    }
    return w;
}

void text(const ArfFont *f, int x, int y, const char *s, u32 c) {
    int cx = x;
    while (*s) {
        u32 cp = next_cp(&s);
        int i = glyph_index(f, cp);
        if (i < 0) continue;
        int w = f->wpitch[i];
        int pitch = (w + 7) / 8;
        const u8 *bits = glyph_bits(f, i);
        for (int j = 0; j < f->height; j++)
            for (int k = 0; k < w; k++) {
                u8 byte = bits[j * pitch + (k >> 3)];
                if (byte & (0x80 >> (k & 7))) px(cx + k, y + j, c);
            }
        cx += f->adv[i];
    }
}

void text_center(const ArfFont *f, int cx, int y, const char *s, u32 c) {
    text(f, cx - text_w(f, s) / 2, y, s, c);
}

void text_right(const ArfFont *f, int x2, int y, const char *s, u32 c) {
    text(f, x2 - text_w(f, s), y, s, c);
}
