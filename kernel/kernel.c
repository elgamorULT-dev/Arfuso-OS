/*
 * Arfuso OS v1.3 - Noyau principal
 * ================================
 * Noyau 64 bits écrit de zéro : aucune base Linux/Windows.
 *
 * Nouveautés v1.3 :
 *   - écran de démarrage = image du disque (Système/BOOT.BMP)
 *   - fond d'écran image (Images/FOND.BMP, modifiable)
 *   - VRAI internet : pile TCP/IP maison sur carte e1000 et navigateur
 *   - curseur sur TOUS les écrans (sauf démarrage)
 *   - barre des tâches à droite : applis OUVERTES ( - minimise, × ferme )
 *   - lanceur dynamique : applications .arf de l'ArfusOS (AFS)
 *   - comptes utilisateurs multiples
 */

#include "sys.h"
#include "fb.h"
#include "kbd.h"
#include "mouse.h"
#include "rtc.h"
#include "ata.h"
#include "apps.h"
#include "installer.h"
#include "net.h"
#include "fs.h"
#include "bmp.h"
#include "generated/colors.h"


#define GRAY_TXT   0x757575

#define Z_LOGIN_GO   901
#define Z_LOGIN_OFF  902
#define Z_LOGIN_USER 950     /* +i = choix utilisateur */

#define Z_TOP_NET    971
#define Z_TOP_BT     972
#define Z_TOP_BAT    973
#define Z_TASK       1200    /* +i = tâche dockée */

typedef enum {
    ST_BOOT, ST_FBOOT, ST_LOCK, ST_LOGIN, ST_WELCOME, ST_DESKTOP, ST_OFF
} State;

int edition = 0;   /* 0 Professionnelle, 1 Familiale, 2 Éducation, 3 Gaming */
const char *ED_NAMES[4] = {"Professionnelle", "Familiale", "Éducation", "Gaming"};
const char *ED_SHORT[4]  = {"Professionnel", "Familial", "Éducation", "Gaming"};
AtaDisk the_disk;
static const int VEILLE_MIN_L[4] = {5, 1, 15, 0};   /* veille écran (min) */
int disk_ok = 0;

static char pw[32];
static int pw_len = 0;
static int login_error = 0;
static int login_sel = 0;         /* utilisateur sélectionné à la connexion */

/* ============ Icônes dessinées par code ============ */

static void icon_battery(int x, int y, int w, int h, u32 c, u32 bg) {
    rr_fill(x, y, w, h, h / 4, c);
    rr_fill(x + 3, y + 3, w - 6, h - 6, h / 5, bg);
    rect_fill(x + 5, y + 5, (w - 10) * 3 / 4, h - 10, c);
    rr_fill(x + w, y + h / 3, h / 4, h / 3, 2, c);
}

static const int BT_DIR[8][2] = {
    {10, 0}, {7, 7}, {0, 10}, {-7, 7}, {-10, 0}, {-7, -7}, {0, -10}, {7, -7}
};
static void icon_gear(int cx, int cy, int r, u32 c) {
    ring(cx, cy, r, r * 3 / 5, c);
    for (int k = 0; k < 8; k++) {
        thick_line(cx + BT_DIR[k][0] * r / 11, cy + BT_DIR[k][1] * r / 11,
                   cx + BT_DIR[k][0] * (r + 5) / 10, cy + BT_DIR[k][1] * (r + 5) / 10,
                   r / 3 + 2, c);
    }
}

static void icon_bt(int cx, int cy, int r, u32 c) {
    int t = r / 4 + 1;
    thick_line(cx, cy - r, cx, cy + r, t, c);
    thick_line(cx, cy - r, cx + r * 2 / 3, cy - r / 2, t, c);
    thick_line(cx + r * 2 / 3, cy - r / 2, cx, cy, t, c);
    thick_line(cx, cy + r, cx + r * 2 / 3, cy + r / 2, t, c);
    thick_line(cx + r * 2 / 3, cy + r / 2, cx, cy, t, c);
}

static void icon_power(int cx, int cy, int r, u32 c) {
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++) {
            int d = i * i + j * j;
            if (d <= r * r && d >= (r - 4) * (r - 4)) {
                if (!(j < 0 && (i > -r / 5 && i < r / 5) && j * j > r * r / 9))
                    px(cx + i, cy + j, c);
            }
        }
    thick_line(cx, cy - r, cx, cy - r / 8, 6, c);
}

static void icon_chrome(int cx, int cy, int r) {
    circle_fill(cx, cy, r, 0xF2F2F2);
    circle_fill(cx, cy, r - 2, 0xFFFFFF);
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++) {
            int d = i * i + j * j;
            if (d > (r - 2) * (r - 2) || d < (r * 3 / 5) * (r * 3 / 5)) continue;
            u32 c;
            if (j < 0 && (-j) * 2 >= (i > 0 ? i : -i)) c = 0xEA4335;
            else if (i < 0) c = 0x34A853;
            else c = 0xFBBC05;
            px(cx + i, cy + j, c);
        }
    circle_fill(cx, cy, r * 2 / 5, 0xFFFFFF);
    circle_fill(cx, cy, r * 2 / 5 - 2, 0x4285F4);
}

