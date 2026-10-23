/* Arfuso OS - Pile réseau maison (e1000 + IPv4/ARP/ICMP/UDP/DHCP/DNS/TCP/HTTP) */
#pragma once
#include "sys.h"

extern u8  NET_ABSENT;     /* 1 si aucune carte e1000 */
extern u8  NET_OK;         /* 1 si DHCP obtenu */
extern u8  NET_MAC[6];
extern u8  NET_IP[4], NET_GW[4], NET_DNS[4], NET_MASK[4];

void net_init(void);                    /* détecte la carte + DHCP (ou plan B 10.0.2.x) */
int  net_available(void);               /* carte présente ? */
int  net_ready(void);                   /* adresse IP obtenue ? */

void ip_to_str(const u8 *ip, char *out);
int  net_resolve(const char *host, u8 ip[4]);       /* 0 ok ; DNS ou IP littérale */
int  net_ping(const u8 ip[4], int tries, u32 *rtt_ms);
const char *net_strerror(int e);

typedef struct {
    int  status;    /* code HTTP (200...) ; 0 si inconnu */
    u32  len;       /* octets utiles renvoyés */
    u32  time_ms;   /* durée totale approx. */
} HttpResult;

/* GET HTTP simple (http:// uniquement). Renvoie 0 ok, <0 erreur net_*. */
int http_get(const char *url, u8 *dst, u32 max, HttpResult *r);

/* TCP générique (utilisé par le navigateur) : 0 ok */
int tcp_fetch(const u8 ip[4], u16 port, const u8 *req, u32 reqlen,
              u8 *dst, u32 dstmax, u32 *outlen);
