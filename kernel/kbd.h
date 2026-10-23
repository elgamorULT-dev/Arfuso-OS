/* Arfuso OS - Pilote clavier PS/2 (scrutation, sans interruptions) */
#pragma once
#include "sys.h"

#define KEY_UP    0x101
#define KEY_DOWN  0x102
#define KEY_LEFT  0x103
#define KEY_RIGHT 0x104

/* Retourne le code de la touche pressée (ASCII ou KEY_*), 0 si aucune */
int kbd_poll(void);

/* Initialise le contrôleur clavier i8042 (activation du balayage) */
void kbd_init(void);