static void draw_avatar(int cx, int cy, int r) {
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++) {
            if (i * i + j * j > r * r) continue;
            u32 c = 0xF5F5F5;
            int hx = i, hy = j + r / 3;
            if (hx * hx + hy * hy <= (r / 3 + 2) * (r / 3 + 2)) c = COL_BLUE;
            int sx = i, sy = j - r + r / 4;
            if (sx * sx + sy * sy <= (r * 3 / 4) * (r * 3 / 4)) c = COL_BLUE;
            px(cx + i, cy + j, c);
        }
}

/* ============ BUREAU (fond, barre, dock tâches) ============ */

void draw_desktop_base(const ArfDate *d) {
    zones_clear();
    /* v1.5 - thème : 0 = Sombre (défaut), 1 = Clair */
    u32 BAR_C  = g_theme ? 0xECECEC : 0x1B1B22;
    u32 BTXT_C = g_theme ? COL_BLACK : 0xF2F2F2;
    if (!g_theme) { bg_colors[0] = 0x181820; bg_colors[1] = 0x1A2430; bg_colors[2] = 0x202028; bg_colors[3] = 0x262018; }
    else          { bg_colors[0] = 0xFFFFFF; bg_colors[1] = 0xE8F4FD; bg_colors[2] = 0xF2F2F2; bg_colors[3] = 0xFAF3E3; }
    /* fond : image ou couleur */
    if (bg_mode == 4 && wallpaper_active)
        wallpaper_blit();
    else
        rect_fill(0, 0, FB_W, FB_H, bg_mode == 4 ? bg_colors[0] : bg_colors[bg_mode]);
    DESK_BG = 0;
    /* barre du haut */
    rect_fill(0, 0, FB_W, 52, BAR_C);
    rect_fill(0, 52, FB_W, 2, BTXT_C);
    /* icônes cliquables : réseau, bluetooth, batterie (mobile), engrenage */
    wifi_arcs(34, 36, 18, BTXT_C);
    zone_add(10, 6, 52, 40, Z_TOP_NET);
    icon_bt(84, 26, 11, BTXT_C);
    zone_add(66, 6, 40, 40, Z_TOP_BT);
    int nextx = 110;
    if (g_platform == 1) {                    /* batterie : seulement sur portables */
        icon_battery(120, 14, 38, 24, BTXT_C, BAR_C);
        zone_add(110, 6, 58, 40, Z_TOP_BAT);
        nextx = 172;
    }
    icon_gear(nextx + 14, 26, 12, BTXT_C);
    zone_add(nextx - 2, 6, 40, 40, APP_PARAMS);

    char dn[16], ts[8];
    rtc_date_num(d, dn);
    text_center(&FONT_UI, FB_W / 2 - 110, 14, dn, BTXT_C);
    rtc_time_str(d, ts);
    text_center(&FONT_UI, FB_W / 2 + 110, 14, ts, BTXT_C);
    draw_logo(FB_W - 46, 6, 40);
    zone_add(FB_W - 52, 0, 52, 52, APP_LAUNCHER);      /* logo -> lanceur */

    /* barre des tâches à droite : applis ouvertes */
    int x0 = FB_W - 96;
    int vx[4] = {x0 + 22, FB_W, FB_W, x0 + 22};
    int vy[4] = {92, 72, FB_H - 72, FB_H - 92};
    poly_fill(vx, vy, 4, COL_DOCK);
    int yy = 96;
    for (int i = 0; i < NTASKS && yy < FB_H - 180; i++) {
        u32 cc = TASKS[i].minimized ? 0x4A4A4A : 0x606060;
        rr_fill(FB_W - 90, yy, 84, 88, 8, cc);
        if (!TASKS[i].minimized) {
            rect_fill(FB_W - 90, yy, 4, 88, 0x7AB8FF);
        }
        rr_fill(FB_W - 71, yy + 10, 46, 46, 9, TASKS[i].color);
        text_center(&FONT_SM, FB_W - 48, yy + 25, TASKS[i].glyph, COL_WHITE);
        text_center(&FONT_SM, FB_W - 48, yy + 62, TASKS[i].name, COL_WHITE);
        if (TASKS[i].minimized)
            text_center(&FONT_SM, FB_W - 48, yy + 74, "réduite", 0xC0C0C0);
        zone_add(FB_W - 90, yy, 84, 88, Z_TASK + i);
        yy += 96;
    }
    notif_draw();   /* v1.5 : bandeaux de notification */
}

/* charge Système/BOOT.BMP (PJ11) dans le cache image ; 1 si ok */
static int load_boot_img(void) {
    if (!disk_ok || !afs_present()) return 0;
    AfsItem f;
    if (!afs_find((u32)FOLD_System, "BOOT.BMP", &f)) return 0;
    if (!f.size || f.size > 4u * 1024u * 1024u) return 0;
    if (afs_read_file(&f, (u8 *)0x1000000, f.size) <= 0) return 0;
    return bmp_fit_to_cache((const u8 *)0x1000000, (int)f.size);
}

