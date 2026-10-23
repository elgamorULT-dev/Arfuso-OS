/*
 * Arfuso OS v1.2 - Assistant d'installation (Images 1.1 a 1.7 du cahier)
 * =====================================================================
 * Apparaît UNIQUEMENT lors d'un boot sur le CD-ROM. Le système
 * ne fonctionne pas sans avoir été installé sur un disque.
 * Pilotage à la SOURIS (obligatoire) : cases à cocher exclusives
 * (1 choix par écran), listes déroulantes, boutons.
 * Flux : Langue -> Édition -> Plateforme -> Disque -> Copie -> Install -> Redémarrer
 */
#include "sys.h"
#include "fb.h"
#include "kbd.h"
#include "mouse.h"
#include "rtc.h"
#include "ata.h"
#include "apps.h"
#include "generated/colors.h"
#include "generated/mbr_image.h"
#include "installer.h"
#include "fs.h"
#include "bmp.h"

/* Tampon de copie (jusqu'à 4 Mo) en mémoire haute identité */
#define STAGING ((u8 *)0x1000000)
#define STAGING_MAX (4 * 1024 * 1024)

#ifndef KSECTORS_VAL
#define KSECTORS_VAL 1
#endif

#define GRAY_TXT   0x757575
#define WIN_BG     0xFAFAFA
#define WIN_EDGE   0x202020
#define FIELD_BG   0xFFFFFF

/* ---- Paramètres choisis ---- */
int g_lang = 0;         /* 0 = Français, 1 = English */
int g_kbd  = 0;         /* 0 = Français (AZERTY), 1 = English (QWERTY) */
int g_tz   = 1;         /* index fuseau : 1 = Paris (UTC+1)            */
int g_platform = 0;     /* 0 = Ordinateur, 1 = Appareil mobile         */
extern int edition;     /* dans kernel.c (0..3)                        */
int ins_disk = 0;

extern AtaDisk the_disk;
extern int disk_ok;

static AtaDisk disks[8];
static int ndisks = 0;

/* ---- Libellés FR / EN ---- */
typedef struct { const char *fr; const char *en; } L;
static const char *T(L l) { return g_lang == 0 ? l.fr : l.en; }

static const L L_TITLE   = {"Arfuso installateur", "Arfuso Installer"};
static const L L_THANKS  = {"Nous vous remercions d'avoir choisi Arfuso.",
                            "Thank you for choosing Arfuso."};
static const L L_CHOOSELG= {"Choisissez votre langue :", "Choose your language:"};
static const L L_LANG    = {"Langage", "Language"};
static const L L_KBD     = {"Clavier", "Keyboard"};
static const L L_TZ      = {"Heure", "Time zone"};
static const L L_EDITION = {"Choisissez votre édition :", "Choose your edition:"};
static const L L_WARN    = {"Attention : pour changer d'édition par la suite,",
                            "Warning: to change edition later,"};
static const L L_WARN2   = {"vous devez réinstaller Arfuso.", "you must reinstall Arfuso."};
static const L L_PLATQ   = {"Sur quelle plateforme souhaitez-vous",
                            "On which platform do you want"};
static const L L_PLATQ2  = {"utiliser Arfuso ?", "to use Arfuso?"};
static const L L_PC      = {"Ordinateur", "Computer"};
static const L L_MOBILE  = {"Appareil mobile", "Mobile device"};
static const L L_DISKQ   = {"Sélectionnez le disque sur lequel installer :",
                            "Select the disk to install to:"};
static const L L_NODISK  = {"Aucun disque ATA détecté !", "No ATA disk found!"};
static const L L_NODISK2 = {"Ajoutez un disque dur à la machine et réessayez.",
                            "Add a hard disk to the machine and retry."};
static const L L_COPY    = {"Copie des fichiers", "Copying files"};
static const L L_LEFT    = {"Temps restant : 0 h 0 min", "Time left: 0 h 0 min"};
static const L L_INSTALL = {"Installation d'Arfuso", "Installing Arfuso"};
static const L L_REBT    = {"(Votre appareil peut redémarrer plusieurs reprises)",
                            "(Your device may restart several times)"};
static const L L_DONE1   = {"L'installation est terminée.",
                            "The installation is complete."};
