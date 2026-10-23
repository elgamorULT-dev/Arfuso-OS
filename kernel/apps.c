/*
 * Arfuso OS v1.3 - Applications système + .arf + barre des tâches
 * ===============================================================
 * Fenêtres façon installateur : × / − à gauche, titre à droite.
 * Les applis OUVERTES apparaissent dans le dock à droite (barre des tâches) ;
 * − minimise vers le dock, × ferme. Le lanceur liste les .arf de l'AFS.
 */
#include "apps.h"
#include "fb.h"
#include "kbd.h"
#include "mouse.h"
#include "net.h"
#include "bmp.h"
#include "installer.h"
#include "generated/colors.h"

/* Chronomètre interne : compteur d'itérations des boucles d'applis */
u32 FRAMES = 0;
#define ticks_now() (FRAMES)

/* ---------- zones ---------- */
ArfZone ZONES[MAX_ZONES];
int NZONES = 0;
void zones_clear(void) { NZONES = 0; }
int zone_add(int x, int y, int w, int h, int id) {
    if (NZONES >= MAX_ZONES) return -1;
    ZONES[NZONES].x = x; ZONES[NZONES].y = y;
    ZONES[NZONES].w = w; ZONES[NZONES].h = h;
    ZONES[NZONES].id = id;
    return NZONES++;
}
int zone_hit(int x, int y) {
    for (int i = NZONES - 1; i >= 0; i--)
        if (x >= ZONES[i].x && x < ZONES[i].x + ZONES[i].w &&
            y >= ZONES[i].y && y < ZONES[i].y + ZONES[i].h)
            return ZONES[i].id;
    return 0;
}