/* cadre de démarrage : image PJ11 (ou logo) + barre, SANS aucun texte */
static void boot_frame(int have_img, int step, int steps) {
    if (have_img) wallpaper_blit();
    else {
        rect_fill(0, 0, FB_W, FB_H, 0x06090C);
        draw_logo(FB_W / 2 - FB_W / 10, FB_H / 2 - FB_W / 5 - 40, FB_W / 5);
    }
    if (step >= 0) {
        int bw = 360, bx = FB_W / 2 - bw / 2, by = FB_H - 96;
        rr_fill(bx - 2, by - 2, bw + 4, 16, 7, 0x000000);
        rr_fill(bx, by, bw * step / steps, 12, 6, 0x5AC8FA);
    }
    fb_flush();
}

/* ============ Écran de démarrage : image PJ11, sans texte ============ */

static State screen_boot(void) {
    int have_img = load_boot_img();
    CURSOR_ON = 0;                           /* pas de curseur au démarrage */
    for (int step = 0; step <= 24; step++) {
        boot_frame(have_img, step, 24);
        if (step == 4)  { afs_probe();  serial_str("s4\n"); }
        if (step == 8)  { users_load(); serial_str("s8\n"); }
        if (step == 11) { net_init(); }
        if (step == 17) { iso_probe();  serial_str("s17\n"); }
        if (step == 21) { wallpaper_load_file(); serial_str("s21\n"); }
        delay(22000000);
    }
    return ST_LOCK;
}

#if 0
#endif

/* ============ VERROUILLAGE v1.4 : fond personnalisable, heure gigantesque ============ */

/* palettes (couleur unie / texte / icônes) — la 1re correspond à la maquette */
static const u32 LOCK_BG[6]  = { 0xF00000, 0x101015, 0x0A1428, 0x4A148C, 0x37474F, 0xF5F5F5 };
static const u32 LOCK_TX[6]  = { 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0x141414 };
static const u32 LOCK_IC[6]  = { 0x1A1A1A, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0x1A1A1A };

static State screen_lock(void) {
    CURSOR_ON = 1;
    int last_sec = -1;
    for (;;) {
        ArfDate d;
        rtc_read(&d);
        if (d.sec != last_sec) {
            last_sec = d.sec;
            zones_clear();
            u32 txt, icn, bgc = 0x202020;
            if (lock_bg_mode == 1 && load_boot_img()) {
                wallpaper_blit();           /* image du disque choisie */
                txt = 0xFFFFFF; icn = 0xFFFFFF;
            } else {
                int i = lock_bg_col % 6;
                bgc = LOCK_BG[i]; txt = LOCK_TX[i]; icn = LOCK_IC[i];
                rect_fill(0, 0, FB_W, FB_H, bgc);
            }
            /* icônes en haut à droite : batterie puis wifi */
            icon_battery(FB_W - 118, 14, 42, 26, icn, bgc);
            wifi_arcs(FB_W - 32, 30, 16, icn);
            /* heure gigantesque en bas à droite : "12h32" */
            char hh[8];
            hh[0] = (char)('0' + d.hour / 10); hh[1] = (char)('0' + d.hour % 10);
            hh[2] = 'h';
            hh[3] = (char)('0' + d.min  / 10); hh[4] = (char)('0' + d.min  % 10);
            hh[5] = 0;
            int hy = FB_H - (FONT_CLK.height + 90);
            text_right(&FONT_CLK, FB_W - 60, hy, hh, txt);
            /* date complète juste en dessous : lundi 1er janvier 2027 */
            char dl[48];
            rtc_date_long(&d, dl);
            text_right(&FONT_UI, FB_W - 60, FB_H - 60, dl, txt);
            fb_flush();
        }
        if (mouse_event() & 2) {
            pw_len = 0; pw[0] = 0; login_error = 0;
            return ST_LOGIN;
        }
        int k = kbd_poll();
        if (k == '\n' || k == KEY_RIGHT) {
            pw_len = 0; pw[0] = 0; login_error = 0;
            return ST_LOGIN;
        }
        delay(12000);
    }
}

/* ============ CONNEXION (multi-utilisateurs) ============ */

/* identifiants saisis à l'écran de connexion */
static char login_id[24];
static int  login_id_len = 0;

/* dessine un champ de saisie arrondi ; focus = 1 si actif */
static void login_field(int x, int y, int w, int h, const char *val, int len,
                        const char *ph, int focus, int secret) {
    rr_fill(x - 4, y - 4, w + 8, h + 8, 14, focus ? 0xFFFFFF : 0xC8C8C8);
    rr_fill(x, y, w, h, 12, COL_WHITE);
    int cy = y + h / 2 - FONT_UI.height / 2;
    if (len == 0) { text(&FONT_UI, x + 20, cy, (char *)ph, COL_PLACEH); return; }
    if (!secret) text(&FONT_UI, x + 20, cy, (char *)val, COL_BLACK);
    else for (int i = 0; i < len; i++) circle_fill(x + 26 + i * 20, y + h / 2, 6, COL_BLACK);
}