static const L L_DONE2   = {"Nous vous demandons de retirer le cd-rom et de redémarrer",
                            "Please remove the CD-ROM and restart"};
static const L L_DONE3   = {"votre appareil en cliquant ci-dessous.",
                            "your device by clicking below."};
static const L L_FMT     = {"Le formater et installer", "Format and install"};
static const L L_INSTALLB= {"Installer", "Install"};
static const L L_REBOOT  = {"Redémarrer", "Restart"};
static const L L_BACK    = {"Retour", "Back"};

static const char *TZ_NAMES[4]  = {"Londres (UTC+0)", "Paris (UTC+1)", "Moscou (UTC+3)", "Tokyo (UTC+9)"};
static const int   TZ_OFF[4]    = {0, 1, 3, 9};
static const char *KBD_NAMES[2] = {"Français (AZERTY)", "English (QWERTY)"};
static const char *LG_NAMES[2]  = {"Français", "English"};
static const char *ED_NAMES_L[4]= {"Professionnel", "Familial", "Éducation", "Gaming"};

/* ---- Widgets ---- */

static void win_frame(void) {
    rect_fill(0, 0, FB_W, FB_H, 0x303030);
    int wx = FB_W / 2 - 350, wy = 70, ww = 700, wh = FB_H - 160;
    /* cadre + barre de titre : X - à gauche, titre à droite */
    rr_fill(wx - 3, wy - 3, ww + 6, wh + 6, 4, WIN_EDGE);
    rr_fill(wx, wy, ww, wh, 3, WIN_BG);
    rr_fill(wx, wy, ww, 30, 3, 0xE8E8E8);
    rect_fill(wx, wy + 30, ww, 2, 0xB0B0B0);
    rr_fill(wx + 2, wy + 2, 28, 28, 2, 0xC8C8C8);
    text(&FONT_SM, wx + 8, wy + 6, "x", COL_DARKTXT);
    zone_add(wx + 2, wy + 2, 28, 28, 970);          /* fermer -> extinction */
    rr_fill(wx + 30, wy + 2, 24, 28, 2, 0xC8C8C8);
    text(&FONT_SM, wx + 37, wy + 6, "-", COL_DARKTXT);
    text_right(&FONT_UI, wx + ww - 14, wy + 6, T(L_TITLE), COL_DARKTXT);
}

static void ui_button(int x, int y, int w, const char *label, u32 bg, int id) {
    rr_fill(x - 2, y - 2, w + 4, 34, 4, WIN_EDGE);
    rr_fill(x, y, w, 30, 3, bg);
    text_center(&FONT_UI, x + w / 2, y + 8, label, COL_WHITE);
    zone_add(x, y, w, 30, id);
}

static void ui_checkbox(int x, int y, const char *label, int checked, int id) {
    rr_fill(x - 1, y - 1, 20, 20, 2, COL_DARKTXT);
    rr_fill(x + 1, y + 1, 16, 16, 2, FIELD_BG);
    if (checked) {
        rect_fill(x + 4, y + 4, 10, 10, COL_BLUE);
    }
    text(&FONT_UI, x + 28, y + 2, label, COL_DARKTXT);
    zone_add(x - 4, y - 4, 420, 26, id);
}

static int dd_open = -1;   /* id de la liste déroulante ouverte (-1 = aucune) */

static void ui_dropdown(int x, int y, int w, const char *label,
                        const char *value, int id, int open) {
    if (label) text(&FONT_UI, x, y + 7, label, COL_DARKTXT);
    rr_fill(x + 110, y, w, 30, 3, COL_DARKTXT);
    rr_fill(x + 112, y + 2, w - 4, 26, 2, FIELD_BG);
    text(&FONT_UI, x + 122, y + 7, value, COL_DARKTXT);
    text_right(&FONT_UI, x + 110 + w - 8, y + 7, "v", COL_DARKTXT);
    zone_add(x + 110, y, w, 30, id);
    (void)open;
}

/* ================================================================== */
/* ÉCRAN 1.1 : Bienvenue + langue/clavier/heure                        */
/* ================================================================== */

