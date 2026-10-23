/*
 * Arfuso OS - Pilote de périphérique de pointage USB (usb-tablet QEMU)
 * =====================================================================
 * Contrôleur : UHCI (USB 1.1, présent sur les machines i440fx/PIIX3/4).
 * Périphérique : QUEMU USB Tablet (classe HID, bouton + X/Y absolus).
 * Le pilote énumère le device sur le bus, récupère l'endpoint
 * d'interruption IN et [3] décode les rapports de 5 octets :
 *     [boutons][X lo][X hi][Y lo][Y hi]   (X/Y : 0x0000..0x7FFF)
 * Tout est scruté en polling, comme le reste du noyau.
 */
#include "usbtab.h"
#include "sys.h"
#include "fb.h"

/* ---------- PCI ---------- */
#define PCI_CMD   0xCF8
#define PCI_DATA  0xCFC


static u32 pci_read(u8 bus, u8 dev, u8 fn, u8 off) {
    outl(PCI_CMD, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                  ((u32)fn << 8) | (off & 0xFC));
    return inl(PCI_DATA);
}
static void pci_write(u8 bus, u8 dev, u8 fn, u8 off, u32 v) {
    outl(PCI_CMD, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                  ((u32)fn << 8) | (off & 0xFC));
    outl(PCI_DATA, v);
}

/* ---------- UHCI ---------- */
/* Registres en espace d'E/S (offsets depuis BAR4) */
#define U_USBCMD    0x00
#define U_USBSTS    0x02
#define U_USBINTR   0x04
#define U_FRNUM     0x06
#define U_FRBASE    0x08
#define U_SOFMOD    0x0C
#define U_PORTSC1   0x10
#define U_PORTSC2   0x12

/* Structures placées en mémoire basse (identité mappée, < 640 Ko) */
#define FRAME_LIST   ((volatile u32 *)0x61000)     /* 4 Ko, 1024 entrées */
#define QH_POOL      ((volatile u32 *)0x62000)     /* QH : 16 o alignés  */
#define TD_POOL      ((volatile u32 *)0x62100)     /* TD : 32 o chacun   */
#define BUF_POOL     ((volatile u8  *)0x62400)     /* tampons            */

#define TD_ACTIVE    0x00800000u
#define TD_IOC       0x01000000u
#define TD_CERR3     0x18000000u
#define TD_SPD       0x20000000u
#define TD_ERROR_MSK 0x00400000u    /* Stall = erreur fatale pour nous */
#define TD_ACTLEN(c) (((c) & 0x7FF) + 1)

#define PID_IN       0x69
#define PID_OUT      0xE1
#define PID_SETUP    0x2D

static u16 usb_io = 0;
static int usb_ready = 0;
static int tab_addr = 0;                 /* adresse du device après énumération */
static int tab_ep = 0;                   /* endpoint IN d'interruption       */
static int tab_mps = 8;                  /* taille max du paquet             */
static int tab_toggle = 0;
static volatile u32 *tab_td = 0;
static int tab_prev_btn = 0;

static void udelay_ms_approx(u64 ms) {
    /* ~1 ms ≈ 100000 nops en TCG QEMU (calibrage grossier, tolérant) */
    delay(ms * 100000);
}

/* Attend la fin d'un TD (bit actif retombé), renvoie 0 si OK, <0 si erreur */
static int td_wait(volatile u32 *td) {
    u32 guard = 0;
    while (td[1] & TD_ACTIVE) {
        if (++guard > 20000000) return -2;        /* timeout */
    }
    if (td[1] & 0x007E0000u) return -1;           /* erreurs diverses */
    return 0;
}

/* Fabrique un TD dans la réserve */
static volatile u32 *td_make(u32 link, u32 token, u32 buf_phys) {
    static u32 td_next = 0;
    volatile u32 *td = TD_POOL + (td_next++ & 63) * 8;
    td[0] = link;
    td[1] = TD_ACTIVE | TD_IOC | TD_CERR3 | TD_SPD;
    td[2] = token;
    td[3] = buf_phys;
    for (int i = 4; i < 8; i++) td[i] = 0;
    return td;
}

static void schedule_qh(volatile u32 *qh) {
    /* route toutes les frames vers cette QH */
    for (int i = 0; i < 1024; i++) FRAME_LIST[i] = ((u32)(u64)qh) | 0x2;
}
static void schedule_idle(void) {
    for (int i = 0; i < 1024; i++) FRAME_LIST[i] = 0x1;   /* T : rien */
}

static u32 td_counter = 0;