/* ============ CONNEXION : identifiant à gauche, mot de passe à droite ============ */
static State screen_login(void) {
    CURSOR_ON = 1;
    if (login_id_len >= 24) login_id_len = 23;
    if (login_id_len == 0 && NUSERS >= 1) {   /* préremplit avec le 1er compte */
        int i = 0;
        for (i = 0; i < 23 && USERS[0].name[i]; i++) login_id[i] = USERS[0].name[i];
        login_id[i] = 0; login_id_len = 0;
        for (i = 0; login_id[i]; i++) login_id_len++;
    }
    int focus = 0;          /* 0 = identifiant, 1 = mot de passe */
    int last_sec = -1, dirty = 1;
    for (;;) {
        ArfDate d;
        rtc_read(&d);
        if (d.sec != last_sec) { last_sec = d.sec; dirty = 1; }
        if (dirty) {
            dirty = 0;
            zones_clear();
            rect_fill(0, 0, FB_W, FB_H, COL_RED);
            wifi_arcs(44, FB_H - 34, 18, COL_BLACK);
            icon_power(112, FB_H - 40, 22, COL_BLACK);
            zone_add(80, FB_H - 70, 64, 64, Z_LOGIN_OFF);

            int ar = FB_W / 12; if (ar > 70) ar = 70;
            int acy = FB_H * 20 / 100;
            draw_avatar(FB_W / 2, acy, ar);
            text_center(&FONT_TTL, FB_W / 2, acy + ar + 12, "Connexion", COL_WHITE);

            int iw = FB_W < 700 ? (FB_W - 130) / 2 : 250, ih = 58, gap = 20, aw = 58;
            int tot = iw * 2 + gap + aw;
            int ix = FB_W / 2 - tot / 2, iy = acy + ar + 12 + FONT_TTL.height + 24;
            /* champ identifiant (gauche) */
            login_field(ix, iy, iw, ih, login_id, login_id_len, "Identifiant", focus == 0, 0);
            zone_add(ix, iy, iw, ih, Z_LOGIN_USER);
            /* champ mot de passe (droite) + flèche */
            int px = ix + iw + gap;
            login_field(px, iy, iw, ih, pw, pw_len, "Mot de passe", focus == 1, 1);
            zone_add(px, iy, iw, ih, Z_LOGIN_USER + 1);
            rr_fill(px + iw + 8, iy - 4, aw, ih + 8, 14, g_accent);
            text_center(&FONT_BTN, px + iw + 8 + aw / 2, iy + ih / 2 - FONT_BTN.height / 2, "→", COL_WHITE);
            zone_add(px + iw + 8, iy - 4, aw, ih + 8, Z_LOGIN_GO);

            if (login_error)
                text_center(&FONT_SM, FB_W / 2, iy + ih + 16,
                            "Identifiant ou mot de passe incorrect.", 0xFFE0E0);
            /* caret clignotant */
            int cx = focus == 0 ? ix + 26 + (int)text_w(&FONT_UI, login_id) :
                                  px + 32 + pw_len * 20;
            if (d.sec % 2 == 0 && (focus == 0 ? login_id_len : pw_len) > 0)
                rect_fill(cx, iy + 12, 3, ih - 24, COL_BLACK);
            fb_flush();
        }
        int mev = mouse_event();
        if (mev & 1) dirty = 1;
        if (mev & 2) {
            int id = zone_hit(MX, MY);
            if (id == Z_LOGIN_OFF) return ST_OFF;
            if (id == Z_LOGIN_USER)     { focus = 0; dirty = 1; }
            if (id == Z_LOGIN_USER + 1) { focus = 1; dirty = 1; }
            if (id == Z_LOGIN_GO) goto validate;
        }
        {
            int k = kbd_poll();
            if (!k) { delay(12000); continue; }
            if (k == '\n') goto validate;
            if (k == 27) return ST_LOCK;
            if (k == '\t') { focus ^= 1; dirty = 1; continue; }
            if (k == '\b' || k == 127) {
                if (focus == 0) { if (login_id_len > 0) login_id[--login_id_len] = 0; }
                else            { if (pw_len > 0) pw[--pw_len] = 0; }
                login_error = 0; dirty = 1;
            } else if (k >= 32 && k < 127) {
                if (focus == 0) { if (login_id_len < 23) login_id[login_id_len++] = (char)k; }
                else            { if (pw_len < 22) pw[pw_len++] = (char)k; }
                login_error = 0; dirty = 1;
            }
        }
        continue;
validate:
        login_id[login_id_len] = 0;
        pw[pw_len] = 0;
        int ok = 0, uidx = -1;
        for (int u = 0; u < NUSERS; u++) {
            int same = 1;
            for (int i = 0; USERS[u].name[i] || login_id[i]; i++)
                if (USERS[u].name[i] != login_id[i]) { same = 0; break; }
            if (!same) continue;
            int pwok = 1;
            char hh[17]; pwd_hash(pw, hh);           /* v1.5 : mdp haché */
            if (!g_login_pwd)          /* connexion sans mot de passe (réglage) */
                for (int i = 0; USERS[u].pwd[i] || hh[i]; i++)
                    if (USERS[u].pwd[i] != hh[i]) { pwok = 0; break; }
            /* v1.5 - PIN rapide : 4 chiffres ouvrent aussi la session */
            int pinok = 0;
            if (g_pin_en && pw_len == 4) {
                pinok = 1;
                for (int i = 0; i < 4; i++)
                    if (pw[i] != (char)('0' + g_pin[i])) { pinok = 0; break; }
            }
            if (pwok || pinok) { ok = 1; uidx = u; break; }
        }
        if (ok && uidx >= 0) {
            CUR_USER_IDX = uidx;
            return ST_WELCOME;
        }
        login_error = 1; pw_len = 0; pw[0] = 0; dirty = 1;
    }
}