static int screen_inst_welcome(void) {
    for (;;) {
        zones_clear();
        win_frame();
        int wx = FB_W / 2 - 350, wy = 70;
        draw_logo(wx + 90, wy + 90, 110);
        ui_button(wx + 490, wy + 90, 150, T(L_INSTALLB), COL_BLUE, 900);
        text(&FONT_UI, wx + 60, wy + 260, T(L_THANKS), COL_DARKTXT);
        text(&FONT_UI, wx + 60, wy + 296, T(L_CHOOSELG), COL_DARKTXT);
        ui_dropdown(wx + 60, wy + 340, 300, T(L_LANG), LG_NAMES[g_lang], 910, dd_open == 910);
        ui_dropdown(wx + 60, wy + 396, 300, T(L_KBD),  KBD_NAMES[g_kbd], 911, dd_open == 911);
        ui_dropdown(wx + 60, wy + 452, 300, T(L_TZ),   TZ_NAMES[g_tz],   912, dd_open == 912);

        if (dd_open >= 0) {
            const char **opts = (dd_open == 910) ? LG_NAMES : (dd_open == 911) ? KBD_NAMES : TZ_NAMES;
            int n = (dd_open == 912) ? 4 : 2;
            int oy = wy + 340 + (dd_open - 910) * 56 + 32;
            for (int i = 0; i < n; i++) {
                rr_fill(wx + 170, oy + i * 28, 300, 28, 2, FIELD_BG);
                rect_fill(wx + 170, oy + i * 28, 300, 1, 0xD0D0D0);
                text(&FONT_UI, wx + 180, oy + i * 28 + 7, opts[i], COL_DARKTXT);
                zone_add(wx + 170, oy + i * 28, 300, 28, 920 + i);
            }
        }
        fb_flush();
        for (;;) {
            int mev = mouse_event();
            if (mev & 2) {
                int id = zone_hit(MX, MY);
                if (id == 970) return 90;               /* croix -> éteindre */
                if (id >= 920 && id < 926) {
                    if (dd_open == 910) g_lang = id - 920;
                    else if (dd_open == 911) g_kbd = id - 920;
                    else g_tz = id - 920;
                    dd_open = -1;
                    break;
                }
                if (id >= 910 && id <= 912) {
                    dd_open = (dd_open == id) ? -1 : id;
                    break;
                }
                if (id == 900) { dd_open = -1; return 1; }
                if (dd_open >= 0) { dd_open = -1; break; }
            }
            if (mev & 1) break;
            int k = kbd_poll();
            if (k == '\n') { dd_open = -1; return 1; }
            if (k == 27) return 90;
            delay(12000);
        }
    }
}

/* ================================================================== */
/* ÉCRAN 1.2 : Édition (cases à cocher, 1 choix)                       */
/* ================================================================== */

static int screen_inst_edition(void) {
    edition = 0;
    for (;;) {
        zones_clear();
        win_frame();
        int wx = FB_W / 2 - 350, wy = 70;
        ui_button(wx + 490, wy + 44, 150, T(L_INSTALLB), COL_BLUE, 902);
        text(&FONT_UI, wx + 60, wy + 120, T(L_EDITION), COL_DARKTXT);
        for (int i = 0; i < 4; i++)
            ui_checkbox(wx + 60, wy + 170 + i * 46, ED_NAMES_L[i], edition == i, 905 + i);
        text(&FONT_SM, wx + 60, wy + 380, T(L_WARN), 0xC62828);
        text(&FONT_SM, wx + 60, wy + 406, T(L_WARN2), 0xC62828);
        ui_button(wx + 60, wy + 500, 140, T(L_BACK), 0x757575, 903);
        fb_flush();
        for (;;) {
            int mev = mouse_event();
            if (mev & 2) {
                int id = zone_hit(MX, MY);
                if (id == 970) return 90;
                if (id >= 905 && id <= 908) { edition = id - 905; break; }
                if (id == 902) return 1;
                if (id == 903) return 0;
            }
            if (mev & 1) break;
            int k = kbd_poll();
            if (k == '\n') return 1;
            if (k == 27) return 0;
            delay(12000);
        }
    }
}

/* ================================================================== */
/* ÉCRAN 1.3 : Plateforme (cases à cocher, 1 choix)                    */
/* ================================================================== */

