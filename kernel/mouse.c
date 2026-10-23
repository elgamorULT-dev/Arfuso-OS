/* Arfuso OS - Souris : PS/2 relatif + usb-tablet (absolu, prioritaire) */
#include "mouse.h"
#include "fb.h"
#include "usbtab.h"

int MX = 512, MY = 384;
int CURSOR_ON = 0;

static int pkt_idx = 0;
static u8 pkt[3];
static int lmb_prev = 0;

static void i8042_wait_w(void) {
    for (u32 i = 0; i < 100000 && (inb(0x64) & 2); i++) {}
}
static int i8042_wait_r(void) {
    for (u32 i = 0; i < 100000; i++)
        if (inb(0x64) & 1) return inb(0x60);
    return -1;
}

static int aux_cmd(u8 c) {
    outb(0x64, 0xD4);
    i8042_wait_w();
    outb(0x60, c);
    return i8042_wait_r();          /* 0xFA = ACK */
}

void mouse_init(void) {
    MX = FB_W / 2;
    MY = FB_H / 2;
    /* Tablette USB d'abord : bien meilleure sous Vectras/tactile */
    if (usb_tablet_init()) {
        serial_str("Souris : usb-tablet (absolu)\n");
        return;                     /* PS/2 ignoré quand la tablette existe */
    }
    serial_str("Souris : PS/2 (relatif)\n");
    i8042_wait_w();
    outb(0x64, 0xA8);               /* activer le port auxiliaire (souris) */
    i8042_wait_w();
    outb(0x64, 0x20);               /* lire la configuration */
    int cfg = i8042_wait_r();
    if (cfg < 0) cfg = 0x43;
    cfg |= 0x02;                    /* IRQ12 autorisée (inutilisée, on scrute) */
    cfg &= ~0x20;                   /* horloge souris activée */
    i8042_wait_w();
    outb(0x64, 0x60);
    i8042_wait_w();
    outb(0x60, (u8)cfg);
    aux_cmd(0xF6);                  /* réglages par défaut */
    aux_cmd(0xF4);                  /* activer l'envoi des données */
}

int mouse_event(void) {
    int ev = 0;
    /* --- usb-tablet : position ABSOLUE (prioritaire si présente) --- */
    if (usb_tablet_available()) {
        int ax, ay;
        int r = usb_tablet_poll(&ax, &ay);
        if (r) {
            if (ax != MX || ay != MY) { MX = ax; MY = ay; ev |= 1; }
            if (r & 1) ev |= 2;
        }
        /* Le PS/2 produirait des deltas parasites avec une tablette présente :
         * on vide quand même son flux sans l'utiliser. */
        for (;;) {
            u8 st = inb(0x64);
            if (!(st & 1)) break;
            if (!(st & 0x20)) break;
            (void)inb(0x60);
        }
        return ev;
    }
    /* --- PS/2 : position relative --- */
    for (;;) {
        u8 st = inb(0x64);
        if (!(st & 1)) break;       /* rien à lire */
        if (!(st & 0x20)) break;    /* octet clavier : le laisser à kbd_poll */
        u8 b = inb(0x60);
        if (pkt_idx == 0 && !(b & 0x08)) continue;   /* resynchronisation */
        pkt[pkt_idx++] = b;
        if (pkt_idx < 3) continue;
        pkt_idx = 0;
        int dx = pkt[1], dy = pkt[2];
        if (pkt[0] & 0x10) dx |= (int)0xFFFFFF00;    /* extension de signe */
        if (pkt[0] & 0x20) dy |= (int)0xFFFFFF00;
        if (dx || dy) {
            MX += dx;
            MY -= dy;                                /* PS/2 : Y inversé */
            if (MX < 0) MX = 0;
            if (MY < 0) MY = 0;
            if (MX > FB_W - 1) MX = FB_W - 1;
            if (MY > FB_H - 1) MY = FB_H - 1;
            ev |= 1;
        }
        int lmb = pkt[0] & 1;
        if (lmb && !lmb_prev) ev |= 2;               /* nouveau clic gauche */
        lmb_prev = lmb;
    }
    return ev;
}
