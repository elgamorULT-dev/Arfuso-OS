/* Arfuso OS - Horloge temps réel (CMOS RTC) avec noms français */
#pragma once
#include "sys.h"

typedef struct {
    int sec, min, hour;
    int day, month, year;       /* ex: 21 / 10 / 2026 */
    int weekday;                /* 0 = dimanche */
} ArfDate;

extern int rtc_tz;   /* fuseau horaire (heures), choisi à l'installation */
void rtc_read(ArfDate *d);
void rtc_time_str(const ArfDate *d, char *out);      /* "21h05" */
void rtc_timeh_str(const ArfDate *d, char *out);     /* idem format court */
void rtc_date_num(const ArfDate *d, char *out);      /* "21/10/2026" */
void rtc_date_long(const ArfDate *d, char *out);     /* "mardi 21 octobre 2026" */
int  rtc_second_now(void);