static int screen_inst_platform(void) {
    g_platform = 0;
    for (;;) {
        zones_clear();
        win_frame();
        int wx = FB_W / 2 - 350, wy = 70;
        text(&FONT_UI, wx + 60, wy + 150, T(L_PLATQ), COL_DARKTXT);
        text(&FONT_UI, wx + 60, wy + 184, T(L_PLATQ2), COL_DARKTXT);
        ui_checkbox(wx + 60, wy + 250, T(L_PC), g_platform == 0, 930);
        ui_checkbox(wx + 60, wy + 300, T(L_MOBILE), g_platform == 1, 931);
        ui_button(wx + 250, wy + 500, 200, T(L_INSTALLB), COL_BLUE, 935);
        ui_button(wx + 60, wy + 500, 140, T(L_BACK), 0x757575, 903);
        fb_flush();
        for (;;) {
            int mev = mouse_event();
            if (mev & 2) {
                int id = zone_hit(MX, MY);
                if (id == 970) return 90;
                if (id == 930) { g_platform = 0; break; }
                if (id == 931) { g_platform = 1; break; }
                if (id == 935) return 1;
                if (id == 903) return 0;
            }
            if (mev & 1) break;
            int k = kbd_poll();
            if (k == '\n') return 1;
            if (k == 27) return 0;
            delay(12000);
        }
    }
}

/* ================================================================== */
/* ÉCRAN 1.4 : Choix du disque — « nom_du_disk, taille G »              */
/* ================================================================== */

static int screen_inst_disk(void) {
    ndisks = ata_enum_disks(disks, 8);
    if (ins_disk >= ndisks) ins_disk = 0;
    if (!ndisks) ins_disk = -1;
    for (;;) {
        zones_clear();
        win_frame();
        int wx = FB_W / 2 - 350, wy = 70;
        text(&FONT_UI, wx + 60, wy + 120, T(L_DISKQ), COL_DARKTXT);
        if (ndisks == 0) {
            text(&FONT_UI, wx + 60, wy + 190, T(L_NODISK), 0xC62828);
            text(&FONT_SM, wx + 60, wy + 228, T(L_NODISK2), GRAY_TXT);
        }
        for (int i = 0; i < ndisks; i++) {
            /* nom_du_disk, taille (en G) — format exact exigé */
            char line[72]; line[0] = 0;
            char *p = line;
            const char *m = disks[i].model;
            while (*m && p < line + 60) *p++ = *m++;
            *p++ = ','; *p++ = ' ';
            /* taille en Go, 2 décimales (GiB) */
            u64 g100 = (u64)disks[i].sectors * 512ull * 100ull / 1073741824ull;
            char num[16]; u32_str((u32)(g100 / 100), num);
            char *q = num; while (*q) *p++ = *q++;
            *p++ = '.';
            u32 frac = (u32)(g100 % 100);
            *p++ = (char)('0' + frac / 10);
            *p++ = (char)('0' + frac % 10);
            *p++ = ' '; *p++ = 'G'; *p = 0;
            ui_checkbox(wx + 60, wy + 170 + i * 46, line, i == ins_disk, 940 + i);
        }
        if (ndisks > 0)
            ui_button(wx + 120, wy + 380, 280, T(L_FMT), COL_BLUE, 945);
        ui_button(wx + 60, wy + 500, 140, T(L_BACK), 0x757575, 903);
        fb_flush();
        for (;;) {
            int mev = mouse_event();
            if (mev & 2) {
                int id = zone_hit(MX, MY);
                if (id == 970) return 90;
                if (id >= 940 && id < 940 + ndisks) { ins_disk = id - 940; break; }
                if (id == 945 && ndisks > 0) return 1;
                if (id == 903) return 0;
            }
            if (mev & 1) break;
            int k = kbd_poll();
            if (k == '\n' && ndisks > 0) return 1;
            if (k == 27) return 0;
            delay(12000);
        }
    }
}

/* ================================================================== */
/* ÉCRANS 1.5 / 1.6 : Copie + Installation (barres de %)               */
/* ================================================================== */

