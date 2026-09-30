/*
 * sim_rtc_shims.c - host implementation of the RTC/time base
 *
 * Real device: platform/kernel/rtc/src/xy_rtc.c (hardware RTC,
 *       32.768kHz tick, with driver details such as leap seconds /
 *       temperature-compensated calibration); the host cannot compile it
 *       and has no need to emulate it.
 *
 * Host convention: **1 tick = 1 ms**.
 *   - the tick value is taken directly from the host monotonic clock in
 *     milliseconds (GetTickCount64, milliseconds since process start,
 *     never steps backward);
 *   - "RTC wall-clock milliseconds" (xy_get_rtc_ms) = snapshot of the
 *     host wall clock at process start + monotonic delta. On the real
 *     device, before time synchronization the RTC is also a rough wall
 *     clock "starting from power-on", so the semantics are equivalent;
 *     once the wall clock has been set via +CCLK/QNTP/CTZEU, the
 *     application relies on the g_softap_var_nv->wall_time_ms snapshot +
 *     this tick delta (see xy_walltime.c), and the RTC falls back to a
 *     last resort, matching the real device.
 *
 * Provided symbols (all declared in xy_rtc.h):
 *   xy_get_rtc_tick()    real device: xy_rtc.c
 *   xy_get_rtc_ms()      real device: xy_rtc.c
 *   tick_covert_ms()     real device: xy_rtc.c (tick -> ms; host is 1:1 identity)
 *   get_abs_delta_tick() real device: xy_rtc.c (absolute delta of two ticks, wraparound-safe)
 *   get_abs_delta_ms()   real device: xy_rtc.c (absolute delta of two ms values, same implementation)
 *
 * Main user: platform/application/common/src/xy_walltime.c
 * (CCLK/QLTS/QNTP time base and snapshot-driven time).
 */

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#include "rtc_utils.h"

/* Millisecond offset from the FILETIME epoch (1601) to the Unix epoch (1970) */
#define FILETIME_TO_UNIX_MS 11644473600000ULL

static uint64_t sim_boot_wall_ms;   /* host wall clock at process start (Unix epoch ms) */
static uint64_t sim_boot_tick;      /* host monotonic clock at process start (ms) */

static uint64_t host_monotonic_ms(void)
{
    return (uint64_t)GetTickCount64();
}

static uint64_t host_wall_ms(void)
{
    FILETIME ft;
    ULARGE_INTEGER u;

    GetSystemTimeAsFileTime(&ft);
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 10000ULL - FILETIME_TO_UNIX_MS;
}

static void rtc_boot_once(void)
{
    static LONG inited = 0;

    if (InterlockedCompareExchange(&inited, 1, 0) == 0)
    {
        sim_boot_tick    = host_monotonic_ms();
        sim_boot_wall_ms = host_wall_ms();
    }
}

uint64_t xy_get_rtc_tick(void)
{
    rtc_boot_once();
    return host_monotonic_ms();
}

uint64_t xy_get_rtc_ms(void)
{
    rtc_boot_once();
    return sim_boot_wall_ms + (host_monotonic_ms() - sim_boot_tick);
}

uint64_t tick_covert_ms(uint64_t tick)
{
    return tick;    /* host convention: 1 tick = 1 ms */
}

uint64_t get_abs_delta_tick(uint64_t tick1, uint64_t tick2)
{
    return (tick1 >= tick2) ? (tick1 - tick2) : (tick2 - tick1);
}

/* Real device: xy_rtc.c (absolute delta of two ms values; same
 * implementation as get_abs_delta_tick, since the real device also uses
 * one common get_abs_delta body). Used by Phase 2 fs_proxy.c */
uint64_t get_abs_delta_ms(uint64_t ms1, uint64_t ms2)
{
    return (ms1 >= ms2) ? (ms1 - ms2) : (ms2 - ms1);
}

/* ================================================================== */
/* The three calendar-conversion helpers (real device:                 */
/* kernel/rtc/src/rtc_utils.c). The original rtc_utils.c only adds an   */
/* extra include of "xy4101.h" (RISC-V SoC header chain, not buildable */
/* on the host); the algorithm is pure calendar math, mirrored line by  */
/* line here; prototypes verified against the official rtc_utils.h.     */
/* ================================================================== */

static const uint16_t g_daysbeforemonth[13] =
{
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
};

int clock_days_before_month(int month, bool leapyear)
{
    int retval = g_daysbeforemonth[month];

    if (month >= 2 && leapyear)
    {
        retval++;
    }
    return retval;
}

int clock_dayoftheweek(int mday, int month, int year)
{
    if ((month == 1) || (month == 2))
    {
        month += 12;
        year--;
    }

    return (mday + 2 * month + 3 * (month + 1) / 5 + year + year / 4 - year / 100 + year / 400) % 7 + 1;
}

void clock_utc_to_calendar(uint64_t days, int *year, int *month, int *day)
{
    int  value;
    int  min;
    int  max;
    uint64_t  tmp;
    bool leapyear;

    /* There is one leap year every four years, so we can get close with the
     * following:
     */
    value   = days / (4 * 365 + 1);                  /* Number of 4-years periods since the epoch */
    days   -= (uint64_t)value * (4 * 365 + 1);       /* Remaining days */
    value <<= 2;                                      /* Years since the epoch */

    /* Then we will brute force the next 0-3 years */

    for (;;)
    {
        /* Is this year a leap year (we'll need this later too) */
        leapyear = CLOCK_IS_LEAPYEAR(value + 1970);

        /* Get the number of days in the year */
        tmp = (leapyear ? 366 : 365);

        /* Do we have that many days? */
        if (days >= tmp)
        {
            /* Yes.. bump up the year */
            value++;
            days -= tmp;
        }
        else
        {
            /* Nope... then go handle months */
            break;
        }
    }

    /* At this point, value has the year and days has number days into this year */
    *year = 1970 + value;

    /* Handle the month (zero based) */
    min = 0;
    max = 11;

    do {
        /* Get the midpoint */
        value = (min + max) >> 1;

        /* Get the number of days that occurred before the beginning of the month
         * following the midpoint.
         */
        tmp = clock_days_before_month(value + 1, leapyear);

        /* Does the number of days before this month that equal or exceed the
         * number of days we have remaining?
         */
        if (tmp > days)
        {
            /* Yes.. then the month we want is somewhere from 'min' and to the
             * midpoint, 'value'.  Could it be the midpoint?
             */
            tmp = clock_days_before_month(value, leapyear);
            if (tmp > days)
            {
                /* No... The one we want is somewhere between min and value-1 */
                max = value - 1;
            }
            else
            {
                /* Yes.. 'value' contains the month that we want */
                break;
            }
        }
        else
        {
            /* No... The one we want is somwhere between value+1 and max */
            min = value + 1;
        }

        /* If we break out of the loop because min == max, then we want value
         * to be equal to min == max.
         */
        value = min;
    } while (min < max);

    /* The selected month number is in value. Subtract the number of days in the
     * selected month
     */
    days -= clock_days_before_month(value, leapyear);

    /* At this point, value has the month into this year (zero based) and days has
     * number of days into this month (zero based)
     */
    *month = value + 1;   /* 1-based */
    *day   = days + 1;    /* 1-based */
}