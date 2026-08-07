/* nl_time.c - Nairobi Life: calendar, clock and solar geometry.
 *
 * Nairobi sits at latitude -1.286389 deg (1 deg 17' South), longitude
 * 36.817223 deg E, in East Africa Time (UTC+3, no daylight saving).
 * Because the city is barely 1.3 degrees off the equator, day length is
 * almost constant: sunrise falls between roughly 06:19 and 06:38 and sunset
 * between roughly 18:25 and 18:50 all year round. The whole seasonal swing
 * comes from the equation of time plus the ~8.2 minute longitude offset
 * from the UTC+3 standard meridian at 45 deg E.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "nl_core.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NL_DEG2RAD  ((float)(M_PI / 180.0))
#define NL_RAD2DEG  ((float)(180.0 / M_PI))

/* Site constants. */
#define NL_LAT_DEG      (-1.286389f)   /* negative = southern hemisphere   */
#define NL_LON_DEG      (36.817223f)   /* east positive                    */
#define NL_TZ_HOURS     (3.0f)         /* EAT = UTC+3                      */
#define NL_STD_MERIDIAN (15.0f * NL_TZ_HOURS)  /* 45 deg E                 */

/* Solar zenith used for "sunrise": geometric 90 deg plus 0.833 deg for
 * refraction and the solar semi-diameter. */
#define NL_SUN_ZENITH_DEG (90.833f)

/* ======================================================================== */
/*  Calendar arithmetic                                                      */
/* ======================================================================== */

static bool nl_is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int nl_days_in_month(int year, int month)
{
    static const int base[13] = { 0, 31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 30;
    if (month == 2 && nl_is_leap(year)) return 29;
    return base[month];
}

static int nl_day_of_year(int year, int month, int day)
{
    static const int cum[13] = { 0, 0, 31, 59, 90, 120, 151,
                                 181, 212, 243, 273, 304, 334 };
    int doy;

    if (month < 1) month = 1;
    if (month > 12) month = 12;

    doy = cum[month] + day;
    if (month > 2 && nl_is_leap(year)) doy += 1;
    return doy;
}

/* Sakamoto's algorithm: 0 = Sunday .. 6 = Saturday, proleptic Gregorian. */
static int nl_weekday_of(int year, int month, int day)
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int y = year;

    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (month < 3) y -= 1;

    return (int)(((y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7 + 7) % 7);
}

/* ======================================================================== */
/*  Solar position                                                           */
/* ======================================================================== */

/* Solar declination in degrees, standard Cooper approximation:
 *   decl = 23.44 * sin( 360/365 * (N - 81) )  [degrees]
 * N = day of year, 81 = the vernal equinox (about 22 March). */
static float nl_solar_declination_deg(int day_of_year)
{
    float b = (360.0f / 365.0f) * ((float)day_of_year - 81.0f);
    return 23.44f * sinf(b * NL_DEG2RAD);
}

/* Equation of time in minutes (Spencer/NOAA-style two-term series).
 * Ranges from about -14.2 min in mid-February to +16.4 min in early November.
 * Positive means the true sun is ahead of mean (clock) sun. */
static float nl_equation_of_time_min(int day_of_year)
{
    float b = (360.0f / 364.0f) * ((float)day_of_year - 81.0f) * NL_DEG2RAD;
    return 9.87f * sinf(2.0f * b) - 7.53f * cosf(b) - 1.5f * sinf(b);
}

/* Fill sunrise / sunset (fractional local hours, EAT) for a calendar date. */
static void nl_solar_times(int year, int month, int day,
                           float *out_sunrise, float *out_sunset)
{
    int doy = nl_day_of_year(year, month, day);
    float decl = nl_solar_declination_deg(doy);
    float eot  = nl_equation_of_time_min(doy);

    float lat_r  = NL_LAT_DEG * NL_DEG2RAD;
    float decl_r = decl * NL_DEG2RAD;

    /* Longitude correction: the sun crosses our meridian 4 minutes per degree
     * earlier than the timezone meridian when we are east of it.
     * Nairobi is 36.817 - 45 = -8.18 deg from 45 E => solar noon is LATER,
     * about 12:29 local, which matches observation. */
    float lon_corr_hours = (NL_STD_MERIDIAN - NL_LON_DEG) / 15.0f;
    float solar_noon = 12.0f + lon_corr_hours - (eot / 60.0f);

    float cos_h, h_deg, half_day;

    /* Hour angle of sunrise/sunset. */
    cos_h = (cosf(NL_SUN_ZENITH_DEG * NL_DEG2RAD)
             - sinf(lat_r) * sinf(decl_r))
            / (cosf(lat_r) * cosf(decl_r));

    if (cos_h > 1.0f) {
        /* Polar night - cannot happen at Nairobi, but keep it well defined. */
        if (out_sunrise) *out_sunrise = solar_noon;
        if (out_sunset)  *out_sunset  = solar_noon;
        return;
    }
    if (cos_h < -1.0f) {
        if (out_sunrise) *out_sunrise = 0.0f;
        if (out_sunset)  *out_sunset  = 24.0f;
        return;
    }

    h_deg = acosf(cos_h) * NL_RAD2DEG;   /* degrees from noon */
    half_day = h_deg / 15.0f;            /* hours */

    if (out_sunrise) *out_sunrise = solar_noon - half_day;
    if (out_sunset)  *out_sunset  = solar_noon + half_day;
}

/* Recompute derived fields (weekday, sunrise/sunset, daylight flag). */
static void nl_clock_refresh(NLClock *c)
{
    c->weekday = nl_weekday_of(c->year, c->month, c->day);
    nl_solar_times(c->year, c->month, c->day, &c->sunrise, &c->sunset);
    c->is_daylight = (c->hour >= c->sunrise && c->hour < c->sunset);
}

/* ======================================================================== */
/*  Public clock API                                                         */
/* ======================================================================== */

void nl_clock_init(NLClock *c, int year, int month, int day, float hour)
{
    if (c == NULL) return;

    memset(c, 0, sizeof(*c));

    if (year < 1) year = 1;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) day = 1;
    if (day > nl_days_in_month(year, month)) day = nl_days_in_month(year, month);

    if (!(hour >= 0.0f)) hour = 0.0f;     /* also catches NaN */
    if (hour >= 24.0f) hour = fmodf(hour, 24.0f);

    c->year = year;
    c->month = month;
    c->day = day;
    c->hour = hour;
    c->day_index = 0;

    nl_clock_refresh(c);
}

