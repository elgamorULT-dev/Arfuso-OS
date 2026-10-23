/* Arfuso OS - Clavier PS/2 en scrutation pure (aucune interruption dans le noyau v1) */
#include "kbd.h"

/* 0 = AZERTY (français), 1 = QWERTY (anglais) — choisi à l'installation */
extern int g_kbd;

/* Scancodes set 1 -> ASCII (disposition de référence : les lettres usuelles
   sont identiques ; le mot de passe ne contient que des lettres/chiffres). */
static const char map[128] = {
    [0x01] = 27,                 /* Échap */
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-',
    [0x0E] = '\b',               /* Retour arrière */
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1C] = '\n',               /* Entrée */
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x27] = ';', [0x28] = '\'',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x0D] = '=',
    [0x39] = ' ',
    [0x52] = '0', [0x4F] = '1', [0x50] = '2', [0x51] = '3',
    [0x4B] = '4', [0x4C] = '5', [0x4D] = '6', [0x47] = '7',
    [0x48] = '8', [0x49] = '9',
    [0x53] = 127,
};

static int i8042_wait_read(void) {
    for (u32 i = 0; i < 100000; i++)
        if (inb(0x64) & 1) return inb(0x60);
    return -1;
}

void kbd_init(void) {
    outb(0x64, 0xAD);                 /* désactiver le port clavier */
    io_wait();
    inb(0x60);                        /* vider le tampon */
    outb(0x64, 0x20);                 /* lire l'octet de configuration */
    int cfg = i8042_wait_read();
    if (cfg < 0) cfg = 0x41;
    cfg &= ~0x10;                     /* horloge clavier activée */
    cfg |= 0x41;                      /* IRQ1 + traduction scancode set 1 */
    outb(0x64, 0x60);
    io_wait();
    for (u32 i = 0; i < 100000 && (inb(0x64) & 2); i++) {}
    outb(0x60, (u8)cfg);
    outb(0x64, 0xAE);                 /* réactiver le port clavier */
    io_wait();
    for (u32 i = 0; i < 100000 && (inb(0x64) & 2); i++) {}
    outb(0x60, 0xF4);                 /* activer le balayage des touches */
    i8042_wait_read();                /* accusé 0xFA attendu */
}

int kbd_poll(void) {
    static int e0 = 0;
    u8 st = inb(0x64);
    if (!(st & 1)) return 0;
    if (st & 0x20) return 0;        /* octet destiné à la souris : ne pas toucher */
    u8 sc = inb(0x60);
    if (sc == 0xE0) { e0 = 1; return 0; }
    if (sc & 0x80) {               /* relâchement : ignorer */
        u8 code = sc & 0x7F;
        if (e0 && (code == 0x48 || code == 0x50 || code == 0x4B || code == 0x4D)) e0 = 0;
        if (!e0 && (code == 0x1D)) {} /* ctrl : ignoré en v1 */
        return 0;
    }
    if (e0) {
        e0 = 0;
        switch (sc) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
        }
        return 0;
    }
    /* flèches du pavé numérique sans NumLock (set de base) : ignorées */
    if (sc < 128) {
        char c = map[sc];
        /* Disposition choisie à l'installation : AZERTY français */
        if (g_kbd == 0) {
            switch (c) {
                case 'q': c = 'a'; break;   /* touche physique A d'un AZERTY */
                case 'a': c = 'q'; break;   /* touche physique Q d'un AZERTY */
                case 'w': c = 'z'; break;   /* touche physique Z d'un AZERTY */
                case 'z': c = 'w'; break;   /* touche physique W d'un AZERTY */
                case ';': c = 'm'; break;   /* touche M : à droite de L      */
                case ',': c = ';'; break;
                case 'm': c = ','; break;
            }
        }
        return c;
    }
    return 0;
}