/* ---------- petites chaînes ---------- */
void s_copy(char *d, const char *s, int max) {
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
void s_append(char *d, const char *s, int max) {
    int i = s_len(d), j = 0;
    while (i < max - 1 && s[j]) d[i++] = s[j++];
    d[i] = 0;
}
int s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
void s_ndec(u32 v, char *out) { u32_str(v, out); }
static void zzero_buf(void *d, int n) { u8 *a = (u8 *)d; for (int i = 0; i < n; i++) a[i] = 0; }
static int s_eq_m(const char *a, const char *pref) {
    while (*pref) { if (*a++ != *pref++) return 0; }
    return 1;
}
/* nom de fichier .arf à partir du nom d'appli */
static const char *fn_of(const char *nom) {
    static char fn[44];
    int i = 0;
    while (nom[i] && nom[i] != ' ' && i < 30) {
        char c = nom[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        fn[i++] = c;
    }
    fn[i++]='.'; fn[i++]='a'; fn[i++]='r'; fn[i++]='f'; fn[i]=0;
    return fn;
}
/* case à cocher ronde */
static void radio(int x, int y, int on) {
    circle_fill(x + 12, y + 12, 11, on ? 0x1565C0 : 0xBDBDBD);
    if (on) circle_fill(x + 12, y + 12, 5, COL_WHITE);
}
static int s_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ---------- réglages globaux ---------- */
u32 DESK_BG = 0xFFFFFF;
int bg_mode = 4;                       /* 4 = image FOND.BMP par défaut */
int bt_on = 0, disp_extend = 0;
char wallpaper_file[48] = "FOND.BMP";
int bg_colors[4] = {0xFFFFFF, 0xE8F4FD, 0xF2F2F2, 0xFAF3E3};
static const char *BG_NAMES[5] = {"Blanc", "Bleu ciel", "Gris clair", "Sable", "Image"};

/* ---------- barre des tâches ---------- */
TaskEnt TASKS[TASK_MAX];
int NTASKS = 0;
void task_add(int id, int node, const char *name, const char *glyph, u32 color) {
    for (int i = 0; i < NTASKS; i++)
        if (TASKS[i].id == id && TASKS[i].node == node) return;
    if (NTASKS >= TASK_MAX) return;
    TASKS[NTASKS].id = id; TASKS[NTASKS].node = node;
    TASKS[NTASKS].minimized = 0;
    s_copy(TASKS[NTASKS].name, name, 24);
    s_copy(TASKS[NTASKS].glyph, glyph, 4);
    TASKS[NTASKS].color = color;
    NTASKS++;
}
void task_remove(int id, int node) {
    for (int i = 0; i < NTASKS; i++)
        if (TASKS[i].id == id && TASKS[i].node == node) {
            for (int j = i; j < NTASKS - 1; j++) TASKS[j] = TASKS[j + 1];
            NTASKS--;
            return;
        }
}
int task_find(int id, int node) {
    for (int i = 0; i < NTASKS; i++)
        if (TASKS[i].id == id && TASKS[i].node == node) return i;
    return -1;
}

/* ---------- fenêtre standard (style installateur) ---------- */
void ui_window_at(const char *title, int w, int h, int *cx, int *cy, int *wx, int *wy) {
    int x = FB_W / 2 - w / 2, y = FB_H / 2 - h / 2;
    if (x < 4) x = 4;
    if (y < 62) y = 62;
    rr_fill(x + 5, y + 7, w, h, 10, 0x9E9E9E);
    rr_fill(x - 3, y - 3, w + 6, h + 6, 12, COL_BLACK);
    rr_fill(x, y, w, h, 10, COL_WHITE);
    rr_fill(x, y, w, 34, 10, 0xE8E8E8);
    rect_fill(x, y + 34, w, 2, 0xB0B0B0);
    /* × fermer  /  − réduire : à GAUCHE */
    rr_fill(x + 4, y + 4, 27, 26, 5, 0xC8C8C8);
    text(&FONT_SM, x + 13, y + 9, "x", COL_DARKTXT);
    zone_add(x + 4, y + 4, 27, 26, Z_CLOSE);
    rr_fill(x + 34, y + 4, 24, 26, 5, 0xC8C8C8);
    text(&FONT_SM, x + 42, y + 9, "-", COL_DARKTXT);
    zone_add(x + 34, y + 4, 24, 26, Z_MIN);
    /* titre à DROITE */
    text_right(&FONT_UI, x + w - 14, y + 7, title, COL_DARKTXT);
    *cx = x + 20; *cy = y + 52;
    if (wx) *wx = x;
    if (wy) *wy = y;
}
void ui_window(const char *title, int w, int h, int *cx, int *cy) {
    ui_window_at(title, w, h, cx, cy, 0, 0);
}

void app_blocked(const char *raison) {
    ArfDate d; rtc_read(&d);
    draw_desktop_base(&d);
    int cx, cy;
    ui_window("Application bloquée", 560, 190, &cx, &cy);
    text(&FONT_UI, cx, cy + 20, "Cette application est indisponible :", COL_DARKTXT);
    text(&FONT_UI, cx, cy + 52, raison, 0xD32F2F);
    text(&FONT_SM, cx, cy + 92, "Cliquez ou touchez pour fermer.", 0x757575);
    fb_flush();
    for (;;) {
        if (kbd_poll()) break;
        if (mouse_event() & 2) break;
        delay(15000);
    }
}

/* ---------- utilisateurs ---------- */
ArfUser USERS[USER_MAX];
int NUSERS = 0, CUR_USER_IDX = 0;

void users_save(void) {
    if (!afs_present()) return;
    char buf[640]; int n = 0; buf[0] = 0;
    for (int i = 0; i < NUSERS && n < 600; i++) {
        s_append(buf, USERS[i].name, 640);
        s_append(buf, ";", 640);
        s_append(buf, USERS[i].pwd, 640);
        s_append(buf, "\n", 640);
        n = s_len(buf);
    }
    afs_write_file((u32)FOLD_System, "users.csv", (const u8 *)buf, (u32)s_len(buf));
}

void users_load(void) {
    NUSERS = 0;
    s_copy(USERS[0].name, "UTILISATEUR", 24);
    s_copy(USERS[0].pwd, "arfuso", 24);
    NUSERS = 1;
    CUR_USER_IDX = 0;
    if (!afs_present()) return;
    AfsItem f;
    if (!afs_find((u32)FOLD_System, "users.csv", &f)) { users_save(); return; }
    static u8 buf[640];
    int n = afs_read_file(&f, buf, 600);
    if (n <= 0) return;
    buf[n] = 0;
    int i = 0, j = 0;
    while (j < n && i < USER_MAX) {
        int k = j, w = 0;
        while (k < n && buf[k] != ';' && buf[k] != '\n' && w < 23) USERS[i].name[w++] = (char)buf[k++];
        USERS[i].name[w] = 0;
        k++; w = 0;
        while (k < n && buf[k] != '\n' && w < 23) USERS[i].pwd[w++] = (char)buf[k++];
        USERS[i].pwd[w] = 0;
        while (k < n && buf[k] != '\n') k++;
        j = k + 1;
        if (USERS[i].name[0] && USERS[i].pwd[0]) i++;
    }
    if (i > 0) NUSERS = i;
}

int users_add(const char *name, const char *pwd) {
    if (NUSERS >= USER_MAX || !name[0] || !pwd[0]) return 0;
    for (int i = 0; i < NUSERS; i++) if (s_eq(USERS[i].name, name)) return 0;
    s_copy(USERS[NUSERS].name, name, 24);
    s_copy(USERS[NUSERS].pwd, pwd, 24);
    NUSERS++;
    users_save();
    return 1;
}

/* ---------- .arf ---------- */
int arf_engine_of(u32 node) {
    AfsItem f; f.node = node; f.type = 1; f.size = 64;
    ArfHead h;
    if (!afs_present()) return -1;
    if (afs_read_file(&f, (u8 *)&h, 64) < 64) return -1;
    if (h.magic[0] != 'A' || h.magic[5] != 'P') return -1;
    return h.engine;
}

static void seed_one(int fold, const char *file, const char *name, const char *glyph,
                     u8 engine, u32 color) {
    ArfHead h;
    for (int i = 0; i < 64; i++) ((u8 *)&h)[i] = 0;
    h.magic[0]='A'; h.magic[1]='R'; h.magic[2]='F'; h.magic[3]='A';
    h.magic[4]='P'; h.magic[5]='P'; h.magic[6]='1'; h.magic[7]='0';
    s_copy(h.name, name, 24);
    s_copy(h.glyph, glyph, 4);
    h.engine = engine;
    h.color = color;
    afs_write_file((u32)fold, file, (const u8 *)&h, 64);
}

void installer_seed_apps(void) {
    if (!afs_present()) return;
    seed_one(FOLD_Apps, "chrome.arf",      "Chrome",      "Ch", APP_NAV,      0x4285F4);
    seed_one(FOLD_Apps, "explorateur.arf", "Explorateur", "Ex", APP_EXPLORER, 0x00897B);
    seed_one(FOLD_Apps, "arfustore.arf",   "Arfu Store",  "St", APP_STORE,    0x7B1FA2);
    seed_one(FOLD_Apps, "terminal.arf",    "Terminal",    "Te", APP_TERMINAL, 0x212121);
    seed_one(FOLD_Apps, "notes.arf",       "ArfuNotes",   "No", APP_NOTES,    0xF9A825);
    seed_one(FOLD_Apps, "parametres.arf",  "Paramètres",  "Pa", APP_PARAMS,   0x607D8B);
    seed_one(FOLD_Apps, "snake.arf",       "Snake",       "Sn", APP_SNAKE,    0x1B8A3B);
    /* app .arf script de démonstration : en-tête 64 o + script texte */
    seed_one(FOLD_Apps, "bienvenue.arf", "Bienvenue", "Bv", 200, 0x00BCD4);
    {
        AfsItem f;
        static u8 buf[512];
        if (afs_find((u32)FOLD_Apps, "bienvenue.arf", &f)) {
            for (int i = 0; i < 512; i++) buf[i] = 0;
            afs_read_file(&f, buf, 64);
            const char *sc =
                "TITRE|Bienvenue sur Arfuso 1.3\n"
                "TXT|Ceci est une application .arf de type script.\n"
                "TXT|Le navigateur Chrome Arfuso va sur le vrai internet (HTTP).\n"
                "TXT|L'explorateur copie vos fichiers vers le disque Arfuso.\n"
                "URL|http://neverssl.com/\n";
            int i = 64;
            while (*sc && i < 510) buf[i++] = (u8)*sc++;
            buf[i] = 0;
            afs_write_file((u32)FOLD_Apps, "bienvenue.arf", buf, (u32)i);
        }
    }
}

/* charge images/wallpaper_file dans le cache fond d'écran */
void wallpaper_load_file(void) {
    if (!wallpaper_file[0]) { bg_mode &= 3; return; }
    if (!afs_present()) return;
    AfsItem f;
    if (!afs_find((u32)FOLD_Images, wallpaper_file, &f)) return;
    if (f.size > 4u*1024u*1024u) return;
    u32 sz = f.size;
    if (afs_read_file(&f, (u8 *)0x1000000, sz) <= 0) return;
    if (bmp_fit_to_cache((const u8 *)0x1000000, (int)sz)) bg_mode = 4;
}

/* Persistance legacy (secteur 900) : conservé pour compatibilité,
   ArfuNotes enregistre désormais dans AFS. */
void notes_load(void) {}


/* ---- ponts inter-applis ---- */
static char LAUNCH_URL[128] = "";      /* URL à ouvrir dans Chrome */
void browser_goto(const char *u) { s_copy(LAUNCH_URL, u, 128); }
static int  LAUNCH_ARF_IDX = -1;       /* index .arf lancé (historique) */
static void launch_mark_arf(int i) { LAUNCH_ARF_IDX = i; }

/* =====================================================================
 * Évènement commun des fenêtres : renvoie
 *   -2 = redessiner, -1 = continuer, 0.. = zone cliquée, 100000+k = touche.
 * ===================================================================== */
static int win_event(int app_id, int *ret) {
    FRAMES++;
    int mev = mouse_event();
    if (mev & 2) {
        int z = zone_hit(MX, MY);
        if (z == Z_CLOSE) { *ret = 0; return -99; }
        if (z == Z_MIN) { *ret = APP_MIN_RET + app_id; return -99; }
        if (z > 0) return z;
        return -2;
    }
    if (mev & 1) return -2;
    int k = kbd_poll();
    if (k) { if (k >= KEY_UP && k <= KEY_RIGHT) return 200000 + k; return 100000 + (k & 0xFF); }
    delay(9000);
    return -1;
}

/* =====================================================================
 * ArfuNotes : éditeur de texte -> fichiers .txt dans Documents/
 * ===================================================================== */
static char NTXT[2048];
static int  NLEN = 0;
static char NFILE[40] = "";
static int  NAMEDLG = 0;     /* 1 = saisie du nom de fichier */

static void notes_save_file(void) {
    if (!NFILE[0]) { NAMEDLG = 1; return; }
    afs_write_file((u32)FOLD_Docs, NFILE, (const u8 *)NTXT, (u32)NLEN);
    NAMEDLG = 0;
}

int app_notes(void) {
    if (!afs_present()) { app_blocked("Disque Arfuso indisponible."); return 0; }
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int cx, cy, wx, wy;
        ui_window_at("ArfuNotes", 680, 470, &cx, &cy, &wx, &wy);
        /* barre d'outils */
        int zx = wx + 70;
        struct { const char *t; int id; } TB[3] = {
            {"Nouveau", 100}, {"Ouvrir", 101}, {"Enregistrer", 102}};
        for (int i = 0; i < 3; i++) {
            int w = 16 + s_len(TB[i].t) * 9;
            rr_fill(zx, wy + 38, w, 24, 4, 0xF9A825);
            text(&FONT_UI, zx + 8, wy + 41, TB[i].t, COL_BLACK);
            zone_add(zx, wy + 38, w, 24, TB[i].id);
            zx += w + 8;
        }
        text_right(&FONT_SM, wx + 660, wy + 42, NFILE[0] ? NFILE : "sans nom .txt", 0x757575);
        /* zone de texte */
        rr_fill(cx - 8, cy + 24, 640, 380, 4, 0xFDFDF6);
        rect_fill(cx - 9, cy + 23, 642, 382, 0);
        rect_fill(cx - 9, cy + 23, 642, 1, 0xBDBDBD);
        rect_fill(cx + 631, cy + 23, 1, 382, 0xBDBDBD);
        int tcol = cx, trow = cy + 32, maxc = 69;
        if (NAMEDLG) {
            text(&FONT_UI, tcol, trow, "Nom du fichier (.txt) : ", COL_DARKTXT);
            text(&FONT_UI, tcol + 230, trow, NFILE, 0x1565C0);
            int cw = text_w(&FONT_UI, NFILE);
            rect_fill(tcol + 230 + cw, trow, 2, FONT_UI.height, COL_DARKTXT);
            text(&FONT_SM, tcol, trow + 30, "Entrée = enregistrer    Échap = annuler", 0x757575);
        } else {
            int maxlines = 20;
            int lines = 0, col = 0;
            for (int i = 0; i < NLEN && lines < maxlines; i++) {
                char ch = NTXT[i];
                if (ch == '\n' || col >= maxc) { trow += FONT_UI.height; lines++; col = 0; tcol = cx; }
                if (ch == '\n') continue;
                char s2[2] = {ch, 0};
                text(&FONT_UI, tcol, trow, s2, COL_DARKTXT);
                tcol += 11; col++;
            }
            /* curseur */
            if ((FRAMES / 4) % 2 == 0)
                rect_fill(tcol, trow, 2, FONT_UI.height, COL_DARKTXT);
        }
        fb_flush();
        int ret = 0;
        int e = win_event(APP_NOTES, &ret);
        if (e == -99) return ret;
        if (e == 100) { NLEN = 0; NTXT[0] = 0; NFILE[0] = 0; NAMEDLG = 0; continue; }
        if (e == 101) { /* ouvrir : liste des .txt */
            static AfsItem lst[24];
            int n = afs_list((u32)FOLD_Docs, lst, 24);
            draw_desktop_base(&d);
            zones_clear();
            int cx2, cy2, wx2, wy2;
            ui_window_at("Ouvrir", 420, 360, &cx2, &cy2, &wx2, &wy2);
            int zid = 300;
            int yy = cy2 + 4;
            for (int i = 0; i < n && yy < wy2 + 330; i++) {
                if (lst[i].type != 1) continue;
                rr_fill(cx2 - 4, yy, 380, 24, 3, 0xEEEEFF);
                text(&FONT_UI, cx2 + 4, yy + 3, lst[i].name, COL_DARKTXT);
                zone_add(cx2 - 4, yy, 380, 24, zid + i);
                yy += 28;
            }
            fb_flush();
            for (;;) {
                int mev = mouse_event();
                if (mev & 2) {
                    int z = zone_hit(MX, MY);
                    if (z == Z_CLOSE || z == 0) break;
                    if (z >= 300 && z < 300 + n) {
                        int i = z - 300;
                        static u8 buf[2048];
                        int m = afs_read_file(&lst[i], buf, 2047);
                        NLEN = m < 0 ? 0 : m;
                        for (int i2 = 0; i2 < NLEN; i2++) NTXT[i2] = (char)buf[i2];
                        NTXT[NLEN] = 0;
                        s_copy(NFILE, lst[i].name, 40);
                        break;
                    }
                }
                delay(9000);
            }
            continue;
        }
        if (e == 102) { notes_save_file(); continue; }
        if (e >= 100000) {
            int k = e - 100000;
            if (NAMEDLG) {
                if ((k == '\r' || k == '\n')) { NAMEDLG = 0; if (NFILE[0]) notes_save_file(); }
                else if (k == 27) NAMEDLG = 0;
                else if (k == 8) { int l = s_len(NFILE); if (l) NFILE[l - 1] = 0; }
                else if (k >= 32 && s_len(NFILE) < 35) { int l = s_len(NFILE); NFILE[l] = (char)k; NFILE[l+1]=0; }
            } else {
                if (k == 27) return 0;
                if (k == 8 && NLEN > 0) NTXT[--NLEN] = 0;
                else if ((k == '\r' || k == '\n')) { if (NLEN < 2046) { NTXT[NLEN++] = '\n'; NTXT[NLEN] = 0; } }
                else if (k >= 32 && NLEN < 2046) { NTXT[NLEN++] = (char)k; NTXT[NLEN] = 0; }
            }
            continue;
        }
    }
}

/* =====================================================================
 * Terminal : commandes système + réseau
 * ===================================================================== */
static char TLINES[18][78];
static int TLPOS = 0;
static void tputc_line(const char *s) { s_copy(TLINES[TLPOS], s, 78); TLPOS = (TLPOS + 1) % 18; }
static void tprint(const char *s) { tputc_line(s); }

int app_terminal(void) {
    char in[72]; int ilen = 0;
    if (TLPOS == 0) tprint("Arfuso Terminal v1.3 - 'aide' pour la liste");
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int cx, cy, wx, wy;
        ui_window_at("Terminal", 700, 480, &cx, &cy, &wx, &wy);
        (void)wx;
        rr_fill(cx - 10, cy + 8, 660, 396, 4, 0x101018);
        for (int i = 0; i < 18; i++) {
            int idx = (TLPOS + i) % 18;
            if (TLINES[idx][0]) text(&FONT_SM, cx - 2, cy + 14 + i * 21, TLINES[idx], 0x9BE89B);
        }
        char prompt[80];
        s_copy(prompt, "> ", 80); s_append(prompt, in, 80);
        text(&FONT_UI, cx - 2, cy + 14 + 17 * 21 + 24, prompt, 0x9BE89B);
        if ((FRAMES / 4) % 2 == 0) {
            int cw = text_w(&FONT_UI, prompt);
            rect_fill(cx - 2 + cw, cy + 14 + 17 * 21 + 24, 2, FONT_UI.height, 0x9BE89B);
        }
        fb_flush();
        int ret = 0;
        int e = win_event(APP_TERMINAL, &ret);
        if (e == -99) return ret;
        if (e >= 100000) {
            int k = e - 100000;
            if (k == 8 && ilen > 0) in[--ilen] = 0;
            else if (k == 27) return 0;
            else if ((k == '\r' || k == '\n')) {
                char line[80];
                s_copy(line, "> ", 80); s_append(line, in, 80);
                tprint(line);
                /* --- exécution --- */
                if (s_eq(in, "")) {}
                else if (s_eq(in, "aide"))
                    tprint("ver, edition, date, heure, clear, ip, ping HOTE, get URL,"
                           " dns HOTE, vol, fichiers, verrouiller, exit");
                else if (s_eq(in, "ver")) tprint("Arfuso OS 1.3.0 - noyau maison");
                else if (s_eq(in, "edition")) tprint((char *)ED_NAMES[edition]);
                else if (s_eq(in, "clear")) { for (int i = 0; i < 18; i++) TLINES[i][0] = 0; TLPOS = 0; }
                else if (s_eq(in, "date") || s_eq(in, "heure")) {
                    char l[80], n2[16];
                    rtc_date_long(&d, l);
                    s_append(l, "  ", 80);
                    rtc_time_str(&d, n2); s_append(l, n2, 80);
                    tprint(l);
                }
                else if (s_eq(in, "ip")) {
                    if (!net_available()) tprint("Aucune carte e1000 détectée");
                    else if (!net_ready()) tprint("DHCP en cours... patientez et retapez 'ip'");
                    else {
                        char l[90];
                        s_copy(l, "IP: ", 90); ip_to_str(NET_IP, l + 4);
                        s_append(l, "   GW/DNS: ", 90); ip_to_str(NET_GW, l + s_len(l));
                        tprint(l);
                    }
                }
                else if (in[0]=='p'&&in[1]=='i'&&in[2]=='n'&&in[3]=='g'&&in[4]==' ') {
                    if (!net_ready()) tprint("Réseau non prêt (DHCP)");
                    else {
                        char name[64]; static u8 ip[4]; u32 rtt;
                        s_copy(name, in + 5, 64);
                        int n = net_resolve(name, ip);
                        if (n == 0) {
                            char l[96]; s_copy(l, "Résolu: ", 96); ip_to_str(ip, l + 8); tprint(l);
                            if (net_ping(ip, 1, &rtt) == 0) {
                                char l[96]; s_copy(l, "réponse en ", 96);
                                char n2[8]; u32_str(rtt, n2); s_append(l, n2, 96);
                                s_append(l, " ms", 96); tprint(l);
                            } else tprint("pas de réponse ICMP");
                        } else { char l[80]; s_copy(l, "DNS erreur ", 80); char e2[8]; u32_str((u32)n, e2); s_append(l, e2, 80); tprint(l); }
                    }
                }
                else if (in[0]=='g'&&in[1]=='e'&&in[2]=='t'&&in[3]==' ') {
                    if (!net_ready()) tprint("Réseau non prêt");
                    else {
                        static u8 page[8192];
                        HttpResult r;
                        tprint("Téléchargement...");
                        int m = http_get(in + 4, page, 8192, &r);
                        if (m == 0) {
                            char l[90];
                            s_copy(l, "HTTP ", 90); char n2[8]; u32_str((u32)r.status, n2);
                            s_append(l, n2, 90); s_append(l, "  ", 90);
                            u32_str(r.len, n2); s_append(l, n2, 90);
                            s_append(l, " octets en ", 90); u32_str(r.time_ms, n2);
                            s_append(l, n2, 90); s_append(l, " ms", 90);
                            tprint(l);
                            page[8191] = 0;
                            char prev[78]; int pi2 = 0;
                            for (int i = 0; page[i] && pi2 < 77; i++) {
                                char c = (char)page[i];
                                if (c < 32 || c > 126) c = '.';
                                prev[pi2++] = c;
                                if (pi2 == 77) { prev[pi2] = 0; tprint(prev); pi2 = 0; }
                            }
                            if (pi2) { prev[pi2] = 0; tprint(prev); }
                        } else {
                            char l[90]; s_copy(l, "Échec: ", 90);
                            s_append(l, net_strerror(m), 90); tprint(l);
                        }
                    }
                }
                else if (in[0]=='d'&&in[1]=='n'&&in[2]=='s'&&in[3]==' ') {
                    char name[64]; static u8 ip[4];
                    s_copy(name, in + 4, 64);
                    if (!net_ready()) tprint("Réseau non prêt");
                    else if (net_resolve(name, ip) == 0) {
                        char l[80]; s_copy(l, "= ", 80); ip_to_str(ip, l + 2); tprint(l);
                    } else tprint("Erreur DNS");
                }
                else if (s_eq(in, "vol")) {
                    char l3[80];
                    for (int v = 0; v < vol_count(); v++) {
                        s_copy(l3, "- ", 80); s_append(l3, vol_name(v), 80);
                        tprint(l3);
                    }
                }
                else if (s_eq(in, "fichiers")) {
                    static AfsItem l2[24];
                    int n = afs_list((u32)-2, l2, 24);
                    char l3[80]; s_copy(l3, "racine: ", 80);
                    char n2[8]; u32_str((u32)n, n2); s_append(l3, n2, 80);
                    s_append(l3, " entrée(s)", 80); tprint(l3);
                }
                else if (s_eq(in, "verrouiller")) return APP_LOCK;
                else if (s_eq(in, "eteindre")) return APP_OFF;
                else if (s_eq(in, "exit")) return 0;
                else if (ilen) tprint("Commande inconnue - essayez 'aide'");
                ilen = 0; in[0] = 0;
            }
            else if (k >= 32 && ilen < 70) { in[ilen++] = (char)k; in[ilen] = 0; }
        }
    }
}