/* Roll the calendar forward by whole days. */
static void nl_clock_add_days(NLClock *c, int days)
{
    while (days > 0) {
        int dim = nl_days_in_month(c->year, c->month);
        c->day += 1;
        if (c->day > dim) {
            c->day = 1;
            c->month += 1;
            if (c->month > 12) {
                c->month = 1;
                c->year += 1;
            }
        }
        c->day_index += 1;
        days--;
    }
}

void nl_clock_advance_hours(NLClock *c, float hours)
{
    int whole_days;

    if (c == NULL) return;
    if (!(hours > 0.0f)) {
        /* Non-positive or NaN: just refresh the derived fields. */
        if (c != NULL) nl_clock_refresh(c);
        return;
    }

    /* Guard against absurd jumps (e.g. a hitched frame) blowing the loop up. */
    if (hours > 24.0f * 3650.0f) hours = 24.0f * 3650.0f;

    c->hour += hours;

    if (c->hour >= 24.0f) {
        whole_days = (int)floorf(c->hour / 24.0f);
        c->hour -= (float)whole_days * 24.0f;
        nl_clock_add_days(c, whole_days);
    }

    /* Kill accumulated float error at the wrap boundary. */
    if (c->hour < 0.0f) c->hour = 0.0f;
    if (c->hour >= 24.0f) c->hour = 0.0f;

    nl_clock_refresh(c);
}

void nl_clock_update(NLClock *c, float sim_seconds)
{
    if (c == NULL) return;
    if (!(sim_seconds > 0.0f)) {
        nl_clock_refresh(c);
        return;
    }
    nl_clock_advance_hours(c, sim_seconds / 3600.0f);
}

const char *nl_month_name(int month)
{
    static const char *names[13] = {
        "???",
        "January", "February", "March",     "April",   "May",      "June",
        "July",    "August",   "September", "October", "November", "December"
    };
    if (month < 1 || month > 12) return names[0];
    return names[month];
}

const char *nl_weekday_name(int weekday)
{
    static const char *names[8] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday", "???"
    };
    if (weekday < 0 || weekday > 6) return names[7];
    return names[weekday];
}

void nl_clock_format(const NLClock *c, char *out, size_t n)
{
    int hh, mm;
    float h;

    if (out == NULL || n == 0) return;
    if (c == NULL) { out[0] = '\0'; return; }

    h = c->hour;
    if (!(h >= 0.0f)) h = 0.0f;
    if (h >= 24.0f) h = fmodf(h, 24.0f);

    hh = (int)h;
    mm = (int)((h - (float)hh) * 60.0f + 0.5f);
    if (mm >= 60) { mm -= 60; hh += 1; }
    if (hh >= 24) hh -= 24;

    /* e.g. "Tue 14 Apr 2026  06:32" */
    snprintf(out, n, "%.3s %02d %.3s %04d  %02d:%02d",
             nl_weekday_name(c->weekday),
             c->day,
             nl_month_name(c->month),
             c->year,
             hh, mm);
    out[n - 1] = '\0';
}

/* Nairobi rush hour. The city's traffic is notorious: the morning peak runs
 * roughly 06:30-09:30 as matatus pour in along Mombasa Road, Jogoo Road and
 * Thika Superhighway, and the evening peak 16:30-19:30. Weekends are much
 * lighter, so we return false for Saturday and Sunday. */
bool nl_clock_is_rush_hour(const NLClock *c)
{
    float h;

    if (c == NULL) return false;
    if (nl_clock_is_weekend(c)) return false;

    h = c->hour;
    if (h >= 6.5f && h < 9.5f)   return true;
    if (h >= 16.5f && h < 19.5f) return true;
    return false;
}

bool nl_clock_is_weekend(const NLClock *c)
{
    if (c == NULL) return false;
    return (c->weekday == 0 || c->weekday == 6);
}
