/*
 * Arfuso OS — Systèmes de fichiers
 * ================================
 * AFS (Arfuso File System, maison) sur le disque principal :
 *   LBA 996   : superbloc "AFS1" + génération
 *   LBA 1000..: table de 256 entrées de 64 octets
 *   LBA 2048..: données des fichiers
 * ISO9660 : lecture du CD-ROM ATAPI.
 * FAT32   : lecture de la clé USB (via pilote USB MSC).
 */
#include "fs.h"
#include "usbtab.h"
#include "sys.h"

#define AFS_SUPER_LBA 996
#define AFS_TABLE_LBA 1000
#define AFS_TABLE_SECT 32
#define AFS_MAX_ENT   256
#define AFS_DATA_LBA  2048

typedef struct {
    char name[40];
    i32  parent;
    u32  lba;
    u32  alloc;
    u32  size;
    u8   type;          /* 0 libre, 1 fichier, 2 dossier */
    u8   pad[11];
} AfsEnt;               /* 64 octets */

int FOLD_Apps = -1, FOLD_Docs = -1, FOLD_Images = -1, FOLD_Store = -1, FOLD_System = -1;

int clip_active = 0;
int clip_vol = 0;
char clip_path[96];
AfsItem clip_afs;
IsoItem clip_iso;
FatItem clip_fat;
u32 clip_iso_dir_lba, clip_iso_dir_size;
u32 clip_fat_dir_cluster;

static const AtaDisk *afs_disk = 0;
static AfsEnt T[AFS_MAX_ENT];
static int afs_ok = 0;
static u32 afs_top = AFS_DATA_LBA;      /* premier LBA jamais utilisé */
extern AtaDisk the_disk;
extern int disk_ok;