/* =====================================================================
 * Snake (édition Gaming)
 * ===================================================================== */
int app_snake(void) {
    static int sx[400], sy[400], slen, dx, dy, fx, fy, alive, paused;
    static u32 last, speed, score;
    if (!alive && slen == 0) { sx[0]=8; sy[0]=8; slen=3; dx=1; dy=0; fx=4; fy=4; alive=1; paused=1; speed=7; score=0; last=0; }
    const int GRID = 16, CW = 24;
    int ret = 0, e;
    (void)fx;
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int cx, cy, wx, wy;
        ui_window_at("Snake Arfuso", GRID * CW + 220, GRID * CW + 130, &cx, &cy, &wx, &wy);
        (void)cx;
        int gx = wx + 100, gy = wy + 60;
        rect_fill(gx - 2, gy - 2, GRID * CW + 4, GRID * CW + 4, 0x1B8A3B);
        for (int i = 0; i < GRID; i++)
            for (int j = 0; j < GRID; j++)
                rect_fill(gx + i * CW, gy + j * CW, CW - 1, CW - 1, (i + j) % 2 ? 0xE0F2E0 : 0xCFE8CF);
        for (int i = 0; i < slen; i++)
            rect_fill(gx + sx[i] * CW + 1, gy + sy[i] * CW + 1, CW - 3, CW - 3, i ? 0x1B8A3B : 0x0B5A0B);
        rect_fill(gx + fx * CW + 3, gy + fy * CW + 3, CW - 7, CW - 7, 0xD32F2F);
        char sc[32];
        s_copy(sc, "Score: ", 32); char n[8]; u32_str(score, n); s_append(sc, n, 32);
        text(&FONT_UI, gx, gy + GRID * CW + 10, sc, COL_DARKTXT);
        if (paused)
            text(&FONT_UI, gx, gy + GRID / 2 * CW, alive ? "PAUSE (flèches)" : "PERDU (flèche = nouveau)", COL_BLACK);
        fb_flush();
        e = win_event(APP_SNAKE, &ret);
        if (e == -99) return ret;
        if (e >= 200000) {
            int k = e - 200000;
            if (!alive) { slen=3; sx[0]=8; sy[0]=8; dx=1; dy=0; score=0; speed=9; alive=1; continue; }
            paused = 0;
            if (k == KEY_UP && dy != 1) { dx = 0; dy = -1; }
            if (k == KEY_DOWN && dy != -1) { dx = 0; dy = 1; }
            if (k == KEY_LEFT && dx != 1) { dx = -1; dy = 0; }
            if (k == KEY_RIGHT && dx != -1) { dx = 1; dy = 0; }
        }
        if (e >= 100000 && (e - 100000) == 32) paused = !paused;
        if (alive && !paused) {
            if (last == 0) last = FRAMES;
            if (FRAMES - last >= speed) {
                last = FRAMES;
                int nx = sx[0] + dx, ny = sy[0] + dy;
                nx = (nx + GRID) % GRID; ny = (ny + GRID) % GRID;
                for (int i = 0; i < slen; i++)
                    if (sx[i] == nx && sy[i] == ny) alive = 0;
                if (alive) {
                    int grow = (nx == fx && ny == fy);
                    if (grow) { score += 10; if (speed > 3) speed--; }
                    if (!grow) {
                        for (int i = slen - 1; i > 0; i--) { sx[i] = sx[i-1]; sy[i] = sy[i-1]; }
                    } else {
                        for (int i = slen; i > 0; i--) { sx[i] = sx[i-1]; sy[i] = sy[i-1]; }
                        slen++;
                        if (slen > 399) slen = 399;
                    }
                    sx[0] = nx; sy[0] = ny;
                    if (grow) {
                        u32 t = FRAMES;
                        fx = (t * 2654435761u) % GRID;
                        fy = ((t >> 3) * 40503u) % GRID;
                    }
                }
            }
        }
    }
}

/* =====================================================================
 * Visionneuse d'app .arf (type script) : TITRE|, TXT|, URL|
 * ===================================================================== */
int app_script_viewer(u32 node) {
    AfsItem f; f.node = node; f.type = 1;
    static u8 buf[1536];
    if (!afs_present()) { app_blocked("Disque Arfuso indisponible."); return 0; }
    int n = afs_read_file(&f, buf, 1535);
    if (n < 64) { app_blocked("Fichier .arf illisible."); return 0; }
    buf[n] = 0;
    ArfHead *h = (ArfHead *)buf;
    char *sc = (char *)buf + 64;
    char url[128] = "", titre[64];
    s_copy(titre, h->name, 64);
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int cx, cy, wx, wy;
        ui_window_at(titre, 620, 380, &cx, &cy, &wx, &wy);
        (void)cx; (void)wx;
        int yy = cy + 6;
        char *p = sc;
        int dlg_url = 0;
        while (*p && yy < wy + 330) {
            char line[96]; int li = 0;
            while (*p && *p != '\n' && li < 90) line[li++] = *p++;
            line[li] = 0;
            if (*p == '\n') p++;
            if (li > 6 && line[0]=='T'&&line[1]=='I'&&line[2]=='T'&&line[3]=='R'&&line[4]=='E'&&line[5]=='|')
                text(&FONT_TTL, wx + 30, yy, line + 6, 0x1565C0), yy += FONT_TTL.height + 12;
            else if (li > 4 && line[0]=='T'&&line[1]=='X'&&line[2]=='T'&&line[3]=='|')
                text(&FONT_UI, wx + 30, yy, line + 4, COL_DARKTXT), yy += FONT_UI.height + 6;
            else if (li > 4 && line[0]=='U'&&line[1]=='R'&&line[2]=='L'&&line[3]=='|') {
                s_copy(url, line + 4, 128);
                rr_fill(wx + 30, yy, 300, 30, 5, 0x212121);
                text(&FONT_UI, wx + 44, yy + 6, "Ouvrir dans Chrome", COL_WHITE);
                zone_add(wx + 30, yy, 300, 30, 150);
                yy += 40;
                dlg_url = 1;
            }
        }
        (void)dlg_url;
        fb_flush();
        int ret = 0;
        int e = win_event(APP_SCRIPT, &ret);
        if (e == -99) return ret;
        if (e == 150) { /* ouvrir URL dans Chrome */
            browser_goto(url);
            return APP_NAV;
        }
    }
}

