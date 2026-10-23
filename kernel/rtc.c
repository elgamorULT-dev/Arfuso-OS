/* Arfuso OS - Lecture de l'horloge CMOS (RTC) */
#include "rtc.h"

static const char *JOURS[7] = {
    "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"
};
static const char *MOIS[12] = {
    "janvier", "février", "mars", "avril", "mai", "juin",
    "juillet", "août", "septembre", "octobre", "novembre", "décembre"
};

static u8 cmos(u8 reg) {
    outb(0x70, reg);
    io_wait();
    return inb(0x71);
}

static u8 bcd(u8 v) { return (u8)((v >> 4) * 10 + (v & 0x0F)); }

int rtc_tz = 0;   /* décalage heure locale - UTC, choisi à l'installation */

static int mdays(int y, int m) {
    static const int T[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int n = T[m - 1];
    if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) n = 29;
    return n;
}

void rtc_read(ArfDate *d) {
    /* attendre la fin d'une mise à jour éventuelle */
    for (u32 i = 0; i < 1000000 && (cmos(0x0A) & 0x80); i++) {}
    int binary = cmos(0x0B) & 0x04;
    u8 s = cmos(0x00), mi = cmos(0x02), h = cmos(0x04);
    u8 dy = cmos(0x07), mo = cmos(0x08), yr = cmos(0x09);
    if (!binary) {
        s = bcd(s); mi = bcd(mi); h = bcd(h);
        dy = bcd(dy); mo = bcd(mo); yr = bcd(yr);
    }
    d->sec = s; d->min = mi; d->hour = h;
    d->day = dy; d->month = mo; d->year = 2000 + yr;
    if (d->month < 1 || d->month > 12) d->month = 1;
    if (d->day < 1 || d->day > 31) d->day = 1;

    /* fuseau horaire choisi à l'installation (avec bascule de date) */
    if (rtc_tz) {
        int hh = (int)d->hour + rtc_tz;
        int dd = d->day, mm = d->month, yy = d->year;
        while (hh < 0) {
            hh += 24; dd--;
            if (dd < 1) { mm--; if (mm < 1) { mm = 12; yy--; } dd = mdays(yy, mm); }
        }
        while (hh >= 24) {
            hh -= 24; dd++;
            if (dd > mdays(yy, mm)) { dd = 1; mm++; if (mm > 12) { mm = 1; yy++; } }
        }
        d->hour = (u8)hh; d->day = (u8)dd; d->month = (u8)mm; d->year = yy;
    }

    /* Jour de la semaine (méthode de Sakamoto) */
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = d->year, m = d->month;
    if (m < 3) y -= 1;
    d->weekday = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d->day) % 7;
    if (d->weekday < 0) d->weekday = 0;
}

static void p2(char *out, int v) {
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
}

void rtc_time_str(const ArfDate *d, char *out) {
    p2(out, d->hour); out[2] = 'h'; p2(out + 3, d->min); out[5] = 0;
}

void rtc_date_num(const ArfDate *d, char *out) {
    p2(out, d->day); out[2] = '/'; p2(out + 3, d->month); out[5] = '/';
    out[6] = '0' + (d->year / 1000) % 10;
    out[7] = '0' + (d->year / 100) % 10;
    out[8] = '0' + (d->year / 10) % 10;
    out[9] = '0' + d->year % 10;
    out[10] = 0;
}

void rtc_date_long(const ArfDate *d, char *out) {
    char num[12];
    const char *j = JOURS[d->weekday];
    while (*j) *out++ = *j++;
    *out++ = ' ';
    if (d->day == 1) {
        *out++ = '1'; *out++ = 'e'; *out++ = 'r';
    } else {
        u32_str((u32)d->day, num);
        char *p = num;
        while (*p) *out++ = *p++;
    }
    *out++ = ' ';
    const char *mm = MOIS[d->month - 1];
    while (*mm) *out++ = *mm++;
    *out++ = ' ';
    u32_str((u32)d->year, num);
    char *p = num;
    while (*p) *out++ = *p++;
    *out = 0;
}

int rtc_second_now(void) {
    ArfDate d;
    rtc_read(&d);
    return d.sec;
}
