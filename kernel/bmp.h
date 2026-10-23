/* Arfuso OS - Images BMP 24 bits : décodage, mise en cache plein écran */
#pragma once
#include "sys.h"

/* Charge un BMP 24 bits (déjà lu en mémoire) et l'étire dans un cache
   1024x768. Renvoie 1 si OK. */
int bmp_fit_to_cache(const u8 *bmp, int len);

/* Recopie le cache plein écran dans le backbuffer (le wallpaper). */
void wallpaper_blit(void);

/* 1 si un wallpaper image est chargé (sinon couleur unie). */
extern int wallpaper_active;
extern u32 wallpaper_cache[1024 * 768];

/* Charge Images/<wallpaper_file> dans le cache fond d'écran */
void wallpaper_load_file(void);