/* =====================================================================
 * Lanceur : applis .arf (Applications/) + démarrage dynamique
 * ===================================================================== */
typedef struct { u32 node; char name[26]; char glyph[5]; u8 engine; u32 color; } LItem;
static LItem LITEMS[14];
static int  NLIT = 0;
static int  LSCRL = 0;

static void launcher_collect(void) {
    NLIT = 0;
    if (afs_present()) {
        static AfsItem lst[24];
        int n = afs_list((u32)FOLD_Apps, lst, 24);
        for (int i = 0; i < n && NLIT < 10; i++) {
            if (lst[i].type != 1 || lst[i].size < 64) continue;
            static u8 buf2[64];
            AfsItem it = lst[i];
            int m = afs_read_file(&it, buf2, 64);
            if (m < 64) continue;
            ArfHead *h = (ArfHead *)buf2;
            if (h->magic[0] != 'A' || h->magic[5] != 'P') continue;
            LITEMS[NLIT].node = it.node;
            s_copy(LITEMS[NLIT].name, h->name, 26);
            if (!LITEMS[NLIT].name[0]) s_copy(LITEMS[NLIT].name, it.name, 26);
            s_copy(LITEMS[NLIT].glyph, h->glyph, 5);
            LITEMS[NLIT].engine = h->engine;
            LITEMS[NLIT].color = h->color;
            NLIT++;
        }
    }
    LSCRL = 0;
}

int launcher_pick(void) {
    launcher_collect();
    int mx = FB_W / 2, my = FB_H / 2;
    if (mx > 8 && mx < 168 && my > FB_H - 80) mx = FB_W / 2;   /* près du logo -> centre */
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int w = 500, h = 420;
        int x = mx - 250; if (x < 8) x = 8; if (x > FB_W - w - 180) x = FB_W - w - 180;
        int y = my - 300; if (y < 64) y = 64;
        rr_fill(x + 5, y + 7, w, h, 10, 0xB0B0B0);
        rr_fill(x - 3, y - 3, w + 6, h + 6, 12, COL_BLACK);
        rr_fill(x, y, w, h, 10, COL_WHITE);
        text(&FONT_TTL, x + 22, y + 16, "Applications", COL_DARKTXT);
        text_right(&FONT_SM, x + w - 16, y + 24, "clic dehors = fermer", 0x757575);
        int cols = 3, cw = 156, ch = 112;
        int yy = y + 56;
        for (int i = 0; i < NLIT; i++) {
            int col = i % cols, row = i / cols;
            int ax = x + 16 + col * cw, ay = yy + row * ch;
            if (ay > y + h - 90) break;
            rr_fill(ax, ay, 140, 100, 8, 0xF5F5F5);
            rr_fill(ax + 42, ay + 10, 56, 56, 12, LITEMS[i].color);
            char g[3]; g[0] = LITEMS[i].glyph[0] ? LITEMS[i].glyph[0] : 'A';
            g[1] = LITEMS[i].glyph[1]; g[2] = 0;
            if (!g[1]) { g[1] = g[0]; g[0] = ' '; }
            text_center(&FONT_SM, ax + 42 + 28, ay + 31, g, COL_WHITE);
            text_center(&FONT_UI, ax + 4 + 66, ay + 74, LITEMS[i].name, COL_DARKTXT);
            zone_add(ax, ay, 140, 100, 200 + i);
        }
        if (!NLIT)
            text(&FONT_UI, x + 30, yy + 20, "Aucune application (.arf) sur le disque.", 0x757575);
        fb_flush();
        int mev = mouse_event();
        if (mev & 2) {
            int z = zone_hit(MX, MY);
            if (z == 0) return APP_NONE;      /* clic dehors */
            if (z >= 200 && z < 200 + NLIT) {
                int i = z - 200;
                launch_mark_arf(i);
                return 10000 + (int)LITEMS[i].node;
            }
        }
        int k = kbd_poll();
        if (k == 27) return APP_NONE;
        if (mev & 1) continue;
        delay(9000);
    }
}

/* =====================================================================
 * Chrome Arfuso : navigateur web (HTTP)
 * ===================================================================== */
#define PAGE_MAX 64000
#define LINK_MAX 96
#define LINE_MAX 320
typedef struct { char text[100]; int link; } BLine;

static u8   PAGE[PAGE_MAX];
static BLine LINES[LINE_MAX];
static struct { char url[128]; } LNK[LINK_MAX];
static int  NLINES = 0, NLNK = 0, SCROLL = 0;
static char CURL[128] = "http://neverssl.com/";
static char STATUS[140] = "Prêt. Entrez une adresse http:// et cliquez Aller.";
static int  HISTP = 0;
static char HIST[8][128];
static int  HISTN = 0;
static int  ADREDIT = 0;                 /* 1 = saisie dans la barre d'adresse */

static void hist_push(const char *u) {
    s_copy(HIST[HISTP], u, 128);
    HISTP = (HISTP + 1) % 8;
    if (HISTN < 8) HISTN++;
}

/* petit analyseur HTML : texte + liens */
static void brow_render(void) {
    NLINES = 0; NLNK = 0; SCROLL = 0;
    char cur[100]; int ci = 0;
    int cur_link = -1;
    static char href_url[128];
    int in_tag = 0, in_script = 0, in_style = 0, in_a = 0;
    int last_sp = 1;
    char tag[128]; int ti = 0;
    const u8 *p = PAGE;
    while (*p && NLINES < LINE_MAX - 1) {
        u8 c = *p;
        if (c == '<') {
            in_tag = 1; ti = 0;
            p++;
            continue;
        }
        if (in_tag) {
            if (c == '>') {
                tag[ti] = 0; in_tag = 0;
                /* balises de saut de ligne */
                if (tag[0]=='/') {
                    if ((tag[1]=='s'&&tag[2]=='c') || (ti>6&&tag[1]=='s')) in_script=0;
                    if (ti >= 6 && tag[1]=='s' && tag[2]=='t') in_style=0;
                    if (ti >= 2 && tag[1]=='a') {
                        if (in_a && href_url[0] && NLNK < LINK_MAX) {
                            s_copy(LNK[NLNK].url, href_url, 128);
                            NLNK++;
                            in_a = 0; cur_link = -1;
                        } else { in_a = 0; cur_link = -1; }
                    }
                } else if (tag[0]=='s' && tag[1]=='c') in_script=1;
                else if (tag[0]=='s' && tag[1]=='t' && tag[2]=='y') in_style=1;
                else if (tag[0]=='a' && tag[1]!=' ') { /* a href séparé ci-dessous */ }
                else if (tag[0]=='a') {
                    /* chercher href= */
                    href_url[0] = 0;
                    int k = 0;
                    while (tag[k] && !(tag[k]=='h'&&tag[k+1]=='r'&&tag[k+2]=='e'&&tag[k+3]=='f')) k++;
                    if (tag[k]) {
                        k += 4;
                        while (tag[k]=='='||tag[k]=='\"'||tag[k]=='\'') k++;
                        int m = 0;
                        while (tag[k] && tag[k]!='\"' && tag[k]!='\'' && m < 120)
                            href_url[m++] = tag[k++];
                        href_url[m] = 0;
                        /* relatives -> absolues (même hôte) */
                        if (href_url[0]=='/') {
                            char full[128];
                            full[0]=0;
                            int i2 = 0;
                            const char *p2 = CURL;
                            while (p2[i2] && i2 < 7) { full[i2] = p2[i2]; i2++; }
                            full[i2] = 0;
                            int i3 = i2;
                            while (p2[i3] && p2[i3] != '/' && i3 < 100) {
                                full[i3] = p2[i3]; i3++;
                                full[i3] = 0;
                            }
                            s_append(full, href_url, 128);
                            s_copy(href_url, full, 128);
                        }
                    }
                    in_a = 1; cur_link = NLNK;
                }
                else if (tag[0]=='b' && tag[1]=='r') {
                    if (ci) { s_copy(LINES[NLINES].text, cur, 100); LINES[NLINES].link = -1; NLINES++; ci = 0; cur[0] = 0; }
                }
                else if (tag[0]=='p' || tag[0]=='h' || tag[0]=='l' || tag[0]=='d' && tag[1]=='i' || tag[0]=='t' && tag[1]=='r') {
                    if (ci) { s_copy(LINES[NLINES].text, cur, 100); LINES[NLINES].link = -1; NLINES++; ci = 0; cur[0] = 0; }
                }
                last_sp = 1;
            } else if (ti < 126) tag[ti++] = (char)(c | (c>='A'&&c<='Z' ? 32 : 0));
            p++;
            continue;
        }
        if (in_script || in_style) { p++; continue; }
        if (c == '&') {
            if (p[1]=='a'&&p[2]=='m'&&p[3]=='p') { p += 4; c = '&'; }
            else if (p[1]=='l'&&p[2]=='t') { p += 3; c = '<'; }
            else if (p[1]=='g'&&p[2]=='t') { p += 3; c = '>'; }
            else if (p[1]=='n'&&p[2]=='b') { p += 5; c = ' '; }
            else if (p[1]=='q'&&p[2]=='u') { p += 5; c = '\"'; }
        }
        if (c < 32 || c > 126) {
            if (!last_sp && ci < 90) { cur[ci++] = ' '; last_sp = 1; }
            p++; continue;
        }
        if (ci < 90) { cur[ci++] = (char)c; last_sp = 0; }
        else {
            cur[ci] = 0;
            s_copy(LINES[NLINES].text, cur, 100);
            LINES[NLINES].link = cur_link >= 0 ? cur_link : -1;
            NLINES++;
            ci = 0; cur[0] = 0;
        }
        p++;
    }
    if (ci && NLINES < LINE_MAX - 1) {
        cur[ci] = 0;
        s_copy(LINES[NLINES].text, cur, 100);
        LINES[NLINES].link = cur_link;
        NLINES++;
    }
    LINES[NLINES].text[0] = 0;
}

static void brow_load(const char *url) {
    ADREDIT = 0;
    if (!net_available()) {
        s_copy(STATUS, "Aucune carte réseau (e1000) : activez-la dans Vectras (carte e1000 NAT).", 140);
        NLINES = 0; return;
    }
    if (!net_ready()) {
        s_copy(STATUS, "Réseau : DHCP en cours. Réessayez dans une seconde.", 140); NLINES = 0; return;
    }
    s_copy(STATUS, "Chargement de ", 140);
    s_append(STATUS, url, 140);
    s_append(STATUS, " ...", 140);
    fb_flush();
    static HttpResult r;
    int m = http_get(url, PAGE, PAGE_MAX - 2, &r);
    if (m == 0) {
        hist_push(url);
        s_copy(CURL, url, 128);
        char nbb[12];
        s_copy(STATUS, "HTTP ", 140);
        u32_str((u32)r.status, nbb); s_append(STATUS, nbb, 140);
        s_append(STATUS, " - ", 140);
        u32_str(r.len, nbb); s_append(STATUS, nbb, 140);
        s_append(STATUS, " octets", 140);
        PAGE[PAGE_MAX - 1] = 0;
        brow_render();
    } else {
        char nbb[12];
        s_copy(STATUS, "Échec : ", 140);
        s_append(STATUS, net_strerror(m), 140);
        s_append(STATUS, "  (", 140);
        u32_str((u32)(-m), nbb); s_append(STATUS, nbb, 140);
        s_append(STATUS, ")", 140);
        NLINES = 0;
    }
}