/* ---------- helpers chaînes ---------- */
static int st_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
static void st_cp(char *d, const char *s, int max) {
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

/* ---------- AFS : table ---------- */
static int afs_load_table(void) {
    if (!disk_ok) return 0;
    u8 sec[512];
    if (!ata_read_sector(&the_disk, AFS_SUPER_LBA, sec)) return 0;
    if (sec[0] != 'A' || sec[1] != 'F' || sec[2] != 'S' || sec[3] != '1') return 0;
    if (!ata_read_sector(&the_disk, AFS_TABLE_LBA, (u8 *)T)) return 0;
    for (int s = 1; s < AFS_TABLE_SECT; s++)
        if (!ata_read_sector(&the_disk, AFS_TABLE_LBA + s, (u8 *)T + s * 512)) return 0;
    afs_top = AFS_DATA_LBA;
    for (int i = 0; i < AFS_MAX_ENT; i++)
        if (T[i].type && T[i].lba + T[i].alloc > afs_top)
            afs_top = T[i].lba + T[i].alloc;
    return 1;
}

static void afs_save_table(void) {
    u8 sec[512];
    for (int i = 0; i < 512; i++) sec[i] = 0;
    sec[0] = 'A'; sec[1] = 'F'; sec[2] = 'S'; sec[3] = '1';
    ata_write_sector(&the_disk, AFS_SUPER_LBA, sec);
    for (int s = 0; s < AFS_TABLE_SECT; s++)
        ata_write_sector(&the_disk, AFS_TABLE_LBA + s, (u8 *)T + s * 512);
    ata_flush(&the_disk);
}

static int afs_free_slot(void) {
    for (int i = 0; i < AFS_MAX_ENT; i++)
        if (!T[i].type) return i;
    return -1;
}

int afs_present(void) { return afs_ok; }
int afs_root(void) { return -1; }

static int afs_folder_get(u32 dir, const char *name) {
    AfsItem it;
    if (afs_find(dir, name, &it) && it.type == 2) return (int)it.node;
    return afs_mkdir(dir, name);
}

void afs_probe(void) {
    afs_ok = 0;
    if (!disk_ok) return;
    if (!afs_load_table()) return;
    afs_ok = 1;
    afs_disk = &the_disk;
    /* dossiers standards (créés à l'installation ; sécurité si absents) */
    FOLD_Apps   = afs_folder_get((u32)-1, "Applications");
    FOLD_Docs   = afs_folder_get((u32)-1, "Documents");
    FOLD_Images = afs_folder_get((u32)-1, "Images");
    FOLD_Store  = afs_folder_get((u32)-1, "ArfuStore");
    FOLD_System = afs_folder_get((u32)-1, "Système");
    afs_save_table();
}

int afs_format(void) {
    if (!disk_ok) return 0;
    for (int i = 0; i < AFS_MAX_ENT; i++) { T[i].type = 0; T[i].name[0] = 0; }
    afs_top = AFS_DATA_LBA;
    afs_ok = 1;
    afs_disk = &the_disk;
    FOLD_Apps = FOLD_Docs = FOLD_Images = FOLD_Store = FOLD_System = -1;
    FOLD_Apps   = afs_mkdir((u32)-1, "Applications");
    FOLD_Docs   = afs_mkdir((u32)-1, "Documents");
    FOLD_Images = afs_mkdir((u32)-1, "Images");
    FOLD_Store  = afs_mkdir((u32)-1, "ArfuStore");
    FOLD_System = afs_mkdir((u32)-1, "Système");
    afs_save_table();
    serial_str("AFS formate.\n");
    return 1;
}

int afs_list(u32 dir_node, AfsItem *out, int max) {
    if (!afs_ok) return 0;
    int n = 0;
    /* dossiers d'abord, puis fichiers */
    for (int pass = 2; pass >= 1; pass--)
        for (int i = 0; i < AFS_MAX_ENT && n < max; i++)
            if (T[i].type == pass && T[i].parent == (i32)dir_node) {
                st_cp(out[n].name, T[i].name, 40);
                out[n].size = T[i].size;
                out[n].type = T[i].type;
                out[n].node = (u32)i;
                n++;
            }
    return n;
}

int afs_find(u32 dir_node, const char *name, AfsItem *out) {
    if (!afs_ok) return 0;
    for (int i = 0; i < AFS_MAX_ENT; i++)
        if (T[i].type && T[i].parent == (i32)dir_node && st_eq(T[i].name, name)) {
            st_cp(out->name, T[i].name, 40);
            out->size = T[i].size;
            out->type = T[i].type;
            out->node = (u32)i;
            return 1;
        }
    return 0;
}

int afs_mkdir(u32 dir_node, const char *name) {
    if (!afs_ok) return -1;
    int s = afs_free_slot();
    if (s < 0) return -1;
    st_cp(T[s].name, name, 40);
    T[s].parent = (i32)dir_node;
    T[s].type = 2;
    T[s].lba = 0; T[s].alloc = 0; T[s].size = 0;
    afs_save_table();
    return s;
}

int afs_write_file(u32 dir_node, const char *name, const u8 *data, u32 size) {
    if (!afs_ok) return 0;
    u32 need = (size + 511) / 512;
    if (need == 0) need = 1;
    AfsItem old;
    int slot;
    if (afs_find(dir_node, name, &old)) {
        slot = (int)old.node;
        /* réécriture au même endroit si ça tient, sinon à la fin */
        if (T[slot].alloc < need || size == 0) {
            T[slot].lba = afs_top;
            T[slot].alloc = (need > 0 ? need : 1);
            afs_top += T[slot].alloc;
        }
    } else {
        slot = afs_free_slot();
        if (slot < 0) return 0;
        st_cp(T[slot].name, name, 40);
        T[slot].parent = (i32)dir_node;
        T[slot].type = 1;
        T[slot].lba = afs_top;
        T[slot].alloc = need;
        afs_top += need;
    }
    T[slot].size = size;
    /* écrit les secteurs */
    u8 z[512];
    for (int i = 0; i < 512; i++) z[i] = 0;
    for (u32 s = 0; s < T[slot].alloc; s++) {
        const u8 *src;
        if ((u64)(s + 1) * 512 <= size) src = data + (u64)s * 512;
        else {
            u32 rem = size - s * 512;
            int i;
            for (i = 0; i < (int)rem && i < 512; i++) z[i] = data[(u64)s * 512 + i];
            for (; i < 512; i++) z[i] = 0;
            src = z;
        }
        if (!ata_write_sector(afs_disk, T[slot].lba + s, src)) return 0;
    }
    ata_flush(afs_disk);
    afs_save_table();
    return 1;
}

int afs_read_file(const AfsItem *f, u8 *dst, u32 max) {
    if (!afs_ok || f->type != 1) return -1;
    u32 toread = f->size < max ? f->size : max;
    u32 full = toread / 512;
    for (u32 s = 0; s < full; s++) {
        if (!ata_read_sector(afs_disk, f->node < AFS_MAX_ENT ? T[f->node].lba + s : 0, dst + (u64)s * 512))
            return -1;
    }
    if (toread % 512) {
        u8 sec[512];
        if (!ata_read_sector(afs_disk, T[f->node].lba + full, sec)) return -1;
        for (u32 i = 0; i < toread % 512; i++) dst[(u64)full * 512 + i] = sec[i];
    }
    return (int)toread;
}

int afs_delete(const AfsItem *f) {
    if (!afs_ok || f->node >= AFS_MAX_ENT) return 0;
    T[f->node].type = 0;
    afs_save_table();
    return 1;
}

int afs_rename(const AfsItem *f, const char *newname) {
    if (!afs_ok || f->node >= AFS_MAX_ENT) return 0;
    st_cp(T[f->node].name, newname, 40);
    afs_save_table();
    return 1;
}

u32 afs_used_pct(void) {
    if (!afs_ok || !disk_ok) return 0;
    u32 total = the_disk.sectors - AFS_DATA_LBA;
    if (!total) return 0;
    u32 used = afs_top - AFS_DATA_LBA;
    u32 pct = (u32)(((u64)used * 100) / total);
    return pct > 100 ? 100 : pct;
}

/* ================= ISO9660 ================= */

static AtaCd the_cd;
static int iso_ok = 0;
static u32 iso_root_lba = 0, iso_root_size = 0;

static u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

int iso_present(void) { return iso_ok; }

void iso_probe(void) {
    iso_ok = 0;
    if (!atapi_find_cd(&the_cd)) return;
    u8 pvd[2048];
    if (!atapi_read(&the_cd, 16, pvd)) return;
    if (pvd[1] != 'C' || pvd[2] != 'D' || pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1') return;
    iso_root_lba  = le32(pvd + 156 + 2);
    iso_root_size = le32(pvd + 156 + 10);
    iso_ok = 1;
    serial_str("ISO9660 trouve : ");
    serial_str(the_cd.model);
    serial_str("\n");
}

int iso_root(u32 *lba, u32 *size) {
    if (!iso_ok) return 0;
    *lba = iso_root_lba; *size = iso_root_size;
    return 1;
}

int iso_read_at(u32 lba, u8 *dst) {
    if (!iso_ok) return 0;
    return atapi_read(&the_cd, lba, dst);
}

/* itère les entrées d'un répertoire ISO9660 (off += len_record) */
static int iso_next(const u8 *blk, u32 blen, u32 *off, u32 *lba, u32 *size,
                    char *name, int maxn, int *is_dir) {
    while (*off < blen) {
        u8 len = blk[*off];
        if (len == 0) { /* padding -> prochain secteur logique */
            *off = (*off + 2047) & ~2047u;
            continue;
        }
        if (*off + len > blen) return 0;
        const u8 *r = blk + *off;
        *off += len;
        *lba = le32(r + 2);
        *size = le32(r + 10);
        *is_dir = r[25] & 2;
        int nl = r[32];
        if (nl == 1 && r[33] <= 1) {           /* . et .. */
            if (name) name[0] = 0;
            return 1;
        }
        if (name) {
            int w = 0;
            for (int i = 0; i < nl && w < maxn - 1; i++) {
                char c = (char)r[33 + i];
                if (c == ';') break;
                if (c >= 'a' && c <= 'z') c -= 32;
                name[w++] = c;
            }
            while (w > 0 && name[w - 1] == '.') w--;
            name[w] = 0;
        }
        return 1;
    }
    return 0;
}

int iso_list(u32 dir_lba, u32 dir_size, IsoItem *out, int max) {
    if (!iso_ok) return 0;
    u32 bytes = ((dir_size + 2047) / 2048) * 2048;
    if (bytes > 64 * 1024) bytes = 64 * 1024;
    static u8 dirbuf[65536 >> 4];           /* 4 Ko : petits répertoires */
    if (bytes > sizeof(dirbuf)) bytes = sizeof(dirbuf);
    int n = 0;
    for (u32 s = 0; s < bytes / 2048; s++) {
        if (!atapi_read(&the_cd, dir_lba + s, dirbuf + (u64)s * 2048)) return n;
    }
    u32 off = 0, lba, size;
    int isdir;
    char name[104];
    while (n < max && iso_next(dirbuf, bytes, &off, &lba, &size, name, 100, &isdir)) {
        if (!name[0]) continue;              /* . .. */
        st_cp(out[n].name, name, 100);
        out[n].size = size;
        out[n].is_dir = (u8)isdir;
        /* on réutilise size/node : lba stocké dans name pad ? non -> table parallèle */
        out[n].pad[0] = (u8)(lba & 0xFF);
        out[n].pad[1] = (u8)((lba >> 8) & 0xFF);
        out[n].pad[2] = (u8)((lba >> 16) & 0xFF);
        n++;
    }
    return n;
}

u32 iso_item_lba(const IsoItem *it) {
    return it->pad[0] | ((u32)it->pad[1] << 8) | ((u32)it->pad[2] << 16);
}

int iso_read_file(u32 file_lba, u32 off, u8 *dst, u32 max) {
    if (!iso_ok) return -1;
    u32 s0 = off / 2048, i0 = off % 2048, done = 0;
    u8 sec[2048];
    while (done < max) {
        if (!atapi_read(&the_cd, file_lba + s0, sec)) return -1;
        u32 chunk = 2048 - i0;
        if (chunk > max - done) chunk = max - done;
        for (u32 i = 0; i < chunk; i++) dst[done + i] = sec[i0 + i];
        done += chunk; s0++; i0 = 0;
    }
    return (int)done;
}

/* ================= Clé USB : FAT32 (lecture) ================= */

static int fat_ok = 0;
static u32 fat_part_lba = 0, fat_fat_lba = 0, fat_data_lba = 0;
static u32 fat_spc = 1, fat_rootcl = 2;
static u8  fat_sec_per_fat_ign = 0;

static u32 fat_next_cluster(u32 cl, u8 *fatsec) {
    u32 fat_off = cl * 4;
    u32 sec = fat_fat_lba + fat_off / 512;
    if (!usb_msc_read(sec, fatsec, 1)) return 0x0FFFFFFF;
    u32 v = le32(fatsec + (fat_off % 512)) & 0x0FFFFFFF;
    return v;
}

static u32 fat_cl_lba(u32 cl) {
    return fat_data_lba + (cl - 2) * fat_spc;
}

int usbvol_present(void) { return fat_ok; }

void usbvol_probe(void) {
    fat_ok = 0;
    if (!usb_msc_available()) return;
    u8 mbr[512];
    if (!usb_msc_read(0, mbr, 1)) return;
    int pi = -1;
    for (int i = 0; i < 4; i++) {
        u8 t = mbr[446 + i * 16 + 4];
        if (t == 0x0B || t == 0x0C || t == 0x0E) { pi = i; break; }
    }
    /* aussi : pas de table -> superfloppy */
    if (pi < 0) {
        if (mbr[0x52] == 'F' && mbr[0x53] == 'A' && mbr[0x54] == 'T') pi = -2;
        else return;
    }
    fat_part_lba = (pi >= 0) ? le32(mbr + 446 + pi * 16 + 8) : 0;
    u8 bpb[512];
    if (!usb_msc_read(fat_part_lba, bpb, 1)) return;
    u16 bps = bpb[11] | (bpb[12] << 8);
    if (bps != 512) return;
    fat_spc = bpb[13];
    u16 rsvd = bpb[14] | (bpb[15] << 8);
    u8 nfat = bpb[16];
    u32 fsz = le32(bpb + 36);              /* FAT32 : secteurs par FAT */
    if (!fsz) return;
    fat_rootcl = le32(bpb + 44);
    fat_fat_lba = fat_part_lba + rsvd;
    fat_data_lba = fat_part_lba + rsvd + (u32)nfat * fsz;
    fat_ok = 1;
    (void)fat_sec_per_fat_ign;
    serial_str("Cle USB FAT32 trouvee.\n");
}

int fat_list(u32 cluster0, FatItem *out, int max) {
    if (!fat_ok) return 0;
    u32 cl = cluster0 ? cluster0 : fat_rootcl;
    u8 fatsec[512];
    int n = 0;
    u8 dir[512];
    while (cl >= 2 && cl < 0x0FFFFFF8 && n < max) {
        for (u32 s = 0; s < fat_spc; s++) {
            if (!usb_msc_read(fat_cl_lba(cl) + s, dir, 1)) return n;
            for (int e = 0; e < 16 && n < max; e++) {
                const u8 *r = dir + e * 32;
                if (r[0] == 0x00) return n;
                if (r[0] == 0xE5 || r[11] == 0x0F) continue;
                if (r[11] & 0x08) continue;      /* label */
                int w = 0;
                for (int i = 0; i < 8 && r[i] != ' '; i++)
                    out[n].name[w++] = (char)r[i];
                if (r[8] != ' ' && !(r[11] & 0x10)) {
                    out[n].name[w++] = '.';
                    for (int i = 8; i < 11 && r[i] != ' '; i++)
                        out[n].name[w++] = (char)r[i];
                }
                out[n].name[w] = 0;
                out[n].size = le32(r + 28);
                out[n].is_dir = (r[11] & 0x10) ? 1 : 0;
                out[n].cluster = ((u32)(r[20] | (r[21] << 8)) << 16) | r[26] | (r[27] << 8);
                n++;
            }
        }
        cl = fat_next_cluster(cl, fatsec);
    }
    return n;
}

int fat_read_file(u32 cluster, u32 off, u8 *dst, u32 max) {
    if (!fat_ok || cluster < 2) return -1;
    u8 fatsec[512], buf[512];
    u32 cl = cluster;
    u32 skip = off / (fat_spc * 512);
    u32 in_off = off % (fat_spc * 512);
    for (u32 i = 0; i < skip; i++) {
        cl = fat_next_cluster(cl, fatsec);
        if (cl >= 0x0FFFFFF8) return -1;
    }
    u32 done = 0;
    while (done < max && cl >= 2 && cl < 0x0FFFFFF8) {
        u32 s = in_off / 512, i0 = in_off % 512;
        if (!usb_msc_read(fat_cl_lba(cl) + s, buf, 1)) break;
        u32 chunk = 512 - i0;
        if (chunk > max - done) chunk = max - done;
        for (u32 i = 0; i < chunk; i++) dst[done + i] = buf[i0 + i];
        done += chunk;
        in_off += chunk;
        if (in_off >= fat_spc * 512) {
            in_off = 0;
            cl = fat_next_cluster(cl, fatsec);
        }
    }
    return (int)done;
}

/* ================= Volumes (vue explorateur) ================= */

int vol_count(void) {
    int n = 1;                              /* disque principal */
    if (iso_ok) n++;
    if (fat_ok) n++;
    return n;
}

int vol_kind(int idx) {
    if (idx == 0) return VOL_AFS;
    idx--;
    if (iso_ok) { if (idx == 0) return VOL_ISO; idx--; }
    if (fat_ok) { if (idx == 0) return VOL_USB; idx--; }
    return VOL_AFS;
}

const char *vol_name(int idx) {
    int k = vol_kind(idx);
    if (k == VOL_AFS) return "Disque Arfuso";
    if (k == VOL_ISO) return "CD-ROM Arfuso";
    return "Cle USB";
}
