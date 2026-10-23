/* Arfuso OS - Applications v1.3 + zones cliquables + barre des tâches + .arf */
#pragma once
#include "sys.h"
#include "rtc.h"
#include "ata.h"
#include "fs.h"

/* Identifiants d'applications natives */
enum {
    APP_NONE = 0,
    APP_NAV = 1,       /* Chrome Arfuso (navigateur web)   */
    APP_TERMINAL = 2,
    APP_NOTES = 3,     /* ArfuNotes                        */
    APP_PARAMS = 4,
    APP_SNAKE = 5,
    APP_EXPLORER = 6,  /* Explorateur de fichiers          */
    APP_STORE = 7,     /* Arfu Store                       */
    APP_LAUNCHER = 90, /* logo -> menu                     */
    APP_OFF = 91,
    APP_LOCK = 92,
    APP_SCRIPT = 93    /* visionneuse d'app .arf script    */
};

/* Valeurs de retour spéciales des applis :
   >= 1000 : "me minimiser" -> app_id = retour - 1000 (l'état static est conservé) */
#define APP_MIN_RET 1000

#define Z_CLOSE 1
#define Z_MIN   2

typedef struct { int x, y, w, h, id; } ArfZone;
#define MAX_ZONES 48
extern ArfZone ZONES[MAX_ZONES];
extern int NZONES;

void zones_clear(void);
int  zone_add(int x, int y, int w, int h, int id);
int  zone_hit(int x, int y);

/* Partagés avec kernel.c */
extern int edition;
extern const char *ED_NAMES[4];
extern const char *ED_SHORT[4];
extern AtaDisk the_disk;
extern int disk_ok;
extern u32 DESK_BG;

/* Réglages persistants de session (Paramètres) */
extern int bg_mode;                    /* 0..3 couleurs, 4 image BMP (fond.bmp) */
extern int bt_on, disp_extend;         /* bluetooth, double écran */
extern char wallpaper_file[48];        /* nom BMP dans Images/ ("" = couleur) */
extern int bg_colors[4];

void draw_desktop_base(const ArfDate *d);   /* bureau (kernel.c) */

/* ---- Fenêtre standard v1.3 : × et − à GAUCHE, titre à DROITE (style installateur) */
/* Remplit *cx,*cy : coin contenu ; *wx,*wy : coin fenêtre. */
void ui_window(const char *title, int w, int h, int *cx, int *cy);
void ui_window_at(const char *title, int w, int h, int *cx, int *cy, int *wx, int *wy);

void app_blocked(const char *raison);

/* ---- Barre des tâches (dock droit) : applis OUVERTES ---- */
#define TASK_MAX 8
typedef struct { int id; int node; int minimized; char name[24]; u32 color; char glyph[4]; } TaskEnt;
extern TaskEnt TASKS[TASK_MAX];
extern int NTASKS;
void task_add(int id, int node, const char *name, const char *glyph, u32 color);
void task_remove(int id, int node);
int  task_find(int id, int node);

/* Applications natives : boucle locale ; retour 0=fermer, 1000+id=minimiser,
   APP_OFF, APP_LOCK, ou chaînage vers une autre app. */
int app_browser(void);
int app_terminal(void);
int app_notes(void);
int app_params(void);
int app_snake(void);
int app_explorer(void);
int app_store(void);
int app_script_viewer(u32 node);      /* app .arf de type script */

/* Lanceur : liste les .arf de Applications/ + applis système */
int launcher_pick(void);              /* >0 id natif, 10000+node = .arf */

/* .arf : en-tête */
typedef struct {
    char magic[8];      /* "ARFAPP10" */
    char name[24];
    char glyph[4];
    u8  engine;         /* 1..7 natif, 200 = script */
    u8  pad[3];
    u32 color;
    u8  reserved[24];
} ArfHead;              /* 64 octets */

int  arf_engine_of(u32 node);         /* lit l'en-tête, renvoie engine (-1 err) */
void installer_seed_apps(void);       /* écrit chrome.arf etc. (installateur) */

/* Panneaux de la barre du haut (réseau / bluetooth / batterie) */
int  topbar_open(int kind);   /* 0 réseau, 1 bluetooth, 2 batterie */           /* 0 réseau, 1 bluetooth, 2 batterie */

/* Utilisateurs (comptes locaux, Système/users.csv) */
#define USER_MAX 8
typedef struct { char name[24]; char pwd[24]; } ArfUser;
extern ArfUser USERS[USER_MAX];
extern int NUSERS, CUR_USER_IDX;
void users_load(void);
void users_save(void);
int  users_add(const char *name, const char *pwd);

/* Persistance legacy des notes (secteur 900, remplacé par AFS en v1.3) */
void notes_load(void);

/* Petites chaînes */
void s_copy(char *d, const char *s, int max);
void s_append(char *d, const char *s, int max);
int  s_len(const char *s);
void s_ndec(u32 v, char *out);