/* ============ PREMIER BOOT (Images 2.1 -> 2.4 du guide) ============ */
static State screen_fboot(void) {
    /* Image 2.1 : fond noir, logo, "Configuration..." (pilotes & drivers) */
    CURSOR_ON = 0;
    for (int t = 0; t < 26; t++) {
        rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
        int dsz = FB_W / 6; if (dsz > 150) dsz = 150;
        draw_logo(FB_W / 2 - dsz / 2, FB_H / 2 - dsz / 2 - 34, dsz);
        text_center(&FONT_BTN, FB_W / 2, FB_H / 2 + dsz / 2 - 6, "Configuration...", COL_WHITE);
        fb_flush();
        if (t == 4)  { afs_probe(); }
        if (t == 6)  { users_load(); }
        if (t == 8)  { net_init(); }
        if (t == 20) { iso_probe(); }
        if (t == 24) { wallpaper_load_file(); }
        delay(22000000);
    }
    /* Image 2.2 : création du profil (identifiant + mdp avec flèche ->) */
    static char nid[24]; int nid_len = 0; nid[0] = 0;
    pw_len = 0; pw[0] = 0;
    int focus = 0, dirty = 1;
    for (;;) {
        if (dirty) {
            dirty = 0;
            zones_clear();
            rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
            int dsz = FB_W / 8; if (dsz > 90) dsz = 90;
            draw_logo(FB_W / 2 - dsz / 2, FB_H * 10 / 100, dsz);
            text_center(&FONT_TTL, FB_W / 2, FB_H * 10 / 100 + dsz + 18,
                        "Configurez Votre Profil Utilisateur", COL_WHITE);
            int iw = FB_W < 620 ? FB_W - 140 : 420, ih = 58;
            int ix = FB_W / 2 - iw / 2;
            int iy = FB_H * 10 / 100 + dsz + 18 + FONT_TTL.height + 30;
            login_field(ix, iy, iw, ih, nid, nid_len, "Identifiant", focus == 0, 0);
            zone_add(ix, iy, iw, ih, Z_LOGIN_USER);
            int iy2 = iy + ih + 22;
            login_field(ix, iy2, iw - 66, ih, pw, pw_len, "Mot de Passe", focus == 1, 1);
            zone_add(ix, iy2, iw - 66, ih, Z_LOGIN_USER + 1);
            rr_fill(ix + iw - 58, iy2 - 4 + 4, 66, ih, 12, g_accent);
            text_center(&FONT_BTN, ix + iw - 25, iy2 + ih / 2 - FONT_BTN.height / 2, "→", COL_WHITE);
            zone_add(ix + iw - 58, iy2, 66, ih, Z_LOGIN_GO);
            text_center(&FONT_SM, FB_W / 2, FB_H - 56,
                        "Si vous ne souhaitez pas mettre de mot de passe, appuyez", 0xB0B0B0);
            text_center(&FONT_SM, FB_W / 2, FB_H - 56 + FONT_SM.height + 4,
                        "directement sur la flèche après avoir mis votre identifiant.", 0xB0B0B0);
            fb_flush();
        }
        int mev = mouse_event();
        if (mev & 1) dirty = 1;
        int go = 0;
        if (mev & 2) {
            int id = zone_hit(MX, MY);
            if (id == Z_LOGIN_USER)     { focus = 0; dirty = 1; }
            if (id == Z_LOGIN_USER + 1) { focus = 1; dirty = 1; }
            if (id == Z_LOGIN_GO) go = 1;
        }
        int k = kbd_poll();
        if (k) {
            if (k == '\n') go = 1;
            else if (k == '\t') { focus ^= 1; dirty = 1; }
            else if (k == '\b' || k == 127) {
                if (focus == 0) { if (nid_len > 0) nid[--nid_len] = 0; }
                else            { if (pw_len > 0) pw[--pw_len] = 0; }
                dirty = 1;
            } else if (k >= 32 && k < 127) {
                if (focus == 0) { if (nid_len < 23) nid[nid_len++] = (char)k; }
                else            { if (pw_len < 22) pw[pw_len++] = (char)k; }
                dirty = 1;
            }
        }
        if (!go) { delay(12000); continue; }
        if (nid_len == 0) { dirty = 1; continue; }   /* identifiant obligatoire */
        nid[nid_len] = 0; pw[pw_len] = 0;
        break;
    }
    /* création du compte (écrase le compte par défaut) */
    {
        int i = 0;
        for (i = 0; i < 23 && nid[i]; i++) USERS[0].name[i] = nid[i];
        USERS[0].name[i] = 0;
        for (i = 0; i < 23 && pw[i]; i++) USERS[0].pwd[i] = pw[i];
        USERS[0].pwd[i] = 0;
        NUSERS = 1; CUR_USER_IDX = 0;
        users_save();
        /* identifiant prérempli sur l'écran de connexion futur */
        for (i = 0; i < 23 && nid[i]; i++) login_id[i] = nid[i];
        login_id[i] = 0; login_id_len = 0;
        for (i = 0; login_id[i]; i++) login_id_len++;
    }
    /* Image 2.3 : configuration du profil (points animés) */
    for (int t = 0; t < 6; t++) {
        rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
        text_center(&FONT_BTN, FB_W / 2, FB_H / 2 - 20,
                    t % 2 ? "Configuration de votre Profil . ." : "Configuration de votre Profil .",
                    COL_WHITE);
        fb_flush();
        delay(30000000);
    }
    /* Image 2.4 : redémarrage obligatoire avec barre % puis reset réel */
    installer_set_booted(&the_disk);
    for (int t = 0; t <= 20; t++) {
        rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
        text_center(&FONT_BTN, FB_W / 2, FB_H / 2 - 46, "Redémarrage en Cours ...", COL_WHITE);
        int bw = 320, bx = FB_W / 2 - bw / 2, by = FB_H / 2 + 6;
        rr_fill(bx - 3, by - 3, bw + 6, 22, 10, 0x303030);
        rr_fill(bx, by, bw * t / 20, 16, 8, 0xFFFFFF);
        fb_flush();
        delay(26000000 * (t ? 1 : 2));
    }
    while (inb(0x64) & 2) {}
    outb(0x64, 0xFE);          /* reset clavier : redémarrage réel */
    delay(4000000);
    cpu_off();
    return ST_BOOT;
}