int app_browser(void) {
    if (LAUNCH_URL[0]) { s_copy(CURL, LAUNCH_URL, 128); LAUNCH_URL[0] = 0; }
    if (NLINES == 0 && PAGE[0] == 0) brow_load(CURL);
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int cx, cy, wx, wy;
        ui_window_at("Chrome Arfuso", 900, 560, &cx, &cy, &wx, &wy);
        /* barre d'adresse */
        rr_fill(wx + 70, wy + 40, 660, 34, 6, 0xF1F3F4);
        if (!CURL[0])
            text(&FONT_SM, wx + 82, wy + 48, "http://  saisir une adresse…", 0x9AA0A6);
        if (CURL[0]) {
            char disp[96]; s_copy(disp, CURL, 90);
            text(&FONT_SM, wx + 82, wy + 48, disp, COL_DARKTXT);
        }
        zone_add(wx + 70, wy + 40, 660, 34, 110);
        rr_fill(wx + 740, wy + 40, 62, 34, 6, 0x1A73E8);
        text_center(&FONT_UI, wx + 771, wy + 48, "Aller", COL_WHITE);
        zone_add(wx + 740, wy + 40, 62, 34, 111);
        rr_fill(wx + 812, wy + 40, 56, 34, 6, 0xDDE3EA);
        text_center(&FONT_UI, wx + 840, wy + 48, "<-", COL_DARKTXT);
        zone_add(wx + 812, wy + 40, 56, 34, 112);
        /* contenu : lignes rendues + liens */
        int yy = wy + 90;
        int vis = 20;
        for (int i = 0; i < vis && SCROLL + i < NLINES; i++) {
            BLine *L = &LINES[SCROLL + i];
            if (L->link >= 0 && L->link < NLNK) {
                char lab[96];
                s_copy(lab, "[", 96); s_append(lab, L->text, 76); s_append(lab, "]", 96);
                text(&FONT_UI, wx + 30, yy, lab, 0x1A0DAB);
                zone_add(wx + 28, yy - 2, 600, FONT_UI.height + 4, 400 + L->link);
            } else {
                text(&FONT_UI, wx + 30, yy, L->text, COL_DARKTXT);
            }
            yy += FONT_UI.height + 6;
        }
        if (!NLINES)
            text(&FONT_SM, wx + 30, wy + 110, "(Aucun contenu à afficher.)", 0x757575);
        /* barre de statut */
        rect_fill(wx, wy + 528, 900, 2, 0xE0E0E0);
        text(&FONT_SM, wx + 12, wy + 534, STATUS, 0x5F6368);
        text_right(&FONT_SM, wx + 888, wy + 534, net_ready() ? "● en ligne" : "○ hors ligne",
                   net_ready() ? 0x188837 : 0x9AA0A6);
        fb_flush();
        int ret = 0;
        int e = win_event(APP_NAV, &ret);
        if (e == -99) return ret;
        if (e == 110) { /* focus adresse */
            ADREDIT = 1;
            for (;;) {
                draw_desktop_base(&d);
                zones_clear();
                ui_window_at("Chrome Arfuso", 900, 560, &cx, &cy, &wx, &wy);
                rr_fill(wx + 70, wy + 40, 660, 34, 6, 0xFFFFFF);
                rect_fill(wx + 70, wy + 40, 660, 2, 0x1A73E8);
                char disp[96]; s_copy(disp, CURL, 90);
                text(&FONT_SM, wx + 82, wy + 48, disp, COL_DARKTXT);
                int cw = text_w(&FONT_SM, disp);
                rect_fill(wx + 84 + cw, wy + 45, 1, 22, 0x1A73E8);
                text(&FONT_SM, wx + 30, wy + 90, "Entrée = charger   Échap = annuler", 0x757575);
                fb_flush();
                int mev = mouse_event();
                if (mev & 2) {
                    int z = zone_hit(MX, MY);
                    if (z == Z_CLOSE) return 0;
                    if (z == Z_MIN) return APP_MIN_RET + APP_NAV;
                    if (z == 111) z = 110;   /* Aller */
                    if (z == 111 || z == 110) {
                        if (z == 111) ADREDIT = 0;
                        else { }
                        if (z == 111) { brow_load(CURL); break; }
                    }
                }
                int k = kbd_poll();
                if ((k == '\r' || k == '\n')) { ADREDIT = 0; brow_load(CURL); break; }
                if (k == 27) { ADREDIT = 0; break; }
                if (k == 8) { int l = s_len(CURL); if (l) CURL[l-1] = 0; }
                else if (k >= 32 && s_len(CURL) < 120) { int l = s_len(CURL); CURL[l] = (char)k; CURL[l+1]=0; }
                delay(9000);
            }
            continue;
        }
        if (e == 111) { brow_load(CURL); continue; }
        if (e == 112) { /* retour */
            if (HISTN > 0) {
                HISTP = (HISTP + 7) % 8;
                s_copy(CURL, HIST[HISTP], 128);
                HISTN--;
                if (HISTP) HISTP--; else HISTP = 7;
                brow_load(CURL);
            }
            continue;
        }
        if (e >= 400 && e < 400 + NLNK) { brow_load(LNK[e - 400].url); continue; }
        if (e >= 200000) {
            int k = e - 200000;
            if (k == KEY_UP && SCROLL > 0) SCROLL--;
            if (k == KEY_DOWN && SCROLL < NLINES - 18 && SCROLL < NLINES) SCROLL++;
            continue;
        }
        if (e >= 100000) {
            int k = e - 100000;
            if (k == 27) return 0;
            if (k == 'r' || k == 'R') { brow_load(CURL); continue; }
        }
    }
}

/* =====================================================================
 * Explorateur de fichiers (AFS + CD ISO9660 + clé FAT32)
 * ===================================================================== */
#define EXP_STAGE ((u8 *)0x1000000)
#define EXP_STAGE_MAX (3 * 1024 * 1024)

static int   XVOL = -1;                 /* 1=AFS 2=ISO 3=FAT */
static u32   XDIR_NODE = (u32)-2;       /* AFS : node dossier courant */
static u32   XISO_LBA = 0, XISO_SZ = 0; /* ISO : dossier courant */
static u32   XISO_LBA_ST[12], XISO_SZ_ST[12]; static int XISO_DEP = 0;
static u32   XFAT_CL = 0;               /* FAT : cluster dossier */
static u32   XFAT_ST[12]; static int XFAT_DEP = 0;
static u32   XAFS_ST[12]; static int XAFS_DEP = 0;
static int   XSEL = -1;
static char  XMSG[100] = "";

static void exp_path(char *out, int max) {
    out[0] = 0;
    if (XVOL == -1) { s_copy(out, "/Volumes", max); return; }
    if (XVOL == 1) s_copy(out, "/Disque Arfuso", max);
    if (XVOL == 2) s_copy(out, "/CD d'installation", max);
    if (XVOL == 3) s_copy(out, "/Clé USB / SD", max);
    s_append(out, "  (sans écriture)", max);
}

static int exp_count(void) {
    if (XVOL == -1) return vol_count();
    return 0;
}

