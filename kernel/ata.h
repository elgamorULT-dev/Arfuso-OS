/* Arfuso OS - Pilote disque ATA en mode PIO (IDE) */
#pragma once
#include "sys.h"

typedef struct {
    int present;
    u16 base;               /* 0x1F0 ou 0x170 */
    u8  drive;              /* 0 = maître, 1 = esclave */
    u32 sectors;            /* capacité en secteurs de 512o */
    char model[41];
} AtaDisk;

typedef struct {
    int present;
    u16 base;
    u8  drive;
    char model[41];
} AtaCd;

/* Détecte le premier disque ATA (ignore les lecteurs CD ATAPI) */
int ata_find_disk(AtaDisk *d);
int ata_enum_disks(AtaDisk *out, int max);   /* nombre de disques trouvés */
int ata_write_sector(const AtaDisk *d, u32 lba, const u8 *buf512);
int ata_read_sector(const AtaDisk *d, u32 lba, u8 *buf512);
void ata_flush(const AtaDisk *d);

/* Lecteur de CD-ROM ATAPI (secteurs de 2048 octets) */
int atapi_find_cd(AtaCd *cd);
int atapi_read(const AtaCd *cd, u32 lba2048, u8 *buf2048);