/* Petite infra d'alloc pour les tampons */
static u32 buf_alloc(void) {
    static u32 n = 0;
    u32 phys = 0x62400 + (n & 15) * 64;
    n++;
    return phys;
}

/* Reset d'un port UHCI, renvoie 1 si un périphérique y est présent/en service */
static int port_probe(u16 port) {
    u16 st = inw(port);
    if (!(st & 1)) return 0;                       /* rien de connecté    */
    outw(port, (1 << 9));                          /* port reset          */
    udelay_ms_approx(55);
    outw(port, 0x0A);                              /* stop reset + clear  */
    udelay_ms_approx(2);
    outw(port, 0x0004);                            /* enable              */
    udelay_ms_approx(10);
    st = inw(port);
    if (st & 0x0E) outw(port, st | 0x0A);          /* clear change bits   */
    return (st & 0x0005) ? 1 : 0;
}

/* ---------- Transferts de contrôle ---------- */
/*
 * setup 8 octets + stage de données optionnel + status.
 * dir_in : données lues depuis le device. Renvoie octets lus ou <0.
 */
static int usb_ctrl(int addr, int out_len, void *setup_pkt,
                    int dir_in, void *data, int data_len, int mps) {
    u32 sp = buf_alloc();
    volatile u8 *spk = (volatile u8 *)(u64)sp;
    for (int i = 0; i < 8; i++) spk[i] = ((u8 *)setup_pkt)[i];

    u32 dp = 0;
    if (data_len) {
        dp = buf_alloc();
        volatile u8 *db = (volatile u8 *)(u64)dp;
        if (!dir_in)
            for (int i = 0; i < data_len; i++) db[i] = ((u8 *)data)[i];
    }

    volatile u32 *qh = QH_POOL + (td_counter++ & 63) * 4;
    qh[0] = 1; qh[1] = 1; qh[2] = 0; qh[3] = 0;

    volatile u32 *sets = td_make(0, PID_SETUP | (addr << 8) | (0 << 15) |
                                 (0 << 19) | ((8 - 1) << 21), sp);
    volatile u32 *prev = sets;
    volatile u32 *data_td = 0;
    if (data_len) {
        int len = data_len;
        /* un seul paquet (mps >= 64 couvre nos usages ; USB tablet = 64) */
        data_td = td_make(0, (dir_in ? PID_IN : PID_OUT) | (addr << 8) |
                          (0 << 15) | (1 << 19) | ((len - 1) << 21), dp);
        prev[0] = ((u32)(u64)data_td);
        prev = data_td;
    }
    /* status : sens inverse des données ; sans stage de données -> IN (spec USB) */
    int stat_pid = (data_len == 0 || !dir_in) ? PID_IN : PID_OUT;
    volatile u32 *stat = td_make(0, stat_pid | (addr << 8) |
                                 (0 << 15) | (1 << 19) | ((0 - 1) << 21), 0);
    prev[0] = ((u32)(u64)stat);
    stat[0] = 1;                                   /* terminate */

    qh[1] = ((u32)(u64)sets);
    schedule_qh(qh);
    int rc = td_wait(stat);
    schedule_idle();

    if (rc < 0) return rc;
    if (data_len && dir_in) {
        int got = TD_ACTLEN(data_td[1]);
        if (got > data_len) got = data_len;
        volatile u8 *db = (volatile u8 *)(u64)dp;
        for (int i = 0; i < got; i++) ((u8 *)data)[i] = db[i];
        return got;
    }
    return 0;
}

/* ---------- Initialisation UHCI + énumération ---------- */

int usb_tablet_available(void) { return usb_ready; }

static void uhci_regs_init(void) {
    /* reset complet */
    outw(usb_io + U_USBCMD, 0x0004);               /* Host Controller Reset */
    udelay_ms_approx(15);
    outw(usb_io + U_USBCMD, 0x0000);
    udelay_ms_approx(2);
    /* frame list */
    outl(usb_io + U_FRBASE, 0x61000);
    outw(usb_io + U_FRNUM, 0);
    outb(usb_io + U_SOFMOD, 0x40);
    outw(usb_io + U_USBINTR, 0);                   /* pas d'IRQ, on scrutte */
    schedule_idle();
}