int app_explorer(void) {
    static AfsItem AL[24];
    static IsoItem IL[24];
    static FatItem FL[24];
    XMSG[0] = 0; XSEL = -1;
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int cx, cy, wx, wy;
        ui_window_at("Explorateur", 800, 520, &cx, &cy, &wx, &wy);
        /* barre d'outils */
        struct { const char *t; int id; } TB[5] = {
            {"Remonter", 120}, {"Copier", 121}, {"Coller", 122},
            {"Supprimer", 123}, {"Nouveau dossier", 124}};
        int bx = wx + 70;
        for (int i = 0; i < 5; i++) {
            int w = 20 + s_len(TB[i].t) * 9;
            u32 bgc = (i == 1 || i == 2) ? 0x7B61B4 : ((i == 3) ? 0xC62828 : 0x1A73E8);
            rr_fill(bx, wy + 38, w, 26, 5, bgc);
            text(&FONT_UI, bx + 10, wy + 42, TB[i].t, COL_WHITE);
            zone_add(bx, wy + 38, w, 26, TB[i].id);
            bx += w + 6;
        }
        /* chemin */
        text(&FONT_SM, wx + 70, wy + 72, "Chemin :", 0x757575);
        char pth[140]; exp_path(pth, 140);
        text(&FONT_UI, wx + 130, wy + 70, pth,
             (XVOL == 2 || XVOL == 3) ? 0x00838F : COL_DARKTXT);
        /* liste */
        int yy = wy + 100;
        int n = 0;
        if (XVOL == -1) {
            static char vn[64];
            n = vol_count();
            for (int i = 0; i < n && i < 3; i++) {
                s_copy(vn, vol_name(i), 64);
                rr_fill(wx + 30, yy, 720, 36, 5, 0xE8F0FE);
                /* icône volume */
                rr_fill(wx + 36, yy + 7, 22, 22, 3, i == 0 ? 0x1B8A3B : (i == 1 ? 0x00838F : 0x7B61B4));
                text(&FONT_UI, wx + 70, yy + 8, vn, COL_DARKTXT);
                zone_add(wx + 30, yy, 720, 36, 300 + i);
                yy += 44;
            }
            if (!n) text(&FONT_UI, wx + 40, yy, "Aucun volume monté.", 0xC62828);
        } else if (XVOL == 1) {
            n = afs_list(XDIR_NODE, AL, 24);
            for (int i = 0; i < n; i++) {
                u32 cc = AL[i].type == 2 ? 0xFFF3E0 : (i == XSEL ? 0xE8F0FE : 0xFAFAFA);
                rr_fill(wx + 30, yy, 720, 30, 4, cc);
                if (AL[i].type == 2) text(&FONT_SM, wx + 36, yy + 5, "[DIR]", 0xE65100);
                text(&FONT_UI, wx + 110, yy + 5, AL[i].name, COL_DARKTXT);
                if (AL[i].type == 1) {
                    char nb[12], szl[16];
                    u32_str(AL[i].size, nb);
                    s_copy(szl, nb, 16); s_append(szl, " o", 16);
                    text_right(&FONT_SM, wx + 730, yy + 7, szl, 0x757575);
                }
                zone_add(wx + 30, yy, 720, 30, 300 + i);
                yy += 34;
                if (yy > wy + 470) break;
            }
            if (!n) text(&FONT_UI, wx + 40, yy, "(Dossier vide.)", 0x757575);
        } else if (XVOL == 2) {
            n = iso_list(XISO_LBA, XISO_SZ, IL, 24);
            for (int i = 0; i < n; i++) {
                rr_fill(wx + 30, yy, 720, 30, 4, i == XSEL ? 0xE8F0FE : 0xFAFAFA);
                if (IL[i].is_dir) text(&FONT_SM, wx + 36, yy + 5, "[DIR]", 0x00838F);
                text(&FONT_UI, wx + 110, yy + 5, IL[i].name, COL_DARKTXT);
                if (!IL[i].is_dir) {
                    char nb[12], szl[16];
                    u32_str(IL[i].size, nb);
                    s_copy(szl, nb, 16); s_append(szl, " o", 16);
                    text_right(&FONT_SM, wx + 730, yy + 7, szl, 0x757575);
                }
                zone_add(wx + 30, yy, 720, 30, 300 + i);
                yy += 34;
                if (yy > wy + 470) break;
            }
        } else {
            n = fat_list(XFAT_CL, FL, 24);
            for (int i = 0; i < n; i++) {
                rr_fill(wx + 30, yy, 720, 30, 4, i == XSEL ? 0xE8F0FE : 0xFAFAFA);
                if (FL[i].is_dir) text(&FONT_SM, wx + 36, yy + 5, "[DIR]", 0x7B61B4);
                text(&FONT_UI, wx + 110, yy + 5, FL[i].name, COL_DARKTXT);
                if (!FL[i].is_dir) {
                    char nb[12], szl[16];
                    u32_str(FL[i].size, nb);
                    s_copy(szl, nb, 16); s_append(szl, " o", 16);
                    text_right(&FONT_SM, wx + 730, yy + 7, szl, 0x757575);
                }
                zone_add(wx + 30, yy, 720, 30, 300 + i);
                yy += 34;
                if (yy > wy + 470) break;
            }
        }
        /* pied */
        if (XMSG[0]) text(&FONT_SM, wx + 30, wy + 486, XMSG, 0x1A73E8);
        else {
            char inf[96];
            s_copy(inf, clip_active ? "Presse-papiers : " : "Sélectionnez puis Copier / Coller (CD et USB -> Disque Arfuso).", 96);
            if (clip_active) s_append(inf, clip_path, 96);
            text(&FONT_SM, wx + 30, wy + 486, inf, 0x757575);
        }
        fb_flush();
        int ret = 0;
        int e = win_event(APP_EXPLORER, &ret);
        if (e == -99) return ret;
        XMSG[0] = 0;
        if (e == 120) { /* remonter */
            if (XVOL == 1) {
                if (XAFS_DEP > 0) { XAFS_DEP--; XDIR_NODE = XAFS_ST[XAFS_DEP]; }
                else XVOL = -1;
            } else if (XVOL == 2) {
                if (XISO_DEP > 0) { XISO_DEP--; XISO_LBA = XISO_LBA_ST[XISO_DEP]; XISO_SZ = XISO_SZ_ST[XISO_DEP]; }
                else XVOL = -1;
            } else if (XVOL == 3) {
                if (XFAT_DEP > 0) { XFAT_DEP--; XFAT_CL = XFAT_ST[XFAT_DEP]; }
                else XVOL = -1;
            }
            XSEL = -1;
            continue;
        }
        if (e == 121) { /* copier */
            if (XVOL == 1 && XSEL >= 0 && XSEL < n && AL[XSEL].type == 1) {
                clip_afs = AL[XSEL]; clip_vol = 1; clip_active = 1;
                s_copy(clip_path, "Arfuso/", 96); s_append(clip_path, AL[XSEL].name, 96);
                s_copy(XMSG, "Copié ! Allez dans un dossier Arfuso puis Coller.", 100);
            } else if (XVOL == 2 && XSEL >= 0 && XSEL < n && !IL[XSEL].is_dir) {
                clip_iso = IL[XSEL]; clip_vol = 2; clip_active = 1;
                s_copy(clip_path, "CD/", 96); s_append(clip_path, IL[XSEL].name, 96);
                s_copy(XMSG, "Fichier CD copié ! Allez sur le disque Arfuso puis Coller.", 100);
            } else if (XVOL == 3 && XSEL >= 0 && XSEL < n && !FL[XSEL].is_dir) {
                clip_fat = FL[XSEL]; clip_vol = 3; clip_active = 1;
                s_copy(clip_path, "USB/", 96); s_append(clip_path, FL[XSEL].name, 96);
                s_copy(XMSG, "Copié ! Allez sur le disque Arfuso puis Coller.", 100);
            } else s_copy(XMSG, "Sélectionnez un fichier d'abord (clic).", 100);
            continue;
        }
        if (e == 122) { /* coller */
            if (!clip_active) { s_copy(XMSG, "Rien à coller (faites d'abord Copier).", 100); continue; }
            if (XVOL != 1) { s_copy(XMSG, "Collage possible uniquement sur le disque Arfuso.", 100); continue; }
            u32 sz = clip_vol == 1 ? clip_afs.size : (clip_vol == 2 ? clip_iso.size : clip_fat.size);
            const char *nm = clip_vol == 1 ? clip_afs.name : (clip_vol == 2 ? clip_iso.name : clip_fat.name);
            if (sz == 0 || sz > EXP_STAGE_MAX) { s_copy(XMSG, "Fichier trop gros pour la copie.", 100); continue; }
            s_copy(XMSG, "Copie en cours...", 100);
            int rd = 0;
            if (clip_vol == 1) rd = afs_read_file(&clip_afs, EXP_STAGE, sz);
            else if (clip_vol == 2) rd = iso_read_file(iso_item_lba(&clip_iso), 0, EXP_STAGE, sz);
            else rd = fat_read_file(clip_fat.cluster, 0, EXP_STAGE, sz);
            if (rd < 0) { s_copy(XMSG, "Lecture source impossible.", 100); continue; }
            if (afs_write_file(XDIR_NODE, nm, EXP_STAGE, (u32)rd) == 0)
                s_copy(XMSG, "Fichier collé dans ce dossier !", 100);
            else s_copy(XMSG, "Échec du collage.", 100);
            clip_active = 0;
            continue;
        }
        if (e == 123) { /* supprimer */
            if (XVOL != 1) { s_copy(XMSG, "Suppression uniquement sur le disque Arfuso.", 100); continue; }
            if (XSEL >= 0 && XSEL < n) {
                if (afs_delete(&AL[XSEL]) == 0) {
                    s_copy(XMSG, "Supprimé.", 100);
                    XSEL = -1;
                } else s_copy(XMSG, "Suppression impossible (dossier non vide ?).", 100);
            }
            continue;
        }
        if (e == 124) { /* nouveau dossier */
            if (XVOL != 1) { s_copy(XMSG, "Dossiers uniquement sur le disque Arfuso.", 100); continue; }
            static char nd[40];
            s_copy(nd, "Nouveau dossier", 40);
            if (afs_mkdir(XDIR_NODE, nd) == 0) s_copy(XMSG, "Dossier créé !", 100);
            else s_copy(XMSG, "Échec de création.", 100);
            continue;
        }
        if (e >= 300) {
            int i = e - 300;
            if (XVOL == -1) {
                if (i < vol_count()) {
                    XVOL = vol_kind(i);
                    if (XVOL == 1) { XDIR_NODE = (u32)-2; XAFS_DEP = 0; }
                    if (XVOL == 2) { XISO_DEP = 0; u32 l,s; if (iso_root(&l,&s)) { XISO_LBA=l; XISO_SZ=s; } }
                    if (XVOL == 3) { XFAT_CL = 0; XFAT_DEP = 0; }
                    XSEL = -1;
                }
            } else if (XVOL == 1 && i < n) {
                if (AL[i].type == 2) {
                    if (XAFS_DEP < 11) { XAFS_ST[XAFS_DEP++] = XDIR_NODE; }
                    XDIR_NODE = AL[i].node;
                    XSEL = -1;
                } else XSEL = i;
            } else if (XVOL == 2 && i < n) {
                if (IL[i].is_dir) {
                    if (XISO_DEP < 11) {
                        XISO_LBA_ST[XISO_DEP] = XISO_LBA;
                        XISO_SZ_ST[XISO_DEP] = XISO_SZ;
                        XISO_DEP++;
                    }
                    XISO_LBA = iso_item_lba(&IL[i]);
                    XISO_SZ = IL[i].size;
                    XSEL = -1;
                } else XSEL = i;
            } else if (XVOL == 3 && i < n) {
                if (FL[i].is_dir) {
                    if (XFAT_DEP < 11) { XFAT_ST[XFAT_DEP++] = XFAT_CL; }
                    XFAT_CL = FL[i].cluster;
                    XSEL = -1;
                } else XSEL = i;
            }
            continue;
        }
        if (e >= 100000 && (e - 100000) == 27) return 0;
    }
}

/* =====================================================================
 * Arfu Store : installer des applications .arf
 * ===================================================================== */
typedef struct { const char *nom; const char *desc; const char *glyph; u32 color;
                 const char *script; } CatEnt;

static const char SC_INFO[] =
    "TITRE|Infos Arfuso 1.3\n"
    "TXT|Noyau 100% maison, écrit en C et assembleur.\n"
    "TXT|Fonctions : navigateur web, exploreur, boutique, jeux.\n";
static const char SC_ASTUCE[] =
    "TITRE|Astuces\n"
    "TXT|La touche Échap ferme la plupart des fenêtres.\n"
    "TXT|Le bouton - envoie l'application dans la barre à droite.\n"
    "URL|http://neverssl.com/\n";
static const char SC_GUIDE[] =
    "TITRE|Guide du Web\n"
    "TXT|Exemples d'adresses HTTP simples :\n"
    "TXT|http://neverssl.com/  -  http://info.cern.ch/ (1ère page du web)\n"
    "TXT|http://example.com/\n"
    "URL|http://info.cern.ch/\n";

static const CatEnt CATALOG[3] = {
    {"Infos système", "À propos d'Arfuso OS 1.3", "In", 0x1A73E8, SC_INFO},
    {"Astuces",       "Raccourcis et bonnes idées", "As", 0x1B8A3B, SC_ASTUCE},
    {"Guide du Web",  "Premières adresses à visiter", "Gu", 0xC62828, SC_GUIDE},
};

static char SMSG[110] = "";
static char SDLURL[110] = "http://";
static int  SDLF = 0;

static void store_install(const CatEnt *c) {
    static u8 buf[960];
    ArfHead *h = (ArfHead *)buf;
    zzero_buf(buf, 960);
    h->magic[0]='A'; h->magic[1]='R'; h->magic[2]='F'; h->magic[3]='A';
    h->magic[4]='P'; h->magic[5]='P'; h->magic[6]='1'; h->magic[7]='0';
    s_copy(h->name, c->nom, 24);
    s_copy(h->glyph, c->glyph, 4);
    h->engine = 200;
    h->color = c->color;
    int i = 64;
    const char *s = c->script;
    while (*s && i < 950) buf[i++] = (u8)*s++;
    afs_write_file((u32)FOLD_Apps, fn_of(c->nom), buf, (u32)i);
    s_copy(SMSG, "Installée ! Cherchez-la dans le menu Applications.", 110);
}

