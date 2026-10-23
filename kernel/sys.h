/* Arfuso OS - Utilitaires bas niveau (ports E/S, mémoire, série) */
#pragma once
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  i32;

/* ---- Ports d'E/S x86 ---- */
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline u16 inw(u16 port) {
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(u16 port, u16 v) {
    __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(port));
}
static inline u32 inl(u16 port) {
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl(u16 port, u32 v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline void insw(u16 port, void *buf, u32 count) {
    __asm__ volatile("rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
}
static inline void outsw(u16 port, const void *buf, u32 count) {
    __asm__ volatile("rep outsw" : "+S"(buf), "+c"(count) : "d"(port) : "memory");
}
static inline void io_wait(void) { outb(0x80, 0); }

/* ---- Mémoire ---- */
static inline void *memcpy(void *dst, const void *src, u64 n) {
    u8 *d = dst; const u8 *s = src;
    for (u64 i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
static inline void *memset(void *dst, u8 v, u64 n) {
    u8 *d = dst;
    for (u64 i = 0; i < n; i++) d[i] = v;
    return dst;
}
static inline u64 strlen_(const char *s) {
    u64 n = 0; while (s[n]) n++; return n;
}

/* ---- Port série COM1 (debug dans QEMU) ---- */
static inline void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
}
static inline void serial_putc(char c) {
    while (!(inb(0x3F8 + 5) & 0x20)) {}
    outb(0x3F8, (u8)c);
}
static inline void serial_str(const char *s) {
    while (*s) serial_putc(*s++);
}
static inline void serial_hex(u64 v) {
    char buf[17];
    for (int i = 15; i >= 0; i--) {
        buf[15 - i] = "0123456789ABCDEF"[(v >> (i * 4)) & 0xF];
    }
    buf[16] = 0;
    serial_str(buf);
}

/* ---- Divers ---- */
static inline void delay(volatile u64 loops) {
    while (loops--) __asm__ volatile("nop");
}
static inline void cpu_off(void) {
    for (;;) { __asm__ volatile("cli; hlt"); }
}

/* Conversion entier -> chaîne */
static inline void u32_str(u32 v, char *out) {
    char tmp[11];
    int i = 0;
    if (!v) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    int j = 0;
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}
