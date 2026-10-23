/*
 * Arfuso OS v1.3 - Pile réseau 100 % maison
 * =========================================
 *   e1000 (QEMU/Vectras) -> Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, HTTP.
 * Pas de bibliothèque, pas de socket : uniquement des registres MMIO et des
 * octets posés directement dans les anneaux du contrôleur.
 *
 * Mémoire interne (haute, hors zones de pagination et du noyau) :
 *   0x1500000 : 16 descripteurs TX   0x1500400 : 32 descripteurs RX
 *   0x1500800 : tampons RX 32x2048   0x1510800 : tampons TX 16x2048
 */
#include "net.h"
#include "sys.h"
#include "rtc.h"

typedef unsigned char uc;

u8 NET_ABSENT = 1, NET_OK = 0;
u8 NET_MAC[6] = {0};
u8 NET_IP[4] = {0}, NET_GW[4] = {0}, NET_DNS[4] = {0}, NET_MASK[4] = {255,255,255,0};

/* ---------- petites utilitaires ---------- */
static void s_copy(char *d, const char *s, int max) {
    int i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}
static int s_eq_m(const char *a, const char *pref) {
    while (*pref) { if (*a++ != *pref++) return 0; }
    return 1;
}
static int s_eq_ci(const char *a, const char *b) {
    while (*b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}

static void zcpy(void *d, const void *s, int n) { uc *a=(uc*)d,*b=(uc*)s; for(int i=0;i<n;i++)a[i]=b[i]; }
static void zzero(void *d, int n) { uc *a=(uc*)d; for(int i=0;i<n;i++)a[i]=0; }
static int  zeq(const void *a, const void *b, int n) {
    const uc *x=(const uc*)a,*y=(const uc*)b;
    for(int i=0;i<n;i++) if(x[i]!=y[i]) return 0;
    return 1;
}
static u16 sw16(u16 v){ return (u16)((v>>8)|(v<<8)); }
static u32 sw32(u32 v){ return sw16((u16)(v>>16)) | ((u32)sw16((u16)v)<<16); }
static u32 rnd_seed;
static u32 zrand(void){ rnd_seed = rnd_seed*1664525u + 1013904223u; return rnd_seed; }

/* ---------- PCI ---------- */
static u32 pci_read32(u8 bus, u8 dev, u8 fn, u8 reg) {
    u32 a = 0x80000000u | ((u32)bus<<16) | ((u32)dev<<11) | ((u32)fn<<8) | (reg & 0xFC);
    outl(0xCF8, a);
    return inl(0xCFC);
}
static void pci_write32(u8 bus, u8 dev, u8 fn, u8 reg, u32 v) {
    u32 a = 0x80000000u | ((u32)bus<<16) | ((u32)dev<<11) | ((u32)fn<<8) | (reg & 0xFC);
    outl(0xCF8, a);
    outl(0xCFC, v);
}

/* ---------- e1000 ---------- */
#define TXD 16
#define RXD 32
static volatile u32 mmio_b;
static int  ebus_ = -1, edev_, efn_;

static inline void ew(u32 reg, u32 v){ *(volatile u32 *)(mmio_b + reg) = v; }
static inline u32  er(u32 reg){ return *(volatile u32 *)(mmio_b + reg); }

/* registres e1000 */
#define R_CTRL  0x0000
#define R_RAL   0x5400
#define R_RAH   0x5404
#define R_ICR   0x00C0
#define R_IMS   0x00D0
#define R_RCTL  0x0100
#define R_TCTL  0x0400
#define R_TIPG  0x0410
#define R_RDBAL 0x2800
#define R_RDBAH 0x2804
#define R_RDLEN 0x2808
#define R_RDH   0x2810
#define R_RDT   0x2818
#define R_TDBAL 0x3800
#define R_TDBAH 0x3804
#define R_TDLEN 0x3808
#define R_TDH   0x3810
#define R_TDT   0x3818
#define R_MTA   0x5200
#define R_STATUS 0x0008

#define TXD_ADDR 0x1500000
#define RXD_ADDR 0x1500400
#define RXB_ADDR 0x1500800
#define TXB_ADDR 0x1510800

#define CARD_MEM ((volatile uc *)TXD_ADDR)

/* structure descripteur legacy (16 octets) */
typedef struct { u32 lo, hi; u16 len; u8 cso; u8 cmd; u8 st; u8 err; u8 css; u8 spec0; } EDesc;

static volatile EDesc *TXDS = (volatile EDesc *)TXD_ADDR;
static volatile EDesc *RXDS = (volatile EDesc *)RXD_ADDR;
static int tx_i = 0, rx_i = 0;

static int e1000_find(void) {
    for (int b = 0; b < 256; b++)
        for (int dev = 0; dev < 32; dev++) {
            u32 id = pci_read32((u8)b, (u8)dev, 0, 0);
            if ((id & 0xFFFF) != 0x8086) continue;
            u16 did = (u16)(id >> 16);
            if (did == 0x100E || did == 0x100F || did == 0x1026 ||
                did == 0x1533 || did == 0x1539 || did == 0x10EA || did == 0x109A) {
                ebus_ = b; edev_ = dev; efn_ = 0;
                return 1;
            }
        }
    return 0;
}

static void e1000_init(void) {
    u32 cmd = pci_read32((u8)ebus_, (u8)edev_, 0, 4);
    pci_write32((u8)ebus_, (u8)edev_, 0, 4, cmd | 0x7);           /* IO+MEM+BUS MASTER */
    u32 bar0 = pci_read32((u8)ebus_, (u8)edev_, 0, 0x10);
    mmio_b = bar0 & 0xFFFFFFF0u;
    /* reset matériel */
    ew(R_CTRL, er(R_CTRL) | (1u<<26));
    for (int i = 0; i < 100000; i++) ;
    while (er(0x00) & (1u<<26)) ;                                 /* RST terminé */
    ew(R_CTRL, 0x000205A4);                                       /* SLU|ASDE|FD|RFCE.. */
    ew(R_IMS, 0);                                                 /* pas d'IRQ */
    er(R_ICR);                                                    /* lecture = clear */
    /* MAC */
    u32 ral = er(R_RAL), rah = er(R_RAH);
    NET_MAC[0] = (uc)(ral); NET_MAC[1] = (uc)(ral>>8);
    NET_MAC[2] = (uc)(ral>>16); NET_MAC[3] = (uc)(ral>>24);
    NET_MAC[4] = (uc)(rah); NET_MAC[5] = (uc)(rah>>8);
    /* MTA vide */
    for (int i = 0; i < 128; i++) ew(R_MTA + i*4, 0);
    /* RX ring */
    for (int i = 0; i < RXD; i++) {
        RXDS[i].lo = RXB_ADDR + i * 2048;
        RXDS[i].hi = 0;
        RXDS[i].len = 0; RXDS[i].cso = 0; RXDS[i].st = 0;
    }
    ew(R_RDBAH, 0); ew(R_RDBAL, RXD_ADDR);
    ew(R_RDLEN, RXD * 16);
    ew(R_RDH, 0); ew(R_RDT, RXD - 1);
    ew(R_RCTL, (1u<<1) | (1u<<15) | (1u<<26));  /* EN | BAM | SECRC ; BSIZE=0 -> 2048 o */
    /* TX ring */
    for (int i = 0; i < TXD; i++) {
        TXDS[i].lo = TXB_ADDR + i * 2048;
        TXDS[i].hi = 0; TXDS[i].st = 1; TXDS[i].cmd = 0; TXDS[i].cso = 0;
    }
    ew(R_TDBAH, 0); ew(R_TDBAL, TXD_ADDR);
    ew(R_TDLEN, TXD * 16);
    ew(R_TDH, 0); ew(R_TDT, 0);
    ew(R_RDT, RXD - 1);                                             /* re-kick : purge la file QEMU */
    ew(R_TIPG, 0x6020F0A);
    ew(R_TCTL, (1u<<1) | (1u<<3) | (0x0Fu<<12) | (0x3Fu<<22));   /* EN|PSP|CT|COLD */
    /* attendre le lien */
    for (int i = 0; i < 5000000; i++) if (er(R_STATUS) & 2) break;
    tx_i = 0; rx_i = 0;
    ew(R_RDT, RXD - 1);                                             /* kick final post-RCTL */
}

/* émission brute d'une trame Ethernet */
static int eth_send(const u8 mac_dst[6], u16 type, const u8 *pay, int n) {
    if (n > 2036) n = 2036;
    uc *buf = (uc *)(TXB_ADDR + tx_i * 2048);
    for (int i = 0; i < 6; i++) buf[i] = mac_dst[i];
    for (int i = 0; i < 6; i++) buf[6+i] = NET_MAC[i];
    buf[12] = (uc)(type>>8); buf[13] = (uc)type;
    for (int i = 0; i < n; i++) buf[14+i] = pay[i];
    int tot = 14 + n;
    while (tot < 60) buf[tot++] = 0;                              /* pad mini Ethernet */
    volatile EDesc *dd = &TXDS[tx_i];
    dd->len = (u16)tot;
    dd->cso = 0;
    dd->cmd = 0x0B;                                               /* EOP|IFCS|RS */
    dd->st = 0;
    ew(R_TDT, (u32)((tx_i + 1) % TXD));
    for (int i = 0; i < 500000; i++) if (dd->st & 1) goto ok;
    return -1;
ok: tx_i = (tx_i + 1) % TXD;
    return 0;
}

/* réception brute : renvoie longueur totale ou 0 ; type ethertype */
static int eth_poll(u8 *dst, u32 max, u16 *etype, u8 src[6]) {
    static u32 kick = 0;
    volatile EDesc *dd = &RXDS[rx_i];
    if (!(dd->st & 1)) {
        /* purge periodique de la file d'attente QEMU (can_receive) */
        if ((++kick & 0x3FF) == 0) ew(R_RDT, (u32)((rx_i + RXD - 1) % RXD));
        return 0;
    }
    uc const *buf = (uc const *)(RXB_ADDR + rx_i * 2048);
    int n = dd->len;
    for (int i = 0; i < 6; i++) if (src) src[i] = buf[6+i];
    *etype = (u16)((buf[12]<<8) | buf[13]);
    int pay = n - 14;
    if (pay > (int)max) pay = (int)max;
    for (int i = 0; i < pay; i++) dst[i] = buf[14+i];
    dd->st = 0; dd->cso = 0; dd->cmd = 0;
    ew(R_RDT, (u32)rx_i);
    rx_i = (rx_i + 1) % RXD;
    return pay;
}

/* temps : boucles calibrées grossièrement en "ms" */
/* calibration des delais : nop-loop va beaucoup trop vite sur hote moderne.
   On mesure delay(1024) sur un front de seconde RTC => vraies millisecondes. */
static u64 DPK = 64;
static void net_calibrate(void) {
    int s0 = rtc_second_now();
    while (rtc_second_now() == s0) delay(1024);      /* attendre front */
    int s1 = rtc_second_now();
    u64 c = 0;
    while (rtc_second_now() == s1) { delay(1024); c++; }
    if (!c) c = 1;
    DPK = c;
}
static void ms_wait(int ms) {
    u64 n = ((u64)ms * DPK) / 1000;
    if (!n) n = 1;
    for (u64 i = 0; i < n; i++) delay(1024);
}

/* attente d'une trame de type donné (ms, dst) ; <0 timeout */
static int eth_wait(u16 type, u8 *dst, u32 max, int ms, u8 src[6]) {
    u16 tp; u8 s2[6];
    while (ms > 0) {
        int n = eth_poll(dst, max, &tp, src ? src : s2);
        if (n > 0 && tp == type) return n;
        ms_wait(1); ms--;
    }
    return -1;
}

/* ---------- ARP ---------- */
typedef struct { u8 ip[4]; u8 mac[6]; u8 ok; } ArpEnt;
static ArpEnt ARP_CACHE[8];
static void arp_put(const u8 ip[4], const u8 mac[6]) {
    for (int i = 0; i < 8; i++) if (ARP_CACHE[i].ok && zeq(ARP_CACHE[i].ip, ip, 4)) {
        zcpy(ARP_CACHE[i].mac, mac, 6); return;
    }
    for (int i = 0; i < 8; i++) if (!ARP_CACHE[i].ok) {
        zcpy(ARP_CACHE[i].ip, ip, 4); zcpy(ARP_CACHE[i].mac, mac, 6);
        ARP_CACHE[i].ok = 1; return;
    }
}
static int arp_lookup(const u8 ip[4], u8 mac[6], int ask) {
    for (int i = 0; i < 8; i++)
        if (ARP_CACHE[i].ok && zeq(ARP_CACHE[i].ip, ip, 4)) { zcpy(mac, ARP_CACHE[i].mac, 6); return 0; }
    if (!ask) return -1;
    uc req[28];
    req[0]=0; req[1]=1; req[2]=8; req[3]=0; req[4]=6; req[5]=4; req[6]=0; req[7]=1;
    zcpy(req+8, NET_MAC, 6); zcpy(req+14, NET_IP, 4);
    zzero(req+18, 6); zcpy(req+24, ip, 4);
    uc bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static uc ans[1518];
    for (int t = 0; t < 3; t++) {
        eth_send(bc, 0x0806, req, 28);
        u16 tp; u8 frm[6];
        int guard = 400;
        while (guard-- > 0) {
            int n = eth_poll(ans, 1518, &tp, frm);
            if (n <= 0) { ms_wait(1); continue; }
            if (tp == 0x0800) { /* paquet IP arrivé entre-temps : ignorer ici */ continue; }
            if (tp == 0x0806 && n >= 28 && ans[6]==0 && ans[7]==2 && zeq(ans+14, ip, 4)) {
                zcpy(mac, ans+8, 6); arp_put(ip, mac); return 0;
            }
        }
    }
    return -1;
}

/* répondre aux requêtes ARP qui nous visent (courtoisie réseau) */
static void arp_reply_if_asked(const u8 *pkt, int n, const u8 src_mac[6]) {
    if (n < 28) return;
    if (pkt[6]==0 && pkt[7]==1 && zeq(pkt+24, NET_IP, 4)) {
        uc rep[28];
        zcpy(rep, pkt, 28); rep[7] = 2;
        zcpy(rep+8, NET_MAC, 6); zcpy(rep+14, NET_IP, 4);
        zcpy(rep+18, src_mac, 6); zcpy(rep+24, pkt+14, 4);
        eth_send(src_mac, 0x0806, rep, 28);
    }
}

/* ---------- IPv4 ---------- */
static u16 ip_id = 0x1234;
static u16 cksum(const u8 *p, int n) {
    u32 s = 0;
    while (n > 1) { s += (u16)((p[0]<<8)|p[1]); p += 2; n -= 2; }
    if (n) s += (u16)(p[0]<<8);
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (u16)~s;
}

/* renvoie la MAC du saut suivant (résolution ARP si besoin) ; 0 ok */
static int ip_mac_of(const u8 dst[4], u8 mac[6]) {
    u8 hop[4];
    if (zeq(dst, NET_IP, 4)) { zcpy(hop, dst, 4); }
    else if ((dst[0]&NET_MASK[0])==(NET_IP[0]&NET_MASK[0]) &&
             (dst[1]&NET_MASK[1])==(NET_IP[1]&NET_MASK[1])) zcpy(hop, dst, 4);
    else zcpy(hop, NET_GW, 4);
    return arp_lookup(hop, mac, 1);
}

static int ip_send(const u8 dst[4], u8 proto, const u8 *pay, int n) {
    if (n > 1400) n = 1400;
    uc hdr[20];
    hdr[0]=0x45; hdr[1]=0; hdr[2]=(uc)((n+20)>>8); hdr[3]=(uc)(n+20);
    hdr[4]=(uc)(ip_id>>8); hdr[5]=(uc)ip_id; ip_id++;
    hdr[6]=0x40; hdr[7]=0; hdr[8]=64; hdr[9]=proto;
    hdr[10]=0; hdr[11]=0;
    zcpy(hdr+12, NET_IP, 4); zcpy(hdr+16, dst, 4);
    u16 c = cksum(hdr, 20); hdr[10]=(uc)(c>>8); hdr[11]=(uc)c;
    uc mac[6];
    if (ip_mac_of(dst, mac) != 0) return -1;
    uc frame[1430];
    zcpy(frame, hdr, 20); zcpy(frame+20, pay, n);
    return eth_send(mac, 0x0800, frame, n + 20);
}

/* attend un paquet IPv4 ; extrait ip source, proto ; renvoie longueur payload */
static int ip_wait(u8 src[4], u8 *proto, u8 *dst, u32 max, int ms) {
    static uc buf[1518]; u16 tp; u8 mac_f[6];
    while (ms > 0) {
        int n = eth_poll(buf, 1518, &tp, mac_f);
        if (n <= 0) { ms_wait(1); ms--; continue; }
        if (tp == 0x0806) { arp_reply_if_asked(buf, n, mac_f); continue; }
        if (tp != 0x0800 || n < 20) continue;
        int ihl = (buf[0] & 0x0F) * 4;
        if (!zeq(buf+16, NET_IP, 4) && !(buf[16]==0xFF&&buf[17]==0xFF)) continue;
        zcpy(src, buf + 12, 4);
        *proto = buf[9];
        int tot = (buf[2]<<8) | buf[3];
        int pay = tot - ihl;
        if (pay > (int)max) pay = (int)max;
        zcpy(dst, buf + ihl, pay);
        return pay;
    }
    return -1;
}

/* ---------- ICMP (ping) ---------- */
int net_ping(const u8 ip[4], int tries, u32 *rtt_ms) {
    (void)tries;
    uc pay[40];
    pay[0]=8; pay[1]=0; pay[2]=0; pay[3]=0;
    pay[4]=(uc)(ip_id); pay[5]=0; pay[6]=0; pay[7]=1;
    for (int i = 8; i < 40; i++) pay[i] = (uc)(i*7);
    u16 c = cksum(pay, 40); pay[2]=(uc)(c>>8); pay[3]=(uc)c;
    if (ip_send(ip, 1, pay, 40) != 0) return -1;
    static uc ipp[700]; u8 src[4], pr; u32 tt = 0;
    for (;;) {
        int n = ip_wait(src, &pr, ipp, 700, 200);
        if (n < 0) return -1;
        tt += 3;
        if (pr == 1 && n >= 8 && ipp[0] == 0 && zeq(src, ip, 4)) {
            *rtt_ms = tt * 10;   /* approx. */
            return 0;
        }
    }
}

/* ---------- UDP ---------- */
static u16 udp_sport = 40000;
static int udp_send(const u8 dst[4], u16 dport, const u8 *pay, int n) {
    uc hdr[8];
    udp_sport++;
    if (udp_sport > 60000) udp_sport = 40000;
    hdr[0]=(uc)((udp_sport)>>8); hdr[1]=(uc)udp_sport;
    hdr[2]=(uc)(dport>>8); hdr[3]=(uc)dport;
    int tot = n + 8;
    hdr[4]=(uc)(tot>>8); hdr[5]=(uc)tot;
    hdr[6]=0; hdr[7]=0;
    uc frame[1100];
    zcpy(frame, hdr, 8); zcpy(frame+8, pay, n);
    return ip_send(dst, 17, frame, tot);
}

/* ---------- DHCP ---------- */
static int dhcp_run(void) {
    static uc pkt[590];
    zzero(pkt, 590);
    pkt[0]=1; pkt[1]=1; pkt[2]=6; pkt[3]=0;
    u32 xid = zrand();
    pkt[4]=(uc)(xid>>24); pkt[5]=(uc)(xid>>16); pkt[6]=(uc)(xid>>8); pkt[7]=(uc)xid;
    pkt[10]=0x80;
    zcpy(pkt+28, NET_MAC, 6);
    pkt[240]=0x63; pkt[241]=0x82; pkt[242]=0x53; pkt[243]=0x63;
    int o = 244;
    pkt[o++]=53; pkt[o++]=1; pkt[o++]=1;                /* DHCPDISCOVER */
    pkt[o++]=55; pkt[o++]=3; pkt[o++]=1; pkt[o++]=3; pkt[o++]=6;
    pkt[o++]=255;
    uc bc4[4] = {255,255,255,255};
    static uc ipp[700]; u8 src4[4], pr;
    for (int round = 0; round < 6; round++) {
        /* sport 68 -> dport 67 ; le broadcast passe par MAC ff:ff */
        uc bc6[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        {
            uc hdr[8+20]; /* en-tête IP+UDP "broadcast" direct sans ARP */
            zzero(hdr, 28);
            hdr[0]=0x45; hdr[2]=0; hdr[3]=0; /* rempli plus bas */
            int tot = 20 + 8 + 300;
            hdr[2]=(uc)(tot>>8); hdr[3]=(uc)tot;
            hdr[4]=(uc)(ip_id>>8); hdr[5]=(uc)ip_id; ip_id++;
            hdr[8]=64; hdr[9]=17;
            /* src 0.0.0.0 */
            zcpy(hdr+16, bc4, 4);
            u16 c = cksum(hdr, 20); hdr[10]=(uc)(c>>8); hdr[11]=(uc)c;
            hdr[20]=(uc)(68>>8); hdr[21]=68;
            hdr[22]=(uc)(67>>8); hdr[23]=67;
            hdr[24]=(uc)((8+300)>>8); hdr[25]=(uc)(8+300);
            uc frame[340];
            zcpy(frame, hdr, 28);
            int cp = 300 < 340-28 ? 300 : 340-28;
            zcpy(frame+28, pkt, cp);
            eth_send(bc6, 0x0800, frame, 28 + cp);
        }
        u32 to = 400;
        while (to--) {
            int n = ip_wait(src4, &pr, ipp, 700, 6);
            if (n < 0) { to = 0; break; }
            if (pr != 17 || n < 244 + 8) continue;
            uc *bp = ipp;
            int uhl = 8;
            if (n < uhl + 243) continue;
            uc *opt = bp + uhl;
            if (opt[236]!=0x63) continue;
            if (zeq(opt+4, pkt+4, 4) == 0) continue;       /* même transaction ? */
            /* yiaddr : offset +8-8+16 => opt+16 */
            int msgt = 0; int p = uhl + 240;
            u8 yi[4]; zcpy(yi, bp + uhl + 16, 4);
            u8 gw[4]={0}, dns[4]={0}, mask[4]={255,255,255,0};
            while (p < n - 2) {
                if (opt[p-uhl+0] == 255 || opt[p-uhl+0]==255) break;
                int code = bp[p], len = bp[p+1];
                if (code == 255 || p + 2 + len > n) break;
                if (code == 53) msgt = bp[p+2];
                if (code == 3 && len >= 4) zcpy(gw, bp+p+2, 4);
                if (code == 6 && len >= 4) zcpy(dns, bp+p+2, 4);
                if (code == 1 && len >= 4) zcpy(mask, bp+p+2, 4);
                p += 2 + len;
            }
            if (msgt == 2) {
                /* REQUEST */
                pkt[12]=yi[0]; pkt[13]=yi[1]; pkt[14]=yi[2]; pkt[15]=yi[3];
                int o2 = 244;
                pkt[o2++]=53; pkt[o2++]=1; pkt[o2++]=3;
                pkt[o2++]=54; pkt[o2++]=4; zcpy(pkt+o2, src4, 4); o2+=4;
                pkt[o2++]=50; pkt[o2++]=4; zcpy(pkt+o2, yi, 4); o2+=4;
                pkt[o2++]=255;
                continue;
            }
            if (msgt == 5) {
                zcpy(NET_IP, yi, 4);
                if (gw[0]) zcpy(NET_GW, gw, 4);
                if (dns[0]) zcpy(NET_DNS, dns, 4);
                zcpy(NET_MASK, mask, 4);
                NET_OK = 1;
                return 0;
            }
        }
    }
    return -1;
}

/* ---------- DNS (A records) ---------- */
int net_resolve(const char *host, u8 ip[4]) {
    /* IP littérale ? */
    int dots = 0, j = 0;
    u8 a[4] = {0}; int cur = 0, dig = 0;
    while (host[j]) {
        char c = host[j];
        if (c >= '0' && c <= '9') { cur = cur*10 + c - '0'; dig = 1; }
        else if (c == '.') { if (dots>2) { dots=-9; break; } a[dots++] = (uc)cur; cur = 0; }
        else { dots = -9; break; }
        j++;
    }
    if (dig && dots == 3 && cur <= 255) { a[3]=(uc)cur; zcpy(ip, a, 4); return 0; }
    if (!NET_OK) return -4;
    uc q[300];
    int i = 0;
    u16 id = (u16)(zrand() & 0xFFFF);
    q[i++]=(uc)(id>>8); q[i++]=(uc)id;
    q[i++]=0x01; q[i++]=0x00; q[i++]=0; q[i++]=1;      /* RD, QDCOUNT=1 */
    q[i++]=0; q[i++]=0; q[i++]=0; q[i++]=0; q[i++]=0; q[i++]=0;
    j = 0;
    while (host[j]) {
        int len = 0;
        while (host[j+len] && host[j+len] != '.') len++;
        q[i++] = (uc)len;
        for (int k = 0; k < len; k++) q[i++] = (uc)host[j+k];
        j += len;
        if (host[j] == '.') j++;
    }
    q[i++] = 0;
    q[i++] = 0; q[i++] = 1;   /* A */
    q[i++] = 0; q[i++] = 1;   /* IN */
    udp_send(NET_DNS[0] ? NET_DNS : NET_GW, 53, q, i);
    static uc ipp[700]; u8 src[4], pr;
    u32 to = 8;
    while (to--) {
        int n = ip_wait(src, &pr, ipp, 700, 400);
        if (n < 0) { return -2; }
        if (pr != 17 || n < 12 + 8) { continue; }
        uc *bp = ipp + 8;
        if (!(bp[2] & 0x80)) continue;   /* bit QR de la réponse */
        int qd = (bp[4]<<8) | bp[5];
        int an = (bp[6]<<8) | bp[7];
        int p = 12;
        for (int k = 0; k < qd; k++) {
            while (p < n && bp[p]) p += 1 + bp[p];
            p += 5;
        }
        for (int k = 0; k < an && p + 12 < n; k++) {
            if ((bp[p] & 0xC0) == 0xC0) p += 2;
            else { while (p < n && bp[p]) p += 1 + bp[p]; p++; }
            int type = (bp[p]<<8) | bp[p+1];
            int rdlen = (bp[p+8]<<8) | bp[p+9];
            if (type == 1 && rdlen == 4) { zcpy(ip, bp + p + 10, 4); return 0; }
            p += 10 + rdlen;
        }
        return -2;
    }
    return -2;
}

/* ---------- TCP (client simple, en ordre) ---------- */
#define TC_CLOSED 0
#define TC_SYNSENT 1
#define TC_ESTAB 2
#define TC_FIN 3

typedef struct {
    int state;
    u8 ip[4]; u16 port, sport;
    u32 snd_una, snd_nxt, rcv_nxt;
} Tcb;

static int tcp_seg(Tcb *c, u8 flags, const u8 *data, int n) {
    uc hdr[32];
    hdr[0]=(uc)(c->sport>>8); hdr[1]=(uc)c->sport;
    hdr[2]=(uc)(c->port>>8); hdr[3]=(uc)c->port;
    hdr[4]=(uc)(c->snd_nxt>>24); hdr[5]=(uc)(c->snd_nxt>>16);
    hdr[6]=(uc)(c->snd_nxt>>8); hdr[7]=(uc)(c->snd_nxt);
    hdr[8]=(uc)(c->rcv_nxt>>24); hdr[9]=(uc)(c->rcv_nxt>>16);
    hdr[10]=(uc)(c->rcv_nxt>>8); hdr[11]=(uc)(c->rcv_nxt);
    hdr[12]=0x50; hdr[13]=flags;
    hdr[14]=0x16; hdr[15]=0xD0;
    hdr[16]=0; hdr[17]=0; hdr[18]=0; hdr[19]=0;
    uc frame[1500];
    zcpy(frame, hdr, 20);
    if (n > 0) zcpy(frame+20, data, n);
    /* pseudo-en-tête */
    uc ph[12];
    zcpy(ph, NET_IP, 4); zcpy(ph+4, c->ip, 4);
    ph[8]=0; ph[9]=6; ph[10]=(uc)((20+n)>>8); ph[11]=(uc)(20+n);
    u32 s = 0;
    for (int i = 0; i < 12; i += 2) s += (u16)((ph[i]<<8)|ph[i+1]);
    for (int i = 0; i < 20+n; i += 2) {
        u16 w = (u16)(frame[i]<<8);
        if (i+1 < 20+n) w |= frame[i+1];
        s += w;
    }
    while (s>>16) s = (s&0xFFFF)+(s>>16);
    u16 cks = (u16)~s;
    frame[16]=(uc)(cks>>8); frame[17]=(uc)cks;
    int r = ip_send(c->ip, 6, frame, 20 + n);
    if ((flags & 0x02) || n > 0) c->snd_nxt += n + ((flags&0x02)?1:0) + ((flags&0x01)?1:0);
    return r;
}

/* renvoie: >0 octets reçus copiés, 0 = fin/timeout, -1 = réinitialisé */
static Tcb TCB;

int tcp_fetch(const u8 ip[4], u16 port, const u8 *req, u32 reqlen,
              u8 *dst, u32 dstmax, u32 *outlen) {
    Tcb *c = &TCB;
    zzero(c, sizeof(Tcb));
    zcpy(c->ip, ip, 4);
    c->port = port;
    c->sport = (u16)(30000 + (udp_sport % 9000));
    c->snd_nxt = 1000 + (zrand() & 0xFFFF);
    c->snd_una = c->snd_nxt;
    u32 len = 0;
    int rto = 0;
    /* SYN */
    tcp_seg(c, 0x02, 0, 0);
    c->state = TC_SYNSENT;
    u32 tout = 40;
    while (tout--) {
        static uc ipp[800]; u8 src[4], pr;
        int n = ip_wait(src, &pr, ipp, 800, 400);
        if (n < 0) { rto++; if (rto > 5) return -3; continue; }
        if (pr != 6 || n < 20 || !zeq(src, ip, 4)) continue;
        u8 fl = ipp[13];
        u32 sq = sw32(*(u32*)(ipp+4));
        if (fl & 0x04) return -3;
        if (fl & 0x12) {  /* SYN+ACK */
            c->rcv_nxt = sq + 1;
            tcp_seg(c, 0x10, 0, 0);
            c->state = TC_ESTAB;
            /* envoi de la requête */
            tcp_seg(c, 0x18, req, (int)reqlen);
            /* réception */
            int idle = 0;
            while (len < dstmax - 1 && idle < 30) {
                n = ip_wait(src, &pr, ipp, 800, 400);
                if (n < 0) { idle++; continue; }
                if (pr != 6 || n < 20) continue;
                if (!zeq(src, ip, 4)) continue;
                idle = 0;
                u8 fl2 = ipp[13];
                int hl = ((ipp[12]>>4)&0xF)*4;
                int dlen = n - hl;
                u32 sq2 = sw32(*(u32*)(ipp+4));
                if (fl2 & 0x04) return (int)len ? 0 : -3;
                if (dlen > 0) {
                    if (sq2 == c->rcv_nxt) {
                        int cp = dlen;
                        if (len + cp > dstmax - 1) cp = dstmax - 1 - len;
                        zcpy(dst + len, ipp + hl, cp);
                        len += cp;
                        c->rcv_nxt += dlen;
                    }
                    tcp_seg(c, 0x10, 0, 0);
                }
                if (fl2 & 0x01) {
                    c->rcv_nxt++;
                    tcp_seg(c, 0x10, 0, 0);
                    *outlen = len;
                    return 0;
                }
            }
            *outlen = len;
            return len ? 0 : -1;
        }
    }
    return -1;
}

/* ---------- HTTP ---------- */
void ip_to_str(const u8 *ip, char *out) {
    char tmp[4]; int p = 0;
    for (int i = 0; i < 4; i++) {
        u32_str(ip[i], tmp);
        int q = 0;
        while (tmp[q]) out[p++] = tmp[q++];
        if (i < 3) out[p++] = '.';
    }
    out[p] = 0;
}

const char *net_strerror(int e) {
    switch (e) {
        case -1: return "délai dépassé";
        case -2: return "DNS : domaine introuvable";
        case -3: return "connexion refusée (ou réinitialisée)";
        case -4: return "pas de réseau (DHCP)";
        case -5: return "URL invalide (http:// uniquement)";
        default: return "erreur réseau";
    }
}

static int url_parts(const char *url, char *host, u16 *port, char *path) {
    const char *p = url;
    if (p[0]=='h'&&p[1]=='t'&&p[2]=='t'&&p[3]=='p'&&p[4]==':') p += 5;
    else if (s_eq_m(url, "https://")) return -5;
    while (*p == '/') p++;
    int i = 0;
    while (host && *p && *p != '/' && *p != ':' && i < 62) host[i++] = *p++;
    if (host) host[i] = 0;
    *port = 80;
    if (*p == ':') {
        p++;
        u16 v = 0;
        while (*p >= '0' && *p <= '9') v = (u16)(v*10 + (*p++ - '0'));
        if (v) *port = v;
    }
    if (*p == '/') { i = 0; while (*p && i < 110) path[i++] = *p++; path[i] = 0; }
    else s_copy(path, "/", 120);
    return 0;
}

int http_get(const char *url, u8 *dst, u32 max, HttpResult *r) {
    char host[64], path[120];
    u16 port = 80;
    u32 t0 = 0; (void)t0;
    if (url_parts(url, host, &port, path) != 0) return -5;
    u8 ip[4];
    int rr = net_resolve(host, ip);
    if (rr != 0) return rr;
    static uc treq[400];
    int n = 0;
    const char *g1 = "GET "; while (*g1) treq[n++] = (uc)*g1++;
    const char *g2 = path; while (*g2) treq[n++] = (uc)*g2++;
    const char *g3 = " HTTP/1.0\r\nHost: "; while (*g3) treq[n++] = (uc)*g3++;
    const char *g4 = host; while (*g4) treq[n++] = (uc)*g4++;
    const char *g5 = "\r\nUser-Agent: Chrome-Arfuso/1.3 (Arfuso OS)\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    while (*g5) treq[n++] = (uc)*g5++;
    static uc resp[65536+24000];
    u32 got = 0;
    int st = tcp_fetch(ip, port, treq, (u32)n, resp, 65000, &got);
    if (st != 0) return st;
    /* statut HTTP */
    r->status = 0;
    const uc *hp = resp;
    int i = 0;
    while (i < (int)got && hp[i] != ' ') i++;
    while (i < (int)got && hp[i] == ' ') i++;
    int v = 0;
    while (i < (int)got && hp[i] >= '0' && hp[i] <= '9') v = v*10 + (hp[i++] - '0');
    r->status = v;
    r->time_ms = 0;
    /* fin des en-têtes */
    int body = -1;
    for (u32 k = 0; k + 3 < got; k++)
        if (resp[k]=='\r' && resp[k+1]=='\n' && resp[k+2]=='\r' && resp[k+3]=='\n') {
            body = (int)k + 4; break;
        }
    if (body < 0) body = 0;
    /* chunked ? */
    int chunked = 0;
    for (u32 k = 0; k < (u32)body - 12; k++)
        if ((resp[k]=='t'||resp[k]=='T') && s_eq_ci((const char*)resp+k, "transfer-encoding:")) {
            for (u32 m = k; m < (u32)body - 7; m++)
                if ((resp[m]=='c'||resp[m]=='C') && s_eq_ci((const char*)resp+m, "chunked")) chunked = 1;
        }
    u32 w = 0;
    if (chunked) {
        u32 p = (u32)body;
        while (p < got) {
            u32 sz = 0;
            while (p < got && resp[p] != '\r') {
                uc c = resp[p++];
                int hv = (c>='0'&&c<='9') ? c-'0' : (c>='a'&&c<='f') ? c-'a'+10 : (c>='A'&&c<='F') ? c-'A'+10 : -1;
                if (hv < 0) continue;
                sz = sz*16 + (u32)hv;
            }
            p += 2;
            if (sz == 0) break;
            if (w + sz > max - 1) sz = max - 1 - w;
            zcpy(dst + w, resp + p, (int)sz);
            w += sz; p += sz + 2;
        }
    } else {
        u32 bl = got - (u32)body;
        if (bl > max - 1) bl = max - 1;
        zcpy(dst, resp + body, (int)bl);
        w = bl;
    }
    dst[w] = 0;
    r->len = w;
    if (r->status != 200 && r->status != 301 && r->status != 302) return -1;
    return 0;
}

/* ---------- init ---------- */
int net_available(void) { return !NET_ABSENT; }
int net_ready(void) { return NET_OK; }

void net_init(void) {
    net_calibrate();
    rnd_seed = 2654435761u + (u32)rtc_second_now();
    NET_ABSENT = 1; NET_OK = 0;
    if (!e1000_find()) return;
    e1000_init();
    NET_ABSENT = 0;
    /* DHCP puis plan B (pile utilisateur QEMU slirp : 10.0.2.x) */
    zzero(NET_IP, 4);
    if (dhcp_run() != 0) {
        NET_IP[0]=10; NET_IP[1]=0; NET_IP[2]=2; NET_IP[3]=15;
        NET_GW[0]=10; NET_GW[1]=0; NET_GW[2]=2; NET_GW[3]=2;
        NET_DNS[0]=10; NET_DNS[1]=0; NET_DNS[2]=2; NET_DNS[3]=3;
        NET_MASK[0]=255; NET_MASK[1]=255; NET_MASK[2]=255; NET_MASK[3]=0;
        NET_OK = 1;
    }
    if (NET_OK && (NET_GW[0] == 0 && NET_GW[1] == 0)) {
        NET_GW[0]=10; NET_GW[1]=0; NET_GW[2]=2; NET_GW[3]=2;
    }
    if (!NET_DNS[0]) { NET_DNS[0]=10; NET_DNS[1]=0; NET_DNS[2]=2; NET_DNS[3]=3; }
}