static int uhci_find(void) {
    for (u16 bus = 0; bus < 2; bus++)
        for (u8 dev = 0; dev < 32; dev++)
            for (u8 fn = 0; fn < 8; fn++) {
                u32 id = pci_read((u8)bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF || !(id & 0xFFFF)) continue;
                u32 cls = pci_read((u8)bus, dev, fn, 0x08);
                if ((cls >> 8) == 0x0C0300) {      /* UHCI */
                    u32 bar = pci_read((u8)bus, dev, fn, 0x20);
                    usb_io = (u16)(bar & 0xFFFC);
                    /* accès I/O + bus mastering dans le registre de commande */
                    u32 cmd = pci_read((u8)bus, dev, fn, 0x04);
                    pci_write((u8)bus, dev, fn, 0x04, cmd | 0x5);
                    serial_str("UHCI trouve, io=0x");
                    serial_hex(usb_io);
                    serial_str("\n");
                    return 1;
                }
            }
    return 0;
}

/* lecture du descripteur de configuration -> endpoint interrupt IN */
static int find_endpoint(const u8 *cfg, int len) {
    int i = 0;
    int seen_hid = 0;
    while (i + 1 < len) {
        u8 blen = cfg[i], dtype = cfg[i + 1];
        if (blen < 2 || i + blen > len) break;
        if (dtype == 4 && blen >= 9) {             /* interface */
            seen_hid = (cfg[i + 5] == 3);
        }
        if (dtype == 5 && blen >= 7 && seen_hid) { /* endpoint  */
            if ((cfg[i + 2] & 0x80) && ((cfg[i + 3] & 3) == 3)) {
                tab_ep = cfg[i + 2] & 0x0F;
                tab_mps = cfg[i + 4] | (cfg[i + 5] << 8);
                if (tab_mps > 16) tab_mps = 16;
                return 1;
            }
        }
        i += blen;
    }
    return 0;
}

static int next_addr = 1;

/* ---------- USB MSC (cle USB) : variables ---------- */
static int msc_ok = 0;
static int msc_addr = 0, msc_ep_in = 0, msc_ep_out = 0;
static int msc_mps_in = 64, msc_mps_out = 64;
static int msc_tog_in = 0, msc_tog_out = 0;

#define MSC_BUF 0x64000                  /* tampon secteurs (16 x 512) */

/* Recherche d'endpoints USB MSC (classe 8, transferts bulk) */
static int find_msc_eps(const u8 *cfg, int len) {
    int i = 0, seen = 0;
    msc_ep_in = msc_ep_out = 0;
    while (i + 1 < len) {
        u8 blen = cfg[i], dtype = cfg[i + 1];
        if (blen < 2 || i + blen > len) break;
        if (dtype == 4 && blen >= 9) seen = (cfg[i + 5] == 8);
        if (dtype == 5 && blen >= 7 && seen && ((cfg[i + 3] & 3) == 2)) {
            if (cfg[i + 2] & 0x80) {
                msc_ep_in = cfg[i + 2] & 0x0F;
                msc_mps_in = cfg[i + 4] | (cfg[i + 5] << 8);
            } else {
                msc_ep_out = cfg[i + 2] & 0x0F;
                msc_mps_out = cfg[i + 4] | (cfg[i + 5] << 8);
            }
        }
        i += blen;
    }
    if (msc_mps_in > 64) msc_mps_in = 64;
    if (msc_mps_out > 64) msc_mps_out = 64;
    return msc_ep_in && msc_ep_out;
}

/* Énumération complète d'un device présent sur un port donné.
   Renvoie 1 si tablette HID armée, 2 si cle USB prête, 0 sinon. */
