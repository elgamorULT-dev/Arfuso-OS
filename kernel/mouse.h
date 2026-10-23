/* Arfuso OS - Pilote souris PS/2 (paquets 3 octets, scrutation) */
#pragma once
#include "sys.h"

extern int MX, MY;          /* position du curseur */
extern int CURSOR_ON;       /* afficher le curseur */

void mouse_init(void);

/* Lit tous les octets souris en attente.
   Retour : bit0 = déplacement, bit1 = clic gauche (pression) */
int mouse_event(void);
