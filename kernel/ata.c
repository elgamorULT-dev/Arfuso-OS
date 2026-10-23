/* Arfuso OS - ATA PIO : détection, lecture et écriture de secteurs (sans IRQ) */
#include "ata.h"

#define REG_DATA   0
#define REG_ERR    1
#define REG_COUNT  2
#define REG_LBA0   3
#define REG_LBA1   4
#define REG_LBA2   5
#define REG_DRV    6
#define REG_STATUS 7
#define REG_CMD    7

static int wait_not_busy(u16 base) {
    for (u32 i = 0; i < 1000000; i++) {
        if (!(inb(base + REG_STATUS) & 0x80)) return 1;
    }
    return 0;
}

static int wait_drq(u16 base) {
    for (u32 i = 0; i < 1000000; i++) {
        u8 s = inb(base + REG_STATUS);
        if (s & 0x01) return -1;           /* erreur */
        if (!(s & 0x80) && (s & 0x08)) return 0;
    }
    return -1;
}

static int identify(u16 base, u8 drive, AtaDisk *d) {
    outb(base + REG_DRV, 0xA0 | (drive << 4));
    io_wait();
    outb(base + REG_COUNT, 0);
    outb(base + REG_LBA0, 0);
    outb(base + REG_LBA1, 0);
    outb(base + REG_LBA2, 0);
    outb(base + REG_CMD, 0xEC);            /* IDENTIFY */
    io_wait();
    u8 s = inb(base + REG_STATUS);
    if (s == 0) return 0;                  /* aucun périphérique */
    if (!wait_not_busy(base)) return 0;
    /* Signature ATAPI (LBA1/LBA2 != 0) -> pas un disque dur */
    if (inb(base + REG_LBA1) != 0 || inb(base + REG_LBA2) != 0) return 0;
    if (wait_drq(base) != 0) return 0;
    u16 info[256];
    insw(base + REG_DATA, info, 256);
    d->sectors = ((u32)info[61] << 16) | info[60];
    for (int i = 0; i < 20; i++) {
        d->model[i * 2]     = (char)(info[27 + i] >> 8);
        d->model[i * 2 + 1] = (char)(info[27 + i] & 0xFF);
    }
    d->model[40] = 0;
    /* enlever les espaces de fin */
    for (int i = 39; i > 0 && d->model[i] == ' '; i--) d->model[i] = 0;
    d->present = 1;
    d->base = base;
    d->drive = drive;
    return 1;
}

int ata_find_disk(AtaDisk *d) {
    static const u16 bases[2] = {0x1F0, 0x170};
    d->present = 0;
    for (int b = 0; b < 2; b++)
        for (int dr = 0; dr < 2; dr++)
            if (identify(bases[b], dr, d)) return 1;
    return 0;
}

int ata_enum_disks(AtaDisk *out, int max) {
    static const u16 bases[2] = {0x1F0, 0x170};
    int n = 0;
    for (int b = 0; b < 2 && n < max; b++)
        for (int dr = 0; dr < 2 && n < max; dr++) {
            out[n].present = 0;
            if (identify(bases[b], dr, &out[n])) n++;
        }
    return n;
}

int ata_write_sector(const AtaDisk *d, u32 lba, const u8 *buf512) {
    if (!wait_not_busy(d->base)) return 0;
    outb(d->base + REG_DRV, (u8)(0xE0 | (d->drive << 4) | ((lba >> 24) & 0x0F)));
    io_wait();
    outb(d->base + REG_COUNT, 1);
    outb(d->base + REG_LBA0, (u8)(lba & 0xFF));
    outb(d->base + REG_LBA1, (u8)((lba >> 8) & 0xFF));
    outb(d->base + REG_LBA2, (u8)((lba >> 16) & 0xFF));
    outb(d->base + REG_CMD, 0x30);         /* WRITE SECTORS */
    if (wait_drq(d->base) != 0) return 0;
    outsw(d->base + REG_DATA, buf512, 256);
    if (!wait_not_busy(d->base)) return 0;
    return 1;
}