static State screen_welcome(void) {
    CURSOR_ON = 1;
    rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
    draw_logo(FB_W / 2 - 60, FB_H / 2 - 130, 120);
    text_center(&FONT_BTN, FB_W / 2, FB_H / 2 + 10, "Bienvenue sur Arfuso", COL_WHITE);
    char wb[64];
    wb[0] = 0;
    char *p = wb;
    const char *a = "Ouverture de la session de ";
    while (*a) *p++ = *a++;
    a = USERS[CUR_USER_IDX].name;
    while (*a && p < wb + 60) *p++ = *a++;
    *p = 0;
    text_center(&FONT_SM, FB_W / 2, FB_H / 2 + 10 + FONT_BTN.height + 10, wb, GRAY_TXT);
    fb_flush();
    delay(120000000);
    return ST_DESKTOP;
}

/* ============ lancement d'applications + barre des tâches ============ */

/* métadonnées affichées pour les applis natives */
typedef struct { const char *name; const char *glyph; u32 color; } BApp;
static const BApp BUILTIN[10] = {
    {"?", "?", 0x555555},
    {"ArfuNet", "An", 0x0B57D0},
    {"Terminal", "Te", 0x212121},
    {"Notes", "No", 0xF9A825},
    {"Réglages", "Pa", 0x607D8B},
    {"Snake", "Sn", 0x1B8A3B},
    {"Explorer", "Ex", 0x00897B},
    {"Store", "St", 0x7B1FA2},
    {"ArfuDocs", "Ad", 0x1565C0},
    {"ArfuCalc", "Ca", 0xD81B60},
};

/* résout id/node -> métadonnées tâche */
static void task_meta(int id, int node, char *name, char *glyph, u32 *color) {
    if (id == APP_SCRIPT) {
        AfsItem f; f.node = (u32)node; f.type = 1;
        ArfHead h;
        if (afs_present() && afs_read_file(&f, (u8 *)&h, 64) >= 64 && h.magic[0] == 'A') {
            s_copy(name, h.name, 24);
            s_copy(glyph, h.glyph, 4);
            *color = h.color;
            return;
        }
        s_copy(name, "Appli", 24); s_copy(glyph, "Ap", 4); *color = 0x555555;
        return;
    }
    if (id >= 1 && id <= 7) {
        s_copy(name, BUILTIN[id].name, 24);
        s_copy(glyph, BUILTIN[id].glyph, 4);
        *color = BUILTIN[id].color;
        return;
    }
    s_copy(name, "Appli", 24); s_copy(glyph, "Ap", 4); *color = 0x555555;
}

