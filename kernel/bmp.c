/* Arfuso OS - Lecteur BMP 24 bits non compressé + cache wallpaper plein écran */
#include "bmp.h"
#include "fb.h"
#include "sys.h"

int wallpaper_active = 0;
u32 wallpaper_cache[1024 * 768];

static u32 rd32(const u8 *p) {
    return p[0] | (p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static i32 rds32(const u8 *p) { return (i32)rd32(p); }

/* Étire un BMP 24 bits vers le cache écran (nearest neighbor, bilatéral doux) */
int bmp_fit_to_cache(const u8 *bmp, int len) {
    if (len < 54 || bmp[0] != 'B' || bmp[1] != 'M') return 0;
    u32 off = rd32(bmp + 10);
    i32 w = rds32(bmp + 18);
    i32 h = rds32(bmp + 22);
    u16 bpp = bmp[28] | (bmp[29] << 8);
    u32 comp = rd32(bmp + 30);
    if (bpp != 24 || comp != 0 || w <= 0 || h == 0) return 0;
    int topdown = h < 0;
    if (h < 0) h = -h;
    if (w > 4096 || h > 4096) return 0;
    u32 rowstride = ((u32)w * 3 + 3) & ~3u;
    if (off + rowstride * (u32)h > (u32)len) return 0;
    for (int y = 0; y < 1024 * 0 + FB_H; y++) {
        int sy = (int)(((u32)y * (u32)h) / (u32)FB_H);
        if (!topdown) sy = h - 1 - sy;
        const u8 *src = bmp + off + rowstride * (u32)sy;
        for (int x = 0; x < FB_W; x++) {
            int sx = (int)(((u32)x * (u32)w) / (u32)FB_W);
            const u8 *p = src + (u32)sx * 3;
            wallpaper_cache[y * 1024 + x] =
                ((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0];
        }
    }
    wallpaper_active = 1;
    return 1;
}

void wallpaper_blit(void) {
    for (int y = 0; y < FB_H; y++) {
        const u32 *row = wallpaper_cache + y * 1024;
        for (int x = 0; x < FB_W; x++) px(x, y, row[x]);
    }
}