int ata_read_sector(const AtaDisk *d, u32 lba, u8 *buf512) {
    if (!wait_not_busy(d->base)) return 0;
    outb(d->base + REG_DRV, (u8)(0xE0 | (d->drive << 4) | ((lba >> 24) & 0x0F)));
    io_wait();
    outb(d->base + REG_COUNT, 1);
    outb(d->base + REG_LBA0, (u8)(lba & 0xFF));
    outb(d->base + REG_LBA1, (u8)((lba >> 8) & 0xFF));
    outb(d->base + REG_LBA2, (u8)((lba >> 16) & 0xFF));
    outb(d->base + REG_CMD, 0x20);         /* READ SECTORS */
    if (wait_drq(d->base) != 0) return 0;
    insw(d->base + REG_DATA, buf512, 256);
    if (!wait_not_busy(d->base)) return 0;
    return 1;
}

void ata_flush(const AtaDisk *d) {
    outb(d->base + REG_CMD, 0xE7);         /* FLUSH CACHE */
    wait_not_busy(d->base);
}

/* ================= ATAPI (CD-ROM) ================= */

static int identify_packet(u16 base, u8 drive, AtaCd *cd) {
    outb(base + REG_DRV, 0xA0 | (drive << 4));
    io_wait();
    outb(base + REG_COUNT, 0);
    outb(base + REG_LBA0, 0);
    outb(base + REG_LBA1, 0);
    outb(base + REG_LBA2, 0);
    outb(base + REG_CMD, 0xA1);            /* IDENTIFY PACKET DEVICE */
    io_wait();
    u8 s = inb(base + REG_STATUS);
    if (s == 0) return 0;
    if (!wait_not_busy(base)) return 0;
    if (wait_drq(base) != 0) return 0;
    u16 info[256];
    insw(base + REG_DATA, info, 256);
    if ((info[0] & 0x1F00) != 0x0500) return 0;   /* type 5 = CD-ROM */
    for (int i = 0; i < 20; i++) {
        cd->model[i * 2]     = (char)(info[27 + i] >> 8);
        cd->model[i * 2 + 1] = (char)(info[27 + i] & 0xFF);
    }
    cd->model[40] = 0;
    for (int i = 39; i > 0 && cd->model[i] == ' '; i--) cd->model[i] = 0;
    cd->present = 1;
    cd->base = base;
    cd->drive = drive;
    return 1;
}

int atapi_find_cd(AtaCd *cd) {
    static const u16 bases[2] = {0x1F0, 0x170};
    cd->present = 0;
    for (int b = 0; b < 2; b++)
        for (int dr = 0; dr < 2; dr++) {
            if (identify_packet(bases[b], dr, cd)) return 1;
        }
    return 0;
}

/* Lit un secteur de 2048 octets via PACKET (READ 12, un seul secteur) */
int atapi_read(const AtaCd *cd, u32 lba, u8 *buf) {
    if (!wait_not_busy(cd->base)) return 0;
    outb(cd->base + REG_DRV, (u8)(0xA0 | (cd->drive << 4)));
    io_wait();
    outb(cd->base + REG_ERR, 0);           /* features = 0 (PIO) */
    outb(cd->base + REG_LBA1, 2048 & 0xFF);        /* taille transfert */
    outb(cd->base + REG_LBA2, (2048 >> 8) & 0xFF);
    outb(cd->base + REG_CMD, 0xA0);        /* PACKET */
    if (!wait_not_busy(cd->base)) return 0;
    if (wait_drq(cd->base) != 0) return 0;
    u16 cdb[6];
    cdb[0] = 0xA8;                         /* READ(12) : oct 0=A8, oct1=00 */
    /* champs big-endian par PAIRES d'octets (outw écrit little-endian) */
    cdb[1] = (u16)((lba >> 24) | ((lba >> 8) & 0xFF00));   /* oct 2-3 */
    cdb[2] = (u16)(((lba >> 8) & 0xFF) | ((lba & 0xFF) << 8)); /* oct 4-5 */
    cdb[3] = 0x0000;                       /* oct 6-7 = 0 */
    cdb[4] = 0x0100;                       /* oct 8-9 = longueur big-endian : 1 */
    cdb[5] = 0;                            /* oct 10-11 */

    for (int i = 0; i < 6; i++) outw(cd->base + REG_DATA, cdb[i]);
    if (wait_drq(cd->base) != 0) return 0;
    insw(cd->base + REG_DATA, buf, 1024);
    if (!wait_not_busy(cd->base)) return 0;
    u8 st = inb(cd->base + REG_STATUS);
    return (st & 0x01) ? 0 : 1;
}