/* Boucle d'exécution d'une app ; gère chaînage, minimisation, tâches */
static int run_app2(int id, int node) {
    int cur_id = id, cur_node = node;
    int minimized = 0;
    for (;;) {
        char nm[24], gl[4]; u32 co;
        task_meta(cur_id, cur_node, nm, gl, &co);
        task_add(cur_id, cur_node, nm, gl, co);
        if (minimized) {   /* retour d'une restauration */
            int ti = task_find(cur_id, cur_node);
            if (ti >= 0) TASKS[ti].minimized = 0;
            minimized = 0;
        }
        int r = 0;
        switch (cur_id) {
            case APP_NAV:      r = app_browser();  break;
            case APP_TERMINAL: r = app_terminal(); break;
            case APP_NOTES:    r = app_notes();    break;
            case APP_PARAMS:   r = app_params();   break;
            case APP_SNAKE:
                if (edition == 0) {
                    app_blocked("l'édition Professionnelle bloque les jeux.");
                    r = 0;
                } else r = app_snake();
                break;
            case APP_EXPLORER: r = app_explorer(); break;
            case APP_STORE:    r = app_store();    break;
            case APP_DOCS:     r = app_docs();     break;
            case APP_CALC:     r = app_calc();     break;
            case APP_SCRIPT:   r = app_script_viewer((u32)cur_node); break;
            case APP_OFF:      task_remove(cur_id, cur_node); return APP_OFF;
            case APP_LOCK:     task_remove(cur_id, cur_node); return APP_LOCK;
            default:           task_remove(cur_id, cur_node); return 0;
        }
        if (r >= APP_MIN_RET) {        /* minimisation */
            int ti = task_find(cur_id, cur_node);
            if (ti >= 0) TASKS[ti].minimized = 1;
            return 0;                  /* retour bureau ; la tâche reste dans le dock */
        }
        if (r >= 1 && r <= 40) {       /* chaînage vers une autre application */
            task_remove(cur_id, cur_node);
            cur_id = r; cur_node = -1;
            minimized = 0;
            continue;
        }
        task_remove(cur_id, cur_node);
        if (r == APP_LOCK || r == APP_OFF) return r;
        return 0;
    }
}

static int run_app(int id) { return run_app2(id, -1); }

/* ============ BUREAU : boucle ============ */

static State screen_desktop(void) {
    CURSOR_ON = 1;
    int dirty = 1, last_sec = -1, idle_sec = 0;
    /* v1.5 : applis choisies lancées automatiquement à la connexion */
    static int startup_done = 0;
    if (!startup_done) { notif_push("Bienvenue sur Arfuso 1.5 !"); }
    if (!startup_done && g_startmask) {
        startup_done = 1;
        for (int i = 1; i <= 9; i++)
            if (g_startmask & (1 << i)) run_app2(i, -1);
        dirty = 1;
    }
    startup_done = 1;
    for (;;) {
        ArfDate d;
        rtc_read(&d);
        if (d.sec != last_sec) { last_sec = d.sec; dirty = 1; idle_sec++; }
        /* veille écran (réglage Système > Alimentation) */
        if (VEILLE_MIN_L[g_veille & 3] && idle_sec >= VEILLE_MIN_L[g_veille & 3] * 60) {
            CURSOR_ON = 0;
            rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
            fb_flush();
            for (;;) {
                if (mouse_event() & 3) break;
                if (kbd_poll()) break;
                delay(6000);
            }
            idle_sec = 0; dirty = 1; CURSOR_ON = 1;
        }
        if (dirty) {
            dirty = 0;
            draw_desktop_base(&d);
            fb_flush();
        }
        int mev = mouse_event();
        int app = 0;
        if (mev & 3) idle_sec = 0;
        if (mev & 1) dirty = 1;
        if (mev & 2) {
            app = zone_hit(MX, MY);
            dirty = 1;
        }
        if (!app) {
            int k = kbd_poll();
            if (k) idle_sec = 0;
            switch (k) {
                case 0: delay(10000); continue;
                case 'v': case 'V': app = APP_LOCK;    break;
                case 'p': case 'P': app = APP_OFF;     break;
            }
            dirty = 1;
        }
        if (!app) continue;

        /* panneaux de la barre du haut */
        if (app == Z_TOP_NET) {
            int nx = topbar_open(0);
            if (nx >= 1 && nx <= 7) {
                int r = run_app2(nx, -1);
                if (r == APP_LOCK) return ST_LOCK;
                if (r == APP_OFF)  return ST_OFF;
            }
            dirty = 1; continue;
        }
        if (app == Z_TOP_BT)  { (void)topbar_open(1); dirty = 1; continue; }
        if (app == Z_TOP_BAT) { (void)topbar_open(2); dirty = 1; continue; }

        /* tâche dans le dock : restaurer / amener au premier plan */
        if (app >= Z_TASK && app < Z_TASK + NTASKS) {
            int ti = app - Z_TASK;
            int tid = TASKS[ti].id, tnode = TASKS[ti].node;
            if (TASKS[ti].minimized) {
                int r = run_app2(tid, tnode);          /* relance (état conservé) */
                dirty = 1;
                if (r == APP_LOCK) return ST_LOCK;
                if (r == APP_OFF)  return ST_OFF;
            } else {
                /* déjà ouverte : revenir dessus = rien, un clic ferme ? non.
                   On n'agit pas (elle est au premier plan par définition). */
            }
            continue;
        }

        if (app == APP_LAUNCHER) {
            app = launcher_full();
            dirty = 1;
            if (!app) continue;
        }
        if (app >= 10000) {              /* .arf lancé depuis le lanceur */
            int node = app - 10000;
            int eng = arf_engine_of((u32)node);
            if (eng >= 1 && eng <= 9) {
                int r = run_app2(eng, -1);
                dirty = 1;
                if (r == APP_LOCK) return ST_LOCK;
                if (r == APP_OFF)  return ST_OFF;
            } else if (eng == 200) {
                int r = run_app2(APP_SCRIPT, node);
                dirty = 1;
                if (r == APP_LOCK) return ST_LOCK;
                if (r == APP_OFF)  return ST_OFF;
            } else {
                app_blocked("ce fichier .arf est corrompu ou illisible.");
                dirty = 1;
            }
            continue;
        }
        int r = run_app(app);
        dirty = 1;
        if (r == APP_LOCK) return ST_LOCK;
        if (r == APP_OFF)  return ST_OFF;
    }
}

