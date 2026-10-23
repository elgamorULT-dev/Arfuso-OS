/* Arfuso OS - Systèmes de fichiers : AFS (maison), ISO9660 (CD), FAT32 (USB) */
#pragma once
#include "sys.h"
#include "ata.h"

/* --------- Volumes visibles dans l'explorateur --------- */
enum { VOL_AFS = 0, VOL_ISO = 1, VOL_USB = 2 };
int  vol_count(void);
int  vol_kind(int idx);
const char *vol_name(int idx);

/* --------- AFS (disque principal) --------- */
typedef struct {
    char name[40];
    u32  size;
    u8   type;          /* 1 fichier, 2 dossier */
    u8   pad[3];
    u32  node;          /* index interne dans la table */
} AfsItem;

int  afs_present(void);                 /* un AFS est-il monté ? */
void afs_probe(void);                   /* détecte/monte l'AFS au démarrage */
int  afs_format(void);                  /* formate (installateur) */
int  afs_list(u32 dir_node, AfsItem *out, int max);
int  afs_find(u32 dir_node, const char *name, AfsItem *out);
int  afs_mkdir(u32 dir_node, const char *name);
int  afs_write_file(u32 dir_node, const char *name, const u8 *data, u32 size);
int  afs_read_file(const AfsItem *f, u8 *dst, u32 max);   /* octets lus, <0 err */
int  afs_delete(const AfsItem *f);
int  afs_rename(const AfsItem *f, const char *newname);
int  afs_root(void);                    /* node racine (0xFFFFFFFF) */
u32  afs_used_pct(void);

/* Dossiers standards (nodes mis en cache après afs_probe) */
extern int FOLD_Apps, FOLD_Docs, FOLD_Images, FOLD_Store, FOLD_System;

/* --------- ISO9660 (CD-ROM) --------- */
typedef struct {
    char name[112];
    u32  size;
    u8   is_dir;
    u8   pad[3];
} IsoItem;

int  iso_present(void);
void iso_probe(void);
int  iso_list(u32 dir_lba, u32 dir_size, IsoItem *out, int max);
int  iso_root(u32 *lba, u32 *size);
int  iso_read_at(u32 lba, u8 *dst);                     /* 2048 octets */
int  iso_read_file(u32 file_lba, u32 off, u8 *dst, u32 max);
u32  iso_item_lba(const IsoItem *it);

/* --------- Clé USB (FAT32, lecture) --------- */
int  usbvol_present(void);
void usbvol_probe(void);
typedef struct { char name[64]; u32 size; u8 is_dir; u8 pad[3]; u32 cluster; } FatItem;
int  fat_list(u32 cluster0, FatItem *out, int max);     /* cluster0=0 racine */
int  fat_read_file(u32 cluster, u32 off, u8 *dst, u32 max);

/* --------- Presse-papiers global (copier/coller) --------- */
extern int   clip_active;
extern int   clip_vol;            /* VOL_* */
extern char  clip_path[96];       /* chemin complet (pour l'affichage) */
extern AfsItem clip_afs;
extern IsoItem clip_iso;
extern FatItem clip_fat;
extern u32   clip_iso_dir_lba, clip_iso_dir_size;
extern u32   clip_fat_dir_cluster;