static int enumerate_device(void) {
    static u8 setup_pkt[8];
    static u8 dev_desc[18];
    /* GET_DESCRIPTOR (device, 8 octets -> mps ep0) */
    setup_pkt[0] = 0x80; setup_pkt[1] = 6;
    setup_pkt[2] = 0x00; setup_pkt[3] = 1;
    setup_pkt[4] = 0;    setup_pkt[5] = 0;
    setup_pkt[6] = 8;    setup_pkt[7] = 0;
    int r = usb_ctrl(0, 0, setup_pkt, 1, dev_desc, 8, 8);
    if (r < 0) { serial_str("USB : GET_DESCRIPTOR echec\n"); return 0; }
    int ep0_mps = dev_desc[7];
    if (ep0_mps < 8) ep0_mps = 8;

    int addr = next_addr++;
    setup_pkt[0] = 0x00; setup_pkt[1] = 5;
    setup_pkt[2] = (u8)addr; setup_pkt[3] = 0;
    setup_pkt[4] = 0;    setup_pkt[5] = 0;
    setup_pkt[6] = 0;    setup_pkt[7] = 0;
    r = usb_ctrl(0, 0, setup_pkt, 1, 0, 0, ep0_mps);
    if (r < 0) { serial_str("USB : SET_ADDRESS echec\n"); return 0; }
    udelay_ms_approx(3);

    setup_pkt[0] = 0x80; setup_pkt[1] = 6;
    setup_pkt[2] = 0x00; setup_pkt[3] = 2;
    setup_pkt[4] = 0;    setup_pkt[5] = 0;
    setup_pkt[6] = 9;    setup_pkt[7] = 0;
    static u8 cfg_head[9];
    r = usb_ctrl(addr, 0, setup_pkt, 1, cfg_head, 9, ep0_mps);
    if (r < 0) { serial_str("USB : CFG echec\n"); return 0; }
    int total = cfg_head[2] | (cfg_head[3] << 8);
    if (total > 64) total = 64;
    static u8 cfg_full[64];
    setup_pkt[6] = (u8)total; setup_pkt[7] = (u8)(total >> 8);
    r = usb_ctrl(addr, 0, setup_pkt, 1, cfg_full, total, ep0_mps);
    if (r <= 0) { serial_str("USB : CFG complet echec\n"); return 0; }

    setup_pkt[0] = 0x00; setup_pkt[1] = 9;
    setup_pkt[2] = 1;    setup_pkt[3] = 0;
    setup_pkt[4] = 0;    setup_pkt[5] = 0;
    setup_pkt[6] = 0;    setup_pkt[7] = 0;
    if (usb_ctrl(addr, 0, setup_pkt, 1, 0, 0, ep0_mps) < 0) {
        serial_str("USB : SET_CONFIG echec\n"); return 0;
    }

    /* Cas 1 : tablette HID (pointeur absolu) */
    if (!usb_ready && find_endpoint(cfg_full, total)) {
        tab_addr = addr;
        tab_toggle = 0;
        tab_td = td_make(1, PID_IN | (tab_addr << 8) | (tab_ep << 15) |
                         (tab_toggle << 19) | ((16 - 1) << 21), 0x62600);
        tab_td[1] = TD_ACTIVE | TD_IOC | TD_SPD;
        volatile u32 *qh = QH_POOL + 0;
        qh[0] = 1; qh[1] = ((u32)(u64)tab_td); qh[2] = 0; qh[3] = 0;
        schedule_qh(qh);
        usb_ready = 1;
        serial_str("USB tablet prete (ep=");
        serial_hex(tab_ep);
        serial_str(")\n");
        return 1;
    }
    /* Cas 2 : cle USB (stockage de masse, bulk-only) */
    if (!msc_ok && find_msc_eps(cfg_full, total)) {
        msc_addr = addr;
        msc_tog_in = 0; msc_tog_out = 0;
        msc_ok = 1;
        serial_str("USB MSC (cle) pret, ep_in=");
        serial_hex(msc_ep_in);
        serial_str(" ep_out=");
        serial_hex(msc_ep_out);
        serial_str("\n");
        return 2;
    }
    return 0;
}

int usb_tablet_init(void) {
    usb_ready = 0;
    if (!uhci_find()) return 0;
    uhci_regs_init();

    outw(usb_io + U_USBCMD, 0x0001);       /* run */
    udelay_ms_approx(5);

    int any = 0;
    if (port_probe(usb_io + U_PORTSC1)) any |= enumerate_device();
    if (port_probe(usb_io + U_PORTSC2)) any |= enumerate_device();
    if (!any) { serial_str("USB : aucun device\n"); return 0; }
    return 1;
}

/* ---------- Transfert bulk générique (chaine de TDs) ----------
   dir_in : lecture depuis le device. toggle : DATA0/1 persistant. */
static int usb_bulk_xfer(int addr, int ep, int mps, int dir_in,
                         u32 phys, int len, int *toggle) {
    int pieces = (len + mps - 1) / mps;
    if (pieces < 1) pieces = 1;
    if (pieces > 48) pieces = 48;
    volatile u32 *qh = QH_POOL + 4;        /* QH index 1 : transfers MSC */
    volatile u32 *first = 0, *prev = 0;
    for (int p = 0; p < pieces; p++) {
        int l = len - p * mps;
        if (l > mps) l = mps;
        volatile u32 *td = td_make(0,
            (dir_in ? PID_IN : PID_OUT) | (addr << 8) | (ep << 15) |
            ((*toggle ? 1 : 0) << 19) | ((l - 1) << 21), phys + p * mps);
        if (prev) prev[0] = ((u32)(u64)td);
        else first = td;
        prev = td;
        *toggle ^= 1;
    }
    prev[0] = 1;                           /* terminate */
    qh[0] = 1; qh[1] = ((u32)(u64)first); qh[2] = 0; qh[3] = 0;
    schedule_qh(qh);
    int rc = td_wait(prev);
    /* restaure la tablette si présente */
    if (usb_ready) {
        volatile u32 *q0 = QH_POOL + 0;
        schedule_qh(q0);
    } else {
        schedule_idle();
    }
    return rc;
}