int app_store(void) {
    SMSG[0] = 0;
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int cx, cy, wx, wy;
        ui_window_at("Arfu Store", 760, 520, &cx, &cy, &wx, &wy);
        text(&FONT_TTL, wx + 30, wy + 40, "Arfu Store", 0x7B1FA2);
        text(&FONT_SM, wx + 30, wy + 78, "Téléchargez et installez des applications .arf", 0x757575);
        int yy = wy + 108;
        for (int i = 0; i < 3; i++) {
            rr_fill(wx + 30, yy, 700, 66, 6, 0xFAF5FF);
            rr_fill(wx + 40, yy + 8, 50, 50, 10, CATALOG[i].color);
            text_center(&FONT_UI, wx + 65, yy + 25, CATALOG[i].glyph, COL_WHITE);
            text(&FONT_UI, wx + 104, yy + 8, CATALOG[i].nom, COL_DARKTXT);
            text(&FONT_SM, wx + 104, yy + 36, CATALOG[i].desc, 0x757575);
            /* déjà installée ? */
            static char fnn[40];
            char *f = fn_of(CATALOG[i].nom);
            s_copy(fnn, f, 40);
            AfsItem it;
            if (afs_find((u32)FOLD_Apps, fnn, &it)) {
                rr_fill(wx + 620, yy + 16, 110, 32, 5, 0xBDBDBD);
                text_center(&FONT_UI, wx + 675, yy + 24, "Installée", COL_WHITE);
            } else {
                rr_fill(wx + 620, yy + 16, 110, 32, 5, 0x7B1FA2);
                text_center(&FONT_UI, wx + 675, yy + 24, "Installer", COL_WHITE);
                zone_add(wx + 620, yy + 16, 110, 32, 500 + i);
            }
            yy += 78;
        }
        /* téléchargement par URL */
        rect_fill(wx + 30, yy + 6, 700, 2, 0xE0E0E0);
        yy += 18;
        text(&FONT_UI, wx + 30, yy, "Installer un .arf depuis internet :", COL_DARKTXT);
        rr_fill(wx + 30, yy + 24, 540, 34, 5, SDLF ? 0xFFFFFF : 0xF1F3F4);
        if (SDLF) rect_fill(wx + 30, yy + 24, 540, 2, 0x7B1FA2);
        text(&FONT_SM, wx + 40, yy + 32, SDLURL, COL_DARKTXT);
        zone_add(wx + 30, yy + 24, 540, 34, 510);
        rr_fill(wx + 580, yy + 24, 150, 34, 5, 0x1B8A3B);
        text_center(&FONT_UI, wx + 655, yy + 31, "Télécharger", COL_WHITE);
        zone_add(wx + 580, yy + 24, 150, 34, 511);
        if (SMSG[0]) text(&FONT_SM, wx + 30, yy + 66, SMSG, 0x7B1FA2);
        fb_flush();
        int ret = 0;
        int e = win_event(APP_STORE, &ret);
        if (e == -99) return ret;
        if (e >= 500 && e < 503) { store_install(&CATALOG[e - 500]); continue; }
        if (e == 510) { SDLF = 1; continue; }
        if (e == 511) {
            if (s_len(SDLURL) < 12) { s_copy(SMSG, "Entrez une URL http://...d'abord.", 110); continue; }
            s_copy(SMSG, "Téléchargement...", 110);
            static u8 dl[8192];
            HttpResult r;
            int m = net_available() ? http_get(SDLURL, dl, 8000, &r) : -4;
            if (m == 0 && r.len > 80 && dl[0]=='A' && dl[5]=='P') {
                /* nom depuis l'en-tête */
                ArfHead *h = (ArfHead *)dl;
                char fname[40];
                s_copy(fname, fn_of(h->name), 40);
                afs_write_file((u32)FOLD_Apps, fname, dl, r.len);
                s_copy(SMSG, "Application téléchargée et installée !", 110);
            } else {
                s_copy(SMSG, "Échec : ", 110);
                s_append(SMSG, m == 0 ? "ce fichier n'est pas une app .arf" : net_strerror(m), 110);
            }
            continue;
        }
        if (e >= 100000) {
            int k = e - 100000;
            if (k == 27) return 0;
            if (SDLF) {
                if (k == 8) { int l = s_len(SDLURL); if (l > 7) SDLURL[l-1] = 0; }
                else if (k >= 32 && s_len(SDLURL) < 105) { int l = s_len(SDLURL); SDLURL[l]=(char)k; SDLURL[l+1]=0; }
            }
            continue;
        }
        if (e == -2 && SDLF) continue;
    }
}

/* =====================================================================
 * Paramètres v2 : onglets
 * ===================================================================== */
static int PTAB = 0;
static const char *PTABS[7] = {
    "Fond d'écran", "Bluetooth", "Wi-Fi", "Ethernet", "Écrans", "Comptes", "Système"
};
static char NEWU[24] = "", NEWP[24] = "";
static int  UEDIT = 0;   /* 1 = nom, 2 = mot de passe */
static int  WALL_L[12];  /* zones bmp */
static char PMSG[90] = "";

