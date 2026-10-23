#pragma once
#include "ata.h"

/* Assistant d'installation Arfuso (boot CD-ROM uniquement, piloté à la souris) */
void installer_wizard(void);   /* ne retourne pas : redémarrage ou extinction */

/* Relit la configuration écrite sur un disque installé (édition, plateforme,
   clavier, langue, fuseau). Renvoie 1 si le disque est un disque Arfuso. */
int installer_read_config(AtaDisk *d, u8 *edition, u8 *platform,
                          u8 *kbd, u8 *lang, i32 *tz, u8 *booted);
/* Marque le premier démarrage comme terminé (secteur persistant). */
void installer_set_booted(AtaDisk *d);

/* Paramètres globaux choisis à l'installation (définis dans installer.c) */
extern int g_lang;       /* 0 = Français, 1 = English            */
extern int g_kbd;        /* 0 = AZERTY (FR), 1 = QWERTY (EN)     */
extern int g_tz;         /* index du fuseau dans l'installateur  */
extern int g_platform;   /* 0 = Ordinateur, 1 = Appareil mobile  */

/* Réglages persistés par l'OS installé (secteur config, octets 16+) */
int  installer_cfg_get(AtaDisk *d, int off, u8 *val);
void installer_cfg_set(AtaDisk *d, int off, u8 val);