/* ============ Extinction ============ */

static void screen_off(void) {
    CURSOR_ON = 0;
    rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
    draw_logo(FB_W / 2 - 50, FB_H / 2 - 140, 100);
    text_center(&FONT_BTN, FB_W / 2, FB_H / 2 - 10, "Extinction d'Arfuso…", COL_WHITE);
    fb_flush();
    delay(110000000);
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    text_center(&FONT_UI, FB_W / 2, FB_H / 2 + 50,
                "Vous pouvez fermer la machine virtuelle.", GRAY_TXT);
    fb_flush();
    cpu_off();
}

/* ============ Disque non installé ============ */

static void screen_not_installed(void) {
    CURSOR_ON = 0;
    rect_fill(0, 0, FB_W, FB_H, COL_BLACK);
    draw_logo(FB_W / 2 - 50, FB_H / 2 - 190, 100);
    text_center(&FONT_BTN, FB_W / 2, FB_H / 2 - 60,
                "Arfuso n'est pas installé sur ce disque.", COL_WHITE);
    text_center(&FONT_UI, FB_W / 2, FB_H / 2 - 6,
                "Le système ne peut pas fonctionner sans installation.", GRAY_TXT);
    text_center(&FONT_UI, FB_W / 2, FB_H / 2 + 34,
                "Démarrez la machine sur l'ISO (CD-ROM) Arfuso pour lancer", GRAY_TXT);
    text_center(&FONT_UI, FB_W / 2, FB_H / 2 + 70,
                "l'assistant d'installation.", GRAY_TXT);
    text_center(&FONT_SM, FB_W / 2, FB_H / 2 + 130,
                "Appuyez sur une touche pour éteindre.", 0x555555);
    fb_flush();
    while (!kbd_poll()) delay(12000);
    screen_off();
}

/* ============ Boucle principale ============ */

void kmain(void) {
    serial_init();
    serial_str("\n=== Arfuso Kernel 1.5.0 (noyau maison) ===\n");
    fb_init();
    serial_str("Framebuffer initialisé.\n");
    disk_ok = ata_find_disk(&the_disk);
    serial_str(disk_ok ? "Disque ATA trouvé.\n" : "Pas de disque ATA.\n");
    kbd_init();
    mouse_init();
    serial_str("Clavier + souris prêts.\n");

    u8 bootdrv = *(volatile u8 *)0x05F8;
    serial_str("Lecteur d'amorçage : 0x");
    serial_hex(bootdrv);
    serial_str("\n");

    if (bootdrv >= 0xE0) {
        serial_str("Boot CD-ROM -> installateur.\n");
        installer_wizard();        /* ne retourne jamais */
    }

    u8 cf_ed = 0, cf_plat = 0, cf_kbd = 0, cf_lang = 0, cf_booted = 1;
    i32 cf_tz = 0;
    int installed = disk_ok &&
        installer_read_config(&the_disk, &cf_ed, &cf_plat, &cf_kbd, &cf_lang, &cf_tz, &cf_booted);
    if (!installed) {
        serial_str("Disque sans installation Arfuso -> arret.\n");
        screen_not_installed();
    }

    edition    = cf_ed & 3;
    g_platform = cf_plat & 1;
    g_kbd      = cf_kbd & 1;
    g_lang     = cf_lang & 1;
    rtc_tz     = cf_tz;
    settings_load(&the_disk);      /* réglages persistés (thème, accent, veille...) */
    serial_str("Configuration Arfuso chargee, noyau OS lance.\n");

    serial_str("kmain: afs_probe...\n");
    afs_probe();
    serial_str("kmain: users_load...\n");
    users_load();
    serial_str("kmain: fin chargements.\n");

    pw_len = 0; pw[0] = 0;
    State st = cf_booted ? ST_BOOT : ST_FBOOT;   /* premier boot après installation */
    for (;;) {
        switch (st) {
            case ST_BOOT:     st = screen_boot();    break;
            case ST_FBOOT:    st = screen_fboot();   break;
            case ST_LOCK:     st = screen_lock();    break;
            case ST_LOGIN:    st = screen_login();   break;
            case ST_WELCOME:  st = screen_welcome(); break;
            case ST_DESKTOP:  st = screen_desktop(); break;
            case ST_OFF:      screen_off();          break;
        }
    }
}