int app_params(void) {
    PMSG[0] = 0;
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int cx, cy, wx, wy;
        ui_window_at("Paramètres", 840, 520, &cx, &cy, &wx, &wy);
        /* onglets à gauche */
        for (int i = 0; i < 7; i++) {
            int yy = wy + 44 + i * 52;
            if (i == PTAB) rr_fill(wx + 6, yy, 200, 44, 6, 0x1565C0);
            else rr_fill(wx + 6, yy, 200, 44, 6, 0xF0F0F0);
            text(&FONT_UI, wx + 18, yy + 12, PTABS[i], i == PTAB ? COL_WHITE : COL_DARKTXT);
            zone_add(wx + 6, yy, 200, 44, 200 + i);
        }
        int px_ = wx + 220, py_ = wy + 52;
        /* --- contenu --- */
        if (PTAB == 0) { /* fond d'écran */
            text(&FONT_UI, px_, py_, "Choisir un fond du bureau :", COL_DARKTXT);
            py_ += 40;
            for (int i = 0; i < 4; i++) {
                int xx = px_ + i * 96;
                rr_fill(xx, py_, 84, 60, 5, bg_colors[i]);
                if (bg_mode == i) rect_fill(xx - 2, py_ - 2, 88, 64, 0x1565C0);
                zone_add(xx - 2, py_ - 2, 88, 64, 230 + i);
            }
            py_ += 76;
            text(&FONT_UI, px_, py_, "Images du disque (Dossier Images) :", COL_DARKTXT);
            py_ += 26;
            static AfsItem IM[12];
            int nb = afs_list((u32)FOLD_Images, IM, 12);
            int shown = 0;
            for (int i = 0; i < nb && shown < 12; i++) {
                if (IM[i].type != 1) continue;
                /* n'a afficher que les .BMP */
                int l = s_len(IM[i].name);
                if (l < 4) continue;
                char e1 = IM[i].name[l-1], e2 = IM[i].name[l-2];
                if (!((e1=='P'||e1=='p') && (e2=='M'||e2=='m'))) continue;
                if (bg_mode == 4 && s_eq(wallpaper_file, IM[i].name))
                    rr_fill(px_, py_, 560, 30, 4, 0xBBDEFB);
                else
                    rr_fill(px_, py_, 560, 30, 4, 0xF5F5F5);
                text(&FONT_UI, px_ + 10, py_ + 5, IM[i].name, COL_DARKTXT);
                zone_add(px_, py_, 560, 30, 240 + shown);
                WALL_L[shown] = i;
                py_ += 34;
                shown++;
            }
            if (!shown) text(&FONT_SM, px_, py_, "(Aucune image BMP. Copiez-en via l'Explorateur ou insérez le CD.)", 0x757575);
            py_ += 44;
            rr_fill(px_, py_, 300, 34, 5, 0x00838F);
            text_center(&FONT_UI, px_ + 150, py_ + 8, "Importer depuis CD/USB...", COL_WHITE);
            zone_add(px_, py_, 300, 34, 255);
        }
        if (PTAB == 1) { /* bluetooth */
            text(&FONT_UI, px_, py_, "Bluetooth", COL_DARKTXT);
            py_ += 36;
            rr_fill(px_, py_, 200, 40, 20, bt_on ? 0x2E7D32 : 0x9E9E9E);
            text_center(&FONT_UI, px_ + 100, py_ + 11, bt_on ? "ACTIVÉ" : "DÉSACTIVÉ", COL_WHITE);
            zone_add(px_, py_, 200, 40, 260);
            py_ += 56;
            if (bt_on) {
                text(&FONT_UI, px_, py_, "Appareils détectés :", COL_DARKTXT);
                text(&FONT_SM, px_, py_ + 26, "Aucun contrôleur Bluetooth sur cette machine.", 0x757575);
                text(&FONT_SM, px_, py_ + 48, "(Sous Vectras VM, le Bluetooth n'est pas émulé.)", 0x757575);
            } else {
                text(&FONT_SM, px_, py_, "Le Bluetooth est désactivé.", 0x757575);
            }
        }
        if (PTAB == 2) { /* wifi */
            text(&FONT_UI, px_, py_, "Wi-Fi", COL_DARKTXT);
            text(&FONT_SM, px_, py_ + 28, "Aucune carte Wi-Fi détectée sur cette machine.", 0x757575);
            text(&FONT_SM, px_, py_ + 50, "La connexion internet passe par l'Ethernet (e1000).", 0x757575);
            text(&FONT_SM, px_, py_ + 72, "Vectras VM n'émule pas de Wi-Fi.", 0x757575);
        }
        if (PTAB == 3) { /* ethernet */
            text(&FONT_UI, px_, py_, "Ethernet (e1000)", COL_DARKTXT);
            char l2[90];
            if (!net_available()) text(&FONT_SM, px_, py_ + 28, "Non détectée : ajoutez la carte e1000 dans Vectras.", 0xC62828);
            else {
                s_copy(l2, "Adresse IP : ", 90);
                if (net_ready()) ip_to_str(NET_IP, l2 + 13); else s_append(l2, "DHCP en cours...", 90);
                text(&FONT_UI, px_, py_ + 30, l2, COL_DARKTXT);
                char m[90];
                s_copy(m, "Passerelle : ", 90); ip_to_str(NET_GW, m + 13);
                text(&FONT_SM, px_, py_ + 58, m, 0x757575);
                s_copy(m, "DNS        : ", 90); ip_to_str(NET_DNS, m + 13);
                text(&FONT_SM, px_, py_ + 80, m, 0x757575);
                rr_fill(px_, py_ + 110, 220, 34, 5, 0x1A73E8);
                text_center(&FONT_UI, px_ + 110, py_ + 118, "Renouveler le DHCP", COL_WHITE);
                zone_add(px_, py_ + 110, 220, 34, 262);
            }
        }
        if (PTAB == 4) { /* écrans */
            text(&FONT_UI, px_, py_, "Configuration des écrans :", COL_DARKTXT);
            py_ += 36;
            struct { const char *t; int v; int id; } D[2] = {
                {"Dupliquer l'écran unique", 0, 270},
                {"Étendre la zone de travail sur un 2e écran", 1, 271}};
            for (int i = 0; i < 2; i++) {
                radio(px_, py_, disp_extend == D[i].v);
                text(&FONT_UI, px_ + 40, py_ - 1, D[i].t, COL_DARKTXT);
                zone_add(px_, py_ - 4, 560, 34, D[i].id);
                py_ += 44;
            }
            py_ += 12;
            text(&FONT_SM, px_, py_, "Second écran physique : non détecté.", 0x757575);
            text(&FONT_SM, px_, py_ + 22, "Avec « étendre », tout est prêt dès qu'il sera branché.", 0x757575);
        }
        if (PTAB == 5) { /* comptes */
            text(&FONT_UI, px_, py_, "Utilisateurs de cette machine :", COL_DARKTXT);
            py_ += 34;
            for (int i = 0; i < NUSERS; i++) {
                rr_fill(px_, py_, 330, 34, 5, i == CUR_USER_IDX ? 0xE8F0FE : 0xF5F5F5);
                text(&FONT_UI, px_ + 12, py_ + 7, USERS[i].name, COL_DARKTXT);
                if (i == CUR_USER_IDX) text_right(&FONT_SM, px_ + 322, py_ + 11, "connecté", 0x1565C0);
                py_ += 40;
            }
            py_ += 16;
            text(&FONT_UI, px_, py_, "Créer un utilisateur :", COL_DARKTXT);
            py_ += 30;
            text(&FONT_SM, px_, py_, "Nom :", 0x757575);
            rr_fill(px_ + 120, py_ - 4, 220, 30, 4, UEDIT == 1 ? 0xFFFFFF : 0xF1F3F4);
            if (UEDIT == 1) rect_fill(px_ + 120, py_ - 4, 220, 2, 0x1565C0);
            text(&FONT_UI, px_ + 128, py_ + 3, NEWU, COL_DARKTXT);
            zone_add(px_ + 120, py_ - 4, 220, 30, 280);
            py_ += 42;
            text(&FONT_SM, px_, py_, "Mot de passe :", 0x757575);
            rr_fill(px_ + 120, py_ - 4, 220, 30, 4, UEDIT == 2 ? 0xFFFFFF : 0xF1F3F4);
            if (UEDIT == 2) rect_fill(px_ + 120, py_ - 4, 220, 2, 0x1565C0);
            char mask[26]; int l = s_len(NEWP);
            for (int i = 0; i < l; i++) mask[i] = '*';
            mask[l] = 0;
            text(&FONT_UI, px_ + 128, py_ + 3, mask, COL_DARKTXT);
            zone_add(px_ + 120, py_ - 4, 220, 30, 281);
            py_ += 44;
            rr_fill(px_ + 120, py_, 180, 34, 5, 0x2E7D32);
            text_center(&FONT_UI, px_ + 210, py_ + 8, "Ajouter", COL_WHITE);
            zone_add(px_ + 120, py_, 180, 34, 282);
        }
        if (PTAB == 6) { /* système */
            text(&FONT_UI, px_, py_, (char *)ED_NAMES[edition], COL_DARKTXT);
            text(&FONT_SM, px_, py_ + 28, "Version 1.3.0 - noyau 100% maison Arfuso", 0x757575);
            char u_[60];
            s_copy(u_, "Disque Arfuso occupé à ", 60);
            char nb[8]; u32_str(afs_used_pct(), nb);
            s_append(u_, nb, 60); s_append(u_, " %", 60);
            text(&FONT_SM, px_, py_ + 50, u_, 0x757575);
            rr_fill(px_, py_ + 90, 180, 36, 5, 0xC62828);
            text_center(&FONT_UI, px_ + 90, py_ + 99, "Éteindre", COL_WHITE);
            zone_add(px_, py_ + 90, 180, 36, 290);
            rr_fill(px_ + 200, py_ + 90, 180, 36, 5, 0x455A64);
            text_center(&FONT_UI, px_ + 290, py_ + 99, "Verrouiller", COL_WHITE);
            zone_add(px_ + 200, py_ + 90, 180, 36, 291);
            if (PMSG[0]) text(&FONT_SM, px_, py_ + 140, PMSG, 0x1565C0);
        }
        fb_flush();
        int ret = 0;
        int e = win_event(APP_PARAMS, &ret);
        if (e == -99) return ret;
        if (e >= 200 && e < 207) { PTAB = e - 200; UEDIT = 0; PMSG[0] = 0; continue; }
        if (e >= 230 && e < 234) { bg_mode = e - 230; wallpaper_file[0] = 0; wallpaper_active = 0; continue; }
        if (e >= 240 && e < 252) {
            int i = WALL_L[e - 240];
            static AfsItem IM2[12];
            afs_list((u32)FOLD_Images, IM2, 12);
            s_copy(wallpaper_file, IM2[i].name, 48);
            wallpaper_load_file();
            continue;
        }
        if (e == 255) { /* import BMP depuis CD/USB */
            int done = 0;
            if (iso_present()) {
                u32 l, s;
                if (iso_root(&l, &s)) {
                    static IsoItem ri[32];
                    int nr = iso_list(l, s, ri, 32);
                    for (int i = 0; i < nr; i++) {
                        if (ri[i].is_dir) continue;
                        int ln = s_len(ri[i].name);
                        if (ln < 4 || !(ri[i].name[ln-1]=='P'||ri[i].name[ln-1]=='p')) continue;
                        if (ri[i].size > EXP_STAGE_MAX || ri[i].size == 0) continue;
                        iso_read_file(iso_item_lba(&ri[i]), 0, EXP_STAGE, ri[i].size);
                        char fnn[40]; s_copy(fnn, ri[i].name, 40);
                        afs_write_file((u32)FOLD_Images, fnn, EXP_STAGE, ri[i].size);
                        done++;
                    }
                }
            }
            if (usbvol_present()) {
                static FatItem fi[24];
                int nf = fat_list(0, fi, 24);
                for (int i = 0; i < nf; i++) {
                    if (fi[i].is_dir) continue;
                    int ln = s_len(fi[i].name);
                    if (ln < 4 || !(fi[i].name[ln-1]=='P'||fi[i].name[ln-1]=='p')) continue;
                    if (fi[i].size > EXP_STAGE_MAX || fi[i].size == 0) continue;
                    fat_read_file(fi[i].cluster, 0, EXP_STAGE, fi[i].size);
                    afs_write_file((u32)FOLD_Images, fi[i].name, EXP_STAGE, fi[i].size);
                    done++;
                }
            }
            s_copy(PMSG, done ? "Image(s) importée(s) dans Dossier Images." :
                 "Aucune image BMP trouvée sur le CD ni la clé.", 90);
            PTAB = 0;   /* revenir au fond d'écran */
            continue;
        }
        if (e == 260) { bt_on = !bt_on; continue; }
        if (e == 262) { s_copy(PMSG, "DHCP relancé en arrière-plan.", 90); net_init(); continue; }
        if (e == 270) { disp_extend = 0; continue; }
        if (e == 271) { disp_extend = 1; continue; }
        if (e == 280) { UEDIT = 1; continue; }
        if (e == 281) { UEDIT = 2; continue; }
        if (e == 282) {
            if (users_add(NEWU, NEWP)) {
                s_copy(PMSG, "Utilisateur créé (il apparaîtra à la connexion).", 90);
                NEWU[0] = 0; NEWP[0] = 0;
            } else s_copy(PMSG, "Création impossible (existe déjà ? trop long ?).", 90);
            PTAB = 6;   /* affiche le message */
            continue;
        }
        if (e == 290) return APP_OFF;
        if (e == 291) return APP_LOCK;
        if (e >= 100000) {
            int k = e - 100000;
            if (k == 27) return 0;
            if (UEDIT) {
                char *buf = UEDIT == 1 ? NEWU : NEWP;
                int l = s_len(buf);
                if (k == 8) { if (l) buf[l-1] = 0; }
                else if ((k == '\r' || k == '\n')) UEDIT = UEDIT == 1 ? 2 : 0;
                else if (k >= 32 && l < 22) { buf[l] = (char)k; buf[l+1] = 0; }
            }
            continue;
        }
    }
}

/* =====================================================================
 * Panneaux de la barre du haut : réseau, bluetooth, batterie
 * ===================================================================== */
int topbar_open(int kind) {
    for (;;) {
        ArfDate d; rtc_read(&d);
        draw_desktop_base(&d);
        zones_clear();
        int w = 420, h = kind == 2 ? 200 : (kind == 0 ? 260 : 240);
        int x = FB_W - w - 180, y = 64;
        rr_fill(x + 4, y + 6, w, h, 8, 0xB0B0B0);
        rr_fill(x - 3, y - 3, w + 6, h + 6, 10, COL_BLACK);
        rr_fill(x, y, w, h, 8, COL_WHITE);
        if (kind == 0) {
            text(&FONT_UI, x + 20, y + 14, "Réseau", COL_DARKTXT);
            char l[110];
            s_copy(l, "Ethernet : ", 110);
            if (net_available() && net_ready()) {
                s_append(l, "connecté (", 110);
                ip_to_str(NET_IP, l + s_len(l));
                s_append(l, ")", 110);
                text(&FONT_UI, x + 20, y + 48, l, 0x188837);
            } else if (net_available()) {
                s_append(l, "carte OK, DHCP en cours", 110);
                text(&FONT_UI, x + 20, y + 48, l, 0xE65100);
            } else {
                s_append(l, "carte absente", 110);
                text(&FONT_UI, x + 20, y + 48, l, 0xC62828);
            }
            text(&FONT_UI, x + 20, y + 86, "Wi-Fi : aucune carte détectée", 0x757575);
            rr_fill(x + 20, y + 130, 200, 34, 5, 0x1565C0);
            text_center(&FONT_UI, x + 120, y + 138, "Paramètres réseau", COL_WHITE);
            zone_add(x + 20, y + 130, 200, 34, 600);
            text(&FONT_SM, x + 20, y + 178, "Le Chrome se sert de la connexion Ethernet.", 0x757575);
        } else if (kind == 1) {
            text(&FONT_UI, x + 20, y + 14, "Bluetooth", COL_DARKTXT);
            rr_fill(x + 20, y + 44, 180, 36, 18, bt_on ? 0x2E7D32 : 0x9E9E9E);
            text_center(&FONT_UI, x + 110, y + 53, bt_on ? "ACTIVÉ" : "DÉSACTIVÉ", COL_WHITE);
            zone_add(x + 20, y + 44, 180, 36, 601);
            text(&FONT_UI, x + 20, y + 100, "Appareils :", COL_DARKTXT);
            text(&FONT_SM, x + 20, y + 130, bt_on ? "Aucun appareil Bluetooth en vue."
                                                 : "Activez le Bluetooth pour rechercher.",
                 0x757575);
            rr_fill(x + 20, y + 168, 240, 34, 5, 0x1565C0);
            text_center(&FONT_UI, x + 140, y + 176, "Rechercher des appareils", COL_WHITE);
            zone_add(x + 20, y + 168, 240, 34, 602);
        } else {
            text(&FONT_UI, x + 20, y + 14, "Batterie", COL_DARKTXT);
            text(&FONT_TTL, x + 20, y + 52, "100 %", 0x188837);
            text(&FONT_SM, x + 20, y + 96, "Source d'alimentation : secteur", 0x757575);
            text(&FONT_SM, x + 20, y + 122, "(Mesure exacte : ACPI non interrogé.)", 0x757575);
        }
        fb_flush();
        for (;;) {
            int mev = mouse_event();
            if (mev & 2) {
                int z = zone_hit(MX, MY);
                if (z == 600) { PTAB = 3; return APP_PARAMS; }
                if (z == 601) { bt_on = !bt_on; break; }
                if (z == 602) break;
                return 0;
            }
            delay(9000);
        }
    }
}