static void progress_draw(const L *title, int pct, int note) {
    zones_clear();
    win_frame();
    int wy = 70;
    text_center(&FONT_BTN, FB_W / 2, wy + 200, T(*title), COL_DARKTXT);
    char pc[12]; pc[0] = 0;
    char *p = pc;
    char num[8]; u32_str((u32)pct, num);
    char *q = num; while (*q) *p++ = *q++;
    *p++ = ' '; *p++ = '%'; *p = 0;
    text_center(&FONT_TTL, FB_W / 2, wy + 250, pc, COL_BLUE);
    text_center(&FONT_SM, FB_W / 2, wy + 420, T(L_LEFT), GRAY_TXT);
    if (note)
        text_center(&FONT_SM, FB_W / 2, wy + 452, T(L_REBT), GRAY_TXT);
    fb_flush();
}

/* ================================================================== */
/* ÉCRAN 1.7 : Terminé -> Redémarrer                                   */
/* ================================================================== */

static int screen_inst_done(void) {
    for (;;) {
        zones_clear();
        win_frame();
        int wx = FB_W / 2 - 350, wy = 70;
        text(&FONT_UI, wx + 60, wy + 160, T(L_DONE1), COL_DARKTXT);
        text(&FONT_SM, wx + 60, wy + 210, T(L_DONE2), GRAY_TXT);
        text(&FONT_SM, wx + 60, wy + 236, T(L_DONE3), GRAY_TXT);
        ui_button(wx + 220, wy + 340, 260, T(L_REBOOT), COL_BLUE, 950);
        fb_flush();
        for (;;) {
            int mev = mouse_event();
            if (mev & 2) {
                int id = zone_hit(MX, MY);
                if (id == 970) return 90;
                if (id == 950) return 1;
            }
            if (mev & 1) break;
            int k = kbd_poll();
            if (k == '\n') return 1;
            delay(12000);
        }
    }
}

/* ================================================================== */
/* Secteur de configuration persistée sur le disque (LBA 950)          */
/* magic "ARC1" + u8 edition, platform, kbd, lang, i8 tz               */
/* ================================================================== */

#define CFG_LBA 950

static void write_config(AtaDisk *d) {
    u8 buf[512];
    memset(buf, 0, 512);
    buf[0] = 'A'; buf[1] = 'R'; buf[2] = 'C'; buf[3] = '1';
    buf[4] = (u8)edition;
    buf[5] = (u8)g_platform;
    buf[6] = (u8)g_kbd;
    buf[7] = (u8)g_lang;
    buf[8] = (u8)(i32)(TZ_OFF[g_tz] & 0xFF);
    buf[9] = 0;                     /* 0 = premier boot à effectuer */
    ata_write_sector(d, CFG_LBA, buf);
    ata_flush(d);
}

/* ================================================================== */
/* Assistant principal                                                 */
/* ================================================================== */