int usb_msc_available(void) { return msc_ok; }

/* BOT : CBW -> data IN -> CSW. dst : au plus 16 secteurs de 512 o. */
int usb_msc_read(u32 lba, u8 *dst, int nsecs) {
    if (!msc_ok || nsecs < 1 || nsecs > 16) return 0;
    volatile u8 *buf = (volatile u8 *)MSC_BUF;
    /* CBW 31 octets */
    for (int i = 0; i < 64; i++) buf[i] = 0;
    buf[0] = 'U'; buf[1] = 'S'; buf[2] = 'B'; buf[3] = 'C';
    buf[4] = 1; buf[5] = 0; buf[6] = 0; buf[7] = 0;          /* tag */
    int dlen = nsecs * 512;
    buf[8]  = (u8)(dlen); buf[9]  = (u8)(dlen >> 8);
    buf[10] = (u8)(dlen >> 16); buf[11] = (u8)(dlen >> 24);
    buf[12] = 0x80;                        /* device -> host */
    buf[13] = 0; buf[14] = 10;             /* LUN, taille CB */
    buf[15] = 0x28;                        /* READ(10) */
    buf[16] = 0;
    buf[17] = (u8)(lba >> 24); buf[18] = (u8)(lba >> 16);
    buf[19] = (u8)(lba >> 8);  buf[20] = (u8)(lba);
    buf[21] = 0;
    buf[22] = (u8)(nsecs >> 8); buf[23] = (u8)(nsecs);
    buf[24] = 0;
    if (usb_bulk_xfer(msc_addr, msc_ep_out, msc_mps_out, 0, MSC_BUF, 31,
                      &msc_tog_out) < 0) return 0;
    if (usb_bulk_xfer(msc_addr, msc_ep_in, msc_mps_in, 1, MSC_BUF, dlen,
                      &msc_tog_in) < 0) return 0;
    for (int i = 0; i < dlen; i++) dst[i] = buf[i];
    /* CSW 13 octets */
    if (usb_bulk_xfer(msc_addr, msc_ep_in, msc_mps_in, 1, MSC_BUF, 13,
                      &msc_tog_in) < 0) return 0;
    if (buf[0] != 'U' || buf[1] != 'S' || buf[2] != 'B' || buf[3] != 'S' ||
        buf[12] != 0) return 0;             /* signature + statut OK */
    for (int i = 0; i < dlen; i++) { }     /* données déjà copiées */
    return 1;
}

/*
 * Scrute un rapport de la tablette. Si un nouveau rapport est là :
 * met ax,ay à jour (coordonnées écran) et renvoie 1 si clic, 2 si déplacement.
 */
int usb_tablet_poll(int *ax, int *ay) {
    if (!usb_ready || !tab_td) return 0;
    if (tab_td[1] & TD_ACTIVE) return 0;           /* pas terminé */

    int rc = 0;
    if (!(tab_td[1] & 0x007E0000u)) {              /* aucune erreur */
        volatile u8 *rp = (volatile u8 *)0x62600;
        int len = TD_ACTLEN(tab_td[1]);
        if (len >= 5) {
            int btn = rp[0] & 1;
            u32 x = rp[1] | (rp[2] << 8);
            u32 y = rp[3] | (rp[4] << 8);
            *ax = (int)(x * (u32)FB_W / 0x8000);
            *ay = (int)(y * (u32)FB_H / 0x8000);
            if (*ax >= FB_W) *ax = FB_W - 1;
            if (*ay >= FB_H) *ay = FB_H - 1;
            rc = 2;
            if (btn && !tab_prev_btn) rc |= 1;     /* front montant du clic */
            tab_prev_btn = btn;
        }
    }
    /* Réarme le TD pour le rapport suivant */
    tab_toggle ^= 1;
    tab_td[2] = PID_IN | (tab_addr << 8) | (tab_ep << 15) |
                (tab_toggle << 19) | ((16 - 1) << 21);
    tab_td[3] = 0x62600;
    tab_td[1] = TD_ACTIVE | TD_IOC | TD_SPD;
    return rc;
}