void installer_wizard(void) {
    CURSOR_ON = 1;
    MX = FB_W / 2; MY = FB_H / 2;
    int step = 0;   /* 0 bienvenue, 1 édition, 2 plateforme, 3 disque, 4 fin */

    /* petit splash du chargeur d'installation */
    rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
    draw_logo(FB_W / 2 - 90, FB_H / 2 - 130, 180);
    text_center(&FONT_TTL, FB_W / 2, FB_H / 2 + 70, "arfuso", COL_BLUE);
    text_center(&FONT_SM, FB_W / 2, FB_H / 2 + 70 + FONT_TTL.height + 14,
                g_lang == 0 ? "Chargement de l'installateur…" : "Loading installer…", GRAY_TXT);
    fb_flush();
    delay(90000000);

    for (;;) {
        int r = 0;
        switch (step) {
            case 0: r = screen_inst_welcome();  break;
            case 1: r = screen_inst_edition();  break;
            case 2: r = screen_inst_platform(); break;
            case 3: r = screen_inst_disk();     break;
        }
        if (r == 90) {                          /* croix -> extinction */
            rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
            text_center(&FONT_BTN, FB_W / 2, FB_H / 2 - 20,
                        g_lang == 0 ? "Extinction d'Arfuso…" : "Shutting down…", COL_WHITE);
            fb_flush();
            delay(60000000);
            outw(0x604, 0x2000);
            outw(0xB004, 0x2000);
            cpu_off();
        }
        if (r == 0 && step > 0) { step--; continue; }
        if (r == 1) {
            step++;
            if (step == 4) {
                /* ---- ÉCRAN 1.5 : Copie des fichiers ---- */
                AtaDisk *tgt = &disks[ins_disk];
                int total = KSECTORS_VAL + 1;
                for (int s = 0; s < total; s++) {
                    const u8 *src = (s == 0) ? mbr_image
                                  : (const u8 *)0x100000 + (u64)(s - 1) * 512;
                    ata_write_sector(tgt, (u32)s, src);
                    if ((s & 1) == 0 || s == total - 1)
                        progress_draw(&L_COPY, s * 90 / total, 0);
                }
                ata_flush(tgt);
                /* ---- ÉCRAN 1.6 : Installation ---- */
                for (int p = 90; p <= 99; p += 3) {
                    progress_draw(&L_INSTALL, p, 1);
                    delay(12000000);
                }
                write_config(tgt);
                progress_draw(&L_INSTALL, 100, 1);
                /* ---- Système de fichiers AFS + fichiers de base ---- */
                afs_format();
                /* copie des fichiers de l'ISO (CD-ROM) vers le disque */
                iso_probe();
                if (iso_present()) {
                    static const struct { const char *iso; int fold; const char *afs; } CP[] = {
                        {"BOOT.BMP", -2, "BOOT.BMP"},      /* -2 : Système */
                        {"FOND.BMP", -3, "FOND.BMP"},      /* -3 : Images    */
                        {"LISEZMOI.TXT", -4, "LISEZMOI.txt"},
                    };
                    for (int f = 0; f < 3; f++) {
                        IsoItem it;
                        u32 rl, rs;
                        if (!iso_root(&rl, &rs)) break;
                        static IsoItem root_items[32];
                        int n = iso_list(rl, rs, root_items, 32);
                        int found = -1;
                        for (int i = 0; i < n; i++)
                            if (!root_items[i].is_dir) {
                                const char *a = root_items[i].name;
                                const char *b = CP[f].iso;
                                int eq = 1;
                                while (*a && *b && *a == *b) { a++; b++; }
                                if (*a == *b) { found = i; break; }
                            }
                        if (found < 0) continue;
                        u32 lba = iso_item_lba(&root_items[found]);
                        u32 size = root_items[found].size;
                        if (size > STAGING_MAX) size = STAGING_MAX;
                        iso_read_file(lba, 0, STAGING, size);
                        int fold = (CP[f].fold == -2) ? FOLD_System :
                                   (CP[f].fold == -3) ? FOLD_Images : FOLD_Docs;
                        afs_write_file((u32)fold, CP[f].afs, STAGING, size);
                    }
                }
                /* applications .arf préinstallées */
                installer_seed_apps();
                progress_draw(&L_INSTALL, 100, 1);
                delay(50000000);
                /* ---- ÉCRAN 1.7 : Terminé ---- */
                r = screen_inst_done();
                if (r == 90) {
                    outw(0x604, 0x2000);
                    outw(0xB004, 0x2000);
                    cpu_off();
                }
                /* Redémarrer : reset via clavier */
                while (inb(0x64) & 2) {}
                outb(0x64, 0xFE);
                delay(4000000);
                cpu_off();
            }
        }
    }
}

/* lecture de la config côté OS installé (exportée pour kernel.c) */
int installer_read_config(AtaDisk *d, u8 *ed, u8 *plat, u8 *kbd_, u8 *lang, i32 *tz, u8 *booted) {
    u8 buf[512];
    if (!ata_read_sector(d, CFG_LBA, buf)) return 0;
    if (buf[0] != 'A' || buf[1] != 'R' || buf[2] != 'C' || buf[3] != '1') return 0;
    *ed = buf[4]; *plat = buf[5]; *kbd_ = buf[6]; *lang = buf[7];
    *tz = (buf[8] >= 128) ? (i32)buf[8] - 256 : (i32)buf[8];
    if (booted) *booted = buf[9];
    return 1;
}

/* appelé par l'OS à la fin du tout premier démarrage (Image 2.4) */
void installer_set_booted(AtaDisk *d) {
    u8 buf[512];
    if (!ata_read_sector(d, CFG_LBA, buf)) return;
    if (buf[0] != 'A' || buf[1] != 'R' || buf[2] != 'C' || buf[3] != '1') return;
    buf[9] = 1;
    ata_write_sector(d, CFG_LBA, buf);
    ata_flush(d);
}
