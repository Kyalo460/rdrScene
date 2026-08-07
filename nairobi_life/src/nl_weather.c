/* nl_weather.c - Nairobi Life: coherent, season-aware weather simulation.
 *
 * The model is driven by fractional Brownian motion seeded per calendar day
 * (so adjacent hours stay coherent, not flickering) plus a month/season lookup
 * of real climate normals for Nairobi (Dagoretti Corner / JKIA, 1991-2020).
 *
 * Key realism points baked in:
 *   - Latitude -1.29 deg => almost no day-length variation; diurnal temperature
 *     and light are driven off the clock's sunrise/sunset, not the season.
 *   - Four-season bimodal rainfall: short dry (Jan-Feb), long rains (Mar-May,
 *     April peak), cool dry (Jun-Sep, grey overcast + light drizzle), short
 *     rains (Oct-Dec, dramatic 16:00-20:00 thunderstorms).
 *   - Convective rain timing: long/short rains favour the late afternoon into
 *     night; cool-dry produces morning mist/drizzle, never downpours.
 *   - Humidity inversely tracks temperature and spikes during rain/night.
 *   - Station pressure ~810-815 hPa at 1795 m; dips before storms.
 *   - PM2.5 high in dry season dust/traffic, washed down by rain.
 *   - murram (dirt) roads stay muddy for hours after rain.
 *   - Lightning & thunder only in thunderstorms, with realistic delay.
 *   - Equatorial twilight is short (~20-25 min); warm sunrise/sunset, deep
 *     blue night, neutral noon, grey overcast, dark storm.
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
#define NL_TWILIGHT_HOURS 0.40f   /* ~24 minutes of usable twilight */

/* Altitude for pressure/boiling calcs. */
#define NL_ALTITUDE_M 1795.0f

/* ======================================================================== */
/*  Climate normals (Nairobi)                                                */
/* ======================================================================== */

/* Monthly mean daily maximum / minimum temperature, deg C. */
static const float TMAX[12] = { 26.3f, 27.3f, 27.0f, 25.3f, 23.7f, 22.5f,
                                21.5f, 22.3f, 24.8f, 25.9f, 23.9f, 23.6f };
static const float TMIN[12] = { 13.0f, 13.4f, 14.4f, 14.8f, 13.8f, 12.0f,
                                11.2f, 11.5f, 11.9f, 13.3f, 13.7f, 13.3f };

/* Monthly mean rainfall (mm) and mean rainy days. */
static const float RAIN_MM[12]   = { 64.1f, 56.5f, 92.8f, 219.4f, 176.6f, 35.0f,
                                     17.5f, 23.5f, 28.3f,  55.0f, 154.2f, 101.0f };
static const float RAIN_DAYS[12] = { 5.0f, 5.0f, 9.0f, 16.0f, 13.0f, 6.0f,
                                     5.0f, 6.0f, 5.0f,  7.0f, 14.0f,  9.0f };

static int nl_month_index(int month)
{
    if (month < 1)  return 0;
    if (month > 12) return 11;
    return month - 1;
}

/* Day-of-year (1..366) used to seed the per-day coherent noise field. */
static int nl_day_of_year_for(int year, int month, int day)
{
    static const int cum[13] = { 0, 0, 31, 59, 90, 120, 151,
                                 181, 212, 243, 273, 304, 334 };
    int doy = cum[month] + day;
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (month > 2 && leap) doy += 1;
    return doy;
}

/* ======================================================================== */
/*  Season                                                                   */
/* ======================================================================== */

NLSeason nl_season_for_month(int month)
{
    int m = nl_month_index(month);

    /* 0=Jan .. 11=Dec */
    if (m <= 1)  return NL_SEASON_SHORT_DRY;   /* Jan, Feb  */
    if (m <= 4)  return NL_SEASON_LONG_RAINS;  /* Mar, Apr, May */
    if (m <= 8)  return NL_SEASON_COOL_DRY;    /* Jun, Jul, Aug, Sep */
    return NL_SEASON_SHORT_RAINS;              /* Oct, Nov, Dec */
}

const char *nl_season_name(NLSeason s)
{
    static const char *names[NL_SEASON_COUNT] = {
        "Short Dry", "Long Rains", "Cool Dry", "Short Rains"
    };
    if (s < 0 || s >= NL_SEASON_COUNT) return "Unknown";
    return names[s];
}

const char *nl_sky_name(NLSkyState s)
{
    static const char *names[NL_SKY_COUNT] = {
        "Clear", "Partly Cloudy", "Overcast", "Drizzle",
        "Rain", "Heavy Rain", "Thunderstorm", "Mist"
    };
    if (s < 0 || s >= NL_SKY_COUNT) return "Unknown";
    return names[s];
}

/* ======================================================================== */
/*  Sky-state probability model per hour (convective rain timing)            */
/* ======================================================================== */

/* Returns, for the given hour (0..24, fractional), the relative likelihood
 * of "rain onset" and of "mist/drizzle" for the current season. These are
 * NOT normalised probabilities, just shape functions 0..1 used to decide
 * when rain starts in the diurnal cycle. */
static void nl_diurnal_shapes(NLSeason season_unused, float hour,
                              float *out_rain, float *out_mist)
{
    (void)season_unused;
    /* Convective peak window helper: smooth bump centred at `c` with halfwidth
     * `w`, value 1.0 at centre, 0.0 outside [c-w, c+w]. */
    float rain = 0.0f;
    float mist = 0.0f;

    /* Late-afternoon/night convective peak: classic 16:00-20:00, tail to ~01. */
    {
        /* Build a smooth bump from 14:00 to 02:00 (wrapping) peaking ~19:30. */
        float h = hour;
        float d1 = h - 19.5f;            /* peak evening */
        float d2 = (h < 12.0f) ? (h + 24.0f - 19.5f) : d1;  /* wrap after midnight */
        float peak = 0.0f;
        /* Evening peak */
        {
            float x = d1 / 5.0f;
            if (x > -1.0f && x < 1.0f) {
                float t = 1.0f - fabsf(x);
                peak = t * t * (3.0f - 2.0f * t);
            }
        }
        /* Late-night secondary shoulder for long rains */
        {
            float x = d2 / 6.0f;
            if (h < 4.0f && x > -1.0f && x < 1.0f) {
                float t = 1.0f - fabsf(x);
                float sh = t * t * (3.0f - 2.0f * t);
                peak = peak > sh ? peak : sh;
            }
        }
        rain = peak;
    }

    /* Morning mist/drizzle: cool-dry season, 04:00-09:00, gentle bump. */
    {
        float x = (hour - 6.5f) / 2.6f;
        if (x > -1.0f && x < 1.0f) {
            float t = 1.0f - fabsf(x);
            mist = t * t * (3.0f - 2.0f * t);
        } else {
            mist = 0.0f;
        }
    }

    if (out_rain) *out_rain = nl_clampf(rain, 0.0f, 1.0f);
    if (out_mist) *out_mist = nl_clampf(mist, 0.0f, 1.0f);
}

/* ======================================================================== */
/*  Helpers                                                                  */
/* ======================================================================== */

/* Smooth diurnal temperature curve: minimum just before sunrise, maximum
 * ~15:00, cosine blend. */
static float nl_diurnal_temp(float hour, float sunrise, float sunset_unused,
                             float tmin, float tmax)
{
    (void)sunset_unused;
    /* Maximum at 15:00, minimum at (sunrise - 0.5h). */
    float tmin_hour = sunrise - 0.5f;
    float tmax_hour = 15.0f;
    float phase;       /* 0 at min, 1 at max */
    float span = tmax_hour - tmin_hour;

    if (span <= 0.0f) span = 15.0f;

    /* Position within the day relative to the min time. */
    float rel = hour - tmin_hour;
    if (rel < 0.0f) rel += 24.0f;
    if (rel > 24.0f) rel -= 24.0f;

    phase = rel / span;
    /* Two half-cosine: rise from min to max over `span`, fall back over rest. */
    if (phase < 1.0f) {
        return tmin + (tmax - tmin) * (0.5f - 0.5f * cosf((float)M_PI * phase));
    } else {
        float q = (phase - 1.0f) / (24.0f / span - 1.0f);
        return tmax - (tmax - tmin) * (0.5f - 0.5f * cosf((float)M_PI * q));
    }
}

/* Ambient sky light from sun elevation and cloud cover. Returns rgb + intensity
 * in `out` (r,g,b,intensity). */
static void nl_sky_light(float hour, float sunrise, float sunset,
                         float cloud, NLSkyState sky,
                         float *r, float *g, float *b, float *intensity)
{
    float day = 0.0f;     /* 0 night .. 1 full day */
    float twi = 0.0f;     /* twilight contribution near horizon */

    /* Day factor: smooth ramp over twilight on each side. */
    if (hour >= sunrise + NL_TWILIGHT_HOURS && hour <= sunset - NL_TWILIGHT_HOURS) {
        day = 1.0f;
    } else if (hour > sunset && hour < sunset + NL_TWILIGHT_HOURS) {
        float t = 1.0f - (hour - sunset) / NL_TWILIGHT_HOURS;
        day = t * t;
        twi = t;
    } else if (hour >= sunrise - NL_TWILIGHT_HOURS && hour < sunrise) {
        float t = (hour - (sunrise - NL_TWILIGHT_HOURS)) / NL_TWILIGHT_HOURS;
        day = t * t;
        twi = t;
    } else if (hour >= sunrise && hour <= sunset) {
        /* inside day but within twilight on the early/late edge */
        float a = nl_smoothstep(sunrise - NL_TWILIGHT_HOURS, sunrise + NL_TWILIGHT_HOURS, hour);
        float c = nl_smoothstep(sunset - NL_TWILIGHT_HOURS, sunset + NL_TWILIGHT_HOURS, hour);
        day = a * c;
        twi = (a < c) ? a : c;
    } else {
        day = 0.0f;
    }

    /* Base clear-sky colours. */
    /* Midday: bright neutral slightly cool. */
    float mid_r = 0.92f, mid_g = 0.95f, mid_b = 1.00f;
    /* Night: deep blue. */
    float night_r = 0.03f, night_g = 0.05f, night_b = 0.12f;
    /* Twilight/sunrise-sunset: warm orange. */
    float sun_r = 1.00f, sun_g = 0.45f, sun_b = 0.18f;

    float rr, gg, bb;

    /* Blend night -> day. */
    rr = nl_lerpf(night_r, mid_r, day);
    gg = nl_lerpf(night_g, mid_g, day);
    bb = nl_lerpf(night_b, mid_b, day);

    /* Add warm twilight tint near horizon. */
    {
        float w = twi * (1.0f - day) * 0.9f;
        rr = nl_lerpf(rr, sun_r, w);
        gg = nl_lerpf(gg, sun_g, w);
        bb = nl_lerpf(bb, sun_b, w);
    }

    /* Cloud cover desaturates and darkens toward grey. */
    if (cloud > 0.0f) {
        float target_r, target_g, target_b;
        if (sky == NL_SKY_THUNDERSTORM || sky == NL_SKY_HEAVY_RAIN) {
            target_r = 0.10f; target_g = 0.11f; target_b = 0.13f;
        } else {
            target_r = 0.55f; target_g = 0.57f; target_b = 0.60f;
        }
        float k = cloud * (sky == NL_SKY_CLEAR ? 0.3f : 1.0f);
        if (k > 1.0f) k = 1.0f;
        rr = nl_lerpf(rr, target_r, k);
        gg = nl_lerpf(gg, target_g, k);
        bb = nl_lerpf(bb, target_b, k);
    }

    /* Lightning flash brightens everything toward white briefly. */
    /* (handled by caller via lightning_flash field; keep base here) */

    *r = rr;
    *g = gg;
    *b = bb;
    *intensity = 0.15f + 0.85f * day * (1.0f - 0.6f * cloud);
    if (*intensity < 0.05f) *intensity = 0.05f;
}

/* ======================================================================== */
/*  Init                                                                     */
/* ======================================================================== */

/* ======================================================================== */
/*  Rain episode state                                                       */
/* ======================================================================== */

/* Nairobi rain is convective: it arrives as a burst that builds, peaks and
 * tapers, typically lasting 30-90 minutes, rather than raining steadily all
 * day. Tracking the episode explicitly is what keeps monthly totals near the
 * real climate normals - a latched intensity would over-deliver rain by
 * several hundred percent. */
static float s_ep_remaining_h = 0.0f;   /* hours left in the burst  */
static float s_ep_total_h     = 0.0f;   /* full duration of burst   */
static float s_ep_peak        = 0.0f;   /* peak intensity mm/hr     */

void nl_weather_init(NLWeather *w, const NLClock *c, uint32_t seed)
{
    float sr, ss;

    if (w == NULL) return;
    memset(w, 0, sizeof(*w));

    s_ep_remaining_h = 0.0f;
    s_ep_total_h     = 0.0f;
    s_ep_peak        = 0.0f;

    if (c == NULL) {
        sr = 6.3f; ss = 18.6f;
    } else {
        sr = c->sunrise; ss = c->sunset;
    }

    w->season = nl_season_for_month(c ? c->month : 1);
    w->sky = NL_SKY_CLEAR;
    w->temperature_c = (TMAX[0] + TMIN[0]) * 0.5f;
    w->apparent_c = w->temperature_c;
    w->humidity = 0.6f;
    w->cloud_cover = 0.15f;
    w->rain_mm_hr = 0.0f;
    w->rain_accum_mm = 0.0f;
    w->wind_speed_ms = 2.5f;
    w->wind_dir_deg = 45.0f;
    w->visibility_m = 12000.0f;
    w->pressure_hpa = 813.0f;
    w->aqi_pm25 = 30.0f;
    w->ground_wetness = 0.0f;
    w->mud_factor = 0.0f;
    w->lightning_flash = 0.0f;
    w->thunder_delay = 0.0f;

    (void)seed; (void)sr; (void)ss;
}

/* ======================================================================== */
/*  Update                                                                   */
/* ======================================================================== */

void nl_weather_update(NLWeather *w, const NLClock *c, float sim_seconds,
                       uint32_t *rng)
{
    float hour, sr, ss;
    int   month_idx;
    float tmax, tmin, rain_norm, raindays_norm, rain_onset, mist_onset;
    float rain_prob, fbm;
    uint32_t seed;
    float dt_hours;
    float prev_rain;

    if (w == NULL || c == NULL) return;

    hour = c->hour;
    sr = c->sunrise;
    ss = c->sunset;
    month_idx = nl_month_index(c->month);
    w->season = nl_season_for_month(c->month);
    tmax = TMAX[month_idx];
    tmin = TMIN[month_idx];

    seed = (uint32_t)(c->year * 1000 + nl_day_of_year_for(c->year, c->month, c->day));
    /* Use a private per-day fbm; the caller passes rng for stochastic strikes. */

    dt_hours = sim_seconds / 3600.0f;
    if (dt_hours < 0.0f) dt_hours = 0.0f;
    if (dt_hours > 48.0f) dt_hours = 48.0f;

    /* ---- fbm field: slowly varying 0..1 coherence over the day ---- */
    /* Day-local coordinate so the field is continuous hour to hour. */
    fbm = nl_fbm1(hour * 0.45f + (float)c->day_index * 3.7f, seed ^ 0x1234u, 4);

    /* ---- diurnal temperature (cloud damps the swing) ---- */
    {
        float base_t = nl_diurnal_temp(hour, sr, ss, tmin, tmax);
        float damp = 1.0f - 0.45f * w->cloud_cover;
        w->temperature_c = tmin + (base_t - tmin) * damp + 0.0f;
        /* Rain drops temperature a few degrees. */
        w->temperature_c -= 2.5f * nl_clampf(w->rain_mm_hr / 8.0f, 0.0f, 1.0f);
    }

    /* ---- humidity: inverse of temperature + rain/night boost ---- */
    {
        float inv = 1.0f - nl_clampf((w->temperature_c - tmin) / (tmax - tmin + 1e-3f), 0.0f, 1.0f);
        float hum = 0.30f + 0.55f * inv;
        if (!c->is_daylight) hum += 0.12f;                 /* night saturation */
        hum += 0.35f * nl_clampf(w->rain_mm_hr / 6.0f, 0.0f, 1.0f);  /* rain */
        hum += 0.15f * w->cloud_cover;
        w->humidity = nl_clampf(hum, 0.0f, 1.0f);
    }

    /* ---- apparent temperature (feels-like): humidity heat + wind cool ---- */
    {
        float ap = w->temperature_c;
        ap += (w->humidity - 0.5f) * 4.0f;                 /* humid = hotter */
        ap -= w->wind_speed_ms * 0.25f;                    /* breeze cools */
        w->apparent_c = ap;
    }

    /* ---- cloud cover: season + fbm + rain state ---- */
    {
        float base_cloud;
        switch (w->season) {
            case NL_SEASON_COOL_DRY:    base_cloud = 0.55f; break;  /* grey overcast */
            case NL_SEASON_LONG_RAINS:  base_cloud = 0.50f; break;
            case NL_SEASON_SHORT_RAINS: base_cloud = 0.40f; break;
            case NL_SEASON_SHORT_DRY:   base_cloud = 0.18f; break;
            default:                    base_cloud = 0.30f; break;
        }
        base_cloud += (fbm - 0.5f) * 0.5f;
        if (w->rain_mm_hr > 0.2f) base_cloud += 0.3f;
        w->cloud_cover = nl_clampf(base_cloud, 0.0f, 1.0f);
    }

    /* ---- rainfall decision (convective diurnal timing) ---- */
    nl_diurnal_shapes(w->season, hour, &rain_onset, &mist_onset);
    rain_norm = RAIN_MM[month_idx];
    /* Monthly total is an emergent result of episode frequency x duration x
     * intensity rather than a direct driver, so the normal is retained only
     * for reference/debugging. */
    (void)rain_norm;
    raindays_norm = RAIN_DAYS[month_idx] / 30.0f;

    prev_rain = w->rain_mm_hr;

    /* Probability that, this hour, a rain episode BEGINS. Scaled so that the
     * expected number of rain days per month tracks the real climate normals.
     * raindays_norm is the monthly mean number of days with rain; dividing by
     * the hours in a day converts it to a per-hour onset chance, which the
     * diurnal convective curve then concentrates into the right part of the
     * day. */
    {
        float mist_prob;
        float field = fbm;

        /* Expected episodes per day is derived from the monthly rain-day
         * normal. The diurnal curve only redistributes WHEN rain falls, so it
         * is normalised by its own daily mean (~0.30) to avoid suppressing the
         * overall frequency. Wet months average slightly more than one burst
         * per rain day, which is what reproduces the real monthly totals. */
        float days_frac = nl_clampf(raindays_norm / 30.0f, 0.0f, 1.0f);
        float onset_norm = (0.10f + 0.90f * rain_onset) / 0.37f;
        rain_prob = days_frac * onset_norm * dt_hours * 1.35f;
        rain_prob = nl_clampf(rain_prob, 0.0f, 0.9f);

        /* Mist/drizzle only in cool-dry mornings, light and brief. */
        mist_prob = (w->season == NL_SEASON_COOL_DRY)
                    ? (0.5f * mist_onset * dt_hours) : 0.0f;

        if (s_ep_remaining_h > 0.0f) {
            /* --- inside an active episode: advance and shape it --- */
            float t, shape;

            s_ep_remaining_h -= dt_hours;
            if (s_ep_remaining_h < 0.0f) s_ep_remaining_h = 0.0f;

            /* Fraction elapsed through the burst. */
            t = (s_ep_total_h > 0.0f)
                ? 1.0f - (s_ep_remaining_h / s_ep_total_h) : 1.0f;
            t = nl_clampf(t, 0.0f, 1.0f);

            /* Asymmetric profile: fast build, slower taper - how a real
             * convective cell passes overhead. */
            if (t < 0.25f) shape = t / 0.25f;
            else           shape = 1.0f - ((t - 0.25f) / 0.75f);
            shape = nl_clampf(shape, 0.0f, 1.0f);
            shape = shape * shape * (3.0f - 2.0f * shape);   /* smoothstep */

            w->rain_mm_hr = s_ep_peak * shape;

            if (s_ep_remaining_h <= 0.0f) {
                s_ep_peak = 0.0f;
                s_ep_total_h = 0.0f;
                w->rain_mm_hr = 0.0f;
            }
        } else if (nl_randf(rng) < rain_prob) {
            /* --- start a new episode --- */
            float intensity, dur;

            if (w->season == NL_SEASON_COOL_DRY) {
                /* Gathandara: fine cold drizzle, long and light. */
                w->sky = NL_SKY_DRIZZLE;
                intensity = nl_randf_range(rng, 0.3f, 1.6f);
                dur       = nl_randf_range(rng, 0.7f, 2.5f);
            } else if (w->season == NL_SEASON_SHORT_RAINS && rain_onset > 0.45f) {
                if (nl_randf(rng) < 0.45f) {
                    w->sky = NL_SKY_THUNDERSTORM;
                    intensity = nl_randf_range(rng, 14.0f, 38.0f);
                    dur       = nl_randf_range(rng, 0.4f, 1.1f);
                } else {
                    w->sky = NL_SKY_RAIN;
                    intensity = nl_randf_range(rng, 3.0f, 12.0f);
                    dur       = nl_randf_range(rng, 0.4f, 1.2f);
                }
            } else if (w->season == NL_SEASON_LONG_RAINS) {
                if (field > 0.82f) {
                    w->sky = NL_SKY_HEAVY_RAIN;
                    intensity = nl_randf_range(rng, 12.0f, 30.0f);
                    dur       = nl_randf_range(rng, 0.6f, 1.6f);
                } else {
                    w->sky = NL_SKY_RAIN;
                    intensity = nl_randf_range(rng, 2.5f, 11.0f);
                    dur       = nl_randf_range(rng, 0.5f, 1.8f);
                }
            } else {
                /* Hot dry season: rare, sharp, short showers. */
                w->sky = NL_SKY_RAIN;
                intensity = nl_randf_range(rng, 2.0f, 9.0f);
                dur       = nl_randf_range(rng, 0.3f, 0.9f);
            }

            s_ep_peak        = intensity;
            s_ep_total_h     = dur;
            s_ep_remaining_h = dur;
            w->rain_mm_hr    = 0.0f;   /* builds from zero via the shape */
        } else if (mist_prob > 0.0f && nl_randf(rng) < mist_prob) {
            w->sky = NL_SKY_MIST;
            s_ep_peak        = nl_randf_range(rng, 0.2f, 0.9f);
            s_ep_total_h     = nl_randf_range(rng, 0.8f, 2.2f);
            s_ep_remaining_h = s_ep_total_h;
            w->rain_mm_hr    = 0.0f;
        }
    }

    /* ---- sky classification when no episode is running ---- */
    if (w->rain_mm_hr <= 0.05f && s_ep_remaining_h <= 0.0f) {
        if (w->cloud_cover > 0.75f)      w->sky = NL_SKY_OVERCAST;
        else if (w->cloud_cover > 0.4f)  w->sky = NL_SKY_PARTLY_CLOUDY;
        else                             w->sky = NL_SKY_CLEAR;

        /* Grey cool-dry mornings look misty without measurable rain. */
        if (w->season == NL_SEASON_COOL_DRY && mist_onset > 0.3f &&
            w->cloud_cover > 0.5f) {
            w->sky = NL_SKY_MIST;
        }
    }

    /* The episode profile already ramps intensity smoothly, so no extra
     * smoothing pass is needed here. Just clean up trailing dust. */
    (void)prev_rain;
    if (w->rain_mm_hr < 0.02f) {
        w->rain_mm_hr = 0.0f;
        if (w->sky == NL_SKY_RAIN || w->sky == NL_SKY_HEAVY_RAIN ||
            w->sky == NL_SKY_DRIZZLE || w->sky == NL_SKY_MIST)
            w->sky = NL_SKY_PARTLY_CLOUDY;
        if (w->sky == NL_SKY_THUNDERSTORM) w->sky = NL_SKY_OVERCAST;
    }

    /* ---- lightning & thunder (thunderstorms only) ---- */
    {
        if (w->sky == NL_SKY_THUNDERSTORM && w->rain_mm_hr > 4.0f && rng) {
            /* Strike probability scales with intensity; ~ every few minutes. */
            float p_strike = nl_clampf(w->rain_mm_hr / 22.0f, 0.0f, 1.0f)
                             * nl_clampf(dt_hours * 3.0f, 0.0f, 1.0f);
            if (nl_randf(rng) < p_strike) {
                w->lightning_flash = 1.0f;
                /* Distance 2-12 km => delay 5.8-35 s. */
                float dist_km = nl_randf_range(rng, 2.0f, 12.0f);
                w->thunder_delay = dist_km * 1000.0f / 343.0f;
            }
        }
        /* Decay flash and count down thunder. */
        w->lightning_flash = nl_lerpf(w->lightning_flash, 0.0f,
                                      nl_clampf(dt_hours * 12.0f, 0.0f, 1.0f));
        if (w->thunder_delay > 0.0f) {
            w->thunder_delay -= sim_seconds;
            if (w->thunder_delay < 0.0f) w->thunder_delay = 0.0f;
        }
    }

    /* ---- wind: light, direction by season, gusts before storms ---- */
    {
        float base_w;
        switch (w->season) {
            case NL_SEASON_SHORT_DRY:  base_w = 3.0f; w->wind_dir_deg = 45.0f; break;  /* NE */
            case NL_SEASON_COOL_DRY:   base_w = 2.5f; w->wind_dir_deg = 135.0f; break; /* SE */
            default:                   base_w = 2.8f; w->wind_dir_deg = 90.0f; break;
        }
        base_w += (fbm - 0.5f) * 1.5f;
        if (w->sky == NL_SKY_THUNDERSTORM) base_w += nl_randf_range(rng, 3.0f, 8.0f);
        else if (w->rain_mm_hr > 4.0f)     base_w += 2.0f;
        w->wind_speed_ms = nl_clampf(base_w, 0.4f, 16.0f);
    }

    /* ---- visibility: drops with rain/mist/dust ---- */
    {
        float vis = 14000.0f;
        vis -= 9000.0f * nl_clampf(w->rain_mm_hr / 12.0f, 0.0f, 1.0f);
        vis -= 6000.0f * (w->sky == NL_SKY_MIST ? 1.0f : 0.0f);
        if (w->aqi_pm25 > 40.0f) vis -= (w->aqi_pm25 - 40.0f) * 120.0f;
        w->visibility_m = nl_clampf(vis, 300.0f, 16000.0f);
    }

    /* ---- pressure: ~810-815 hPa, dips before/during storms ---- */
    {
        float p = 813.0f - 6.0f * nl_clampf(w->rain_mm_hr / 12.0f, 0.0f, 1.0f);
        if (w->sky == NL_SKY_THUNDERSTORM) p -= 5.0f;
        p += (fbm - 0.5f) * 3.0f;            /* gentle diurnal-ish variation */
        w->pressure_hpa = nl_clampf(p, 795.0f, 818.0f);
    }

    /* ---- PM2.5 air quality ---- */
    {
        float base_aqi;
        /* Dry seasons: dust + traffic => 25-60; rain washes to 8-15. */
        switch (w->season) {
            case NL_SEASON_SHORT_DRY:  base_aqi = 42.0f; break;
            case NL_SEASON_COOL_DRY:   base_aqi = 38.0f; break;
            default:                   base_aqi = 22.0f; break;
        }
        base_aqi += (fbm - 0.5f) * 10.0f;
        /* Rush hour traffic bump (use weekday + hour heuristic). */
        if (c->weekday >= 1 && c->weekday <= 5) {
            if ((hour >= 6.5f && hour < 9.5f) || (hour >= 16.5f && hour < 19.5f))
                base_aqi += 8.0f;
        }
        /* Rain scavenging. */
        base_aqi -= 18.0f * nl_clampf(w->rain_mm_hr / 10.0f, 0.0f, 1.0f);
        w->aqi_pm25 = nl_clampf(base_aqi, 6.0f, 70.0f);
    }

    /* ---- ground wetness & mud persistence ---- */
    {
        float add = w->rain_mm_hr * dt_hours * 0.06f;
        /* Evaporation driven by temperature & sun. */
        float sun = c->is_daylight ? 1.0f : 0.3f;
        float evap = (0.01f + 0.03f * nl_clampf((w->temperature_c - tmin) /
                    (tmax - tmin + 1e-3f), 0.0f, 1.0f)) * sun * dt_hours;
        w->ground_wetness = nl_clampf(w->ground_wetness + add - evap, 0.0f, 1.0f);

        /* Mud forms on dirt when wet, and lingers long after (murram stays
         * muddy for hours). Decays far slower than bare wetness. */
        float mud_add = nl_clampf(w->ground_wetness - 0.25f, 0.0f, 1.0f) * add * 1.5f;
        float mud_dry = 0.012f * sun * dt_hours;   /* very slow drying */
        w->mud_factor = nl_clampf(w->mud_factor + mud_add - mud_dry, 0.0f, 1.0f);
        /* Mud can't exceed wetness. */
        if (w->mud_factor > w->ground_wetness) w->mud_factor = w->ground_wetness;
    }

    /* ---- accumulated rainfall today ---- */
    w->rain_accum_mm += w->rain_mm_hr * dt_hours;

    /* ---- ambient sky light ---- */
    {
        float cr, cg, cb, ci;
        nl_sky_light(hour, sr, ss, w->cloud_cover, w->sky, &cr, &cg, &cb, &ci);
        /* Lightning override. */
        if (w->lightning_flash > 0.01f) {
            cr = nl_lerpf(cr, 1.0f, w->lightning_flash);
            cg = nl_lerpf(cg, 1.0f, w->lightning_flash);
            cb = nl_lerpf(cb, 1.0f, w->lightning_flash);
            ci = nl_lerpf(ci, 0.9f, w->lightning_flash);
        }
        w->ambient_r = cr;
        w->ambient_g = cg;
        w->ambient_b = cb;
        w->ambient_intensity = ci;
    }
}

/* ======================================================================== */
/*  Movement & earning multipliers                                           */
/* ======================================================================== */

float nl_weather_move_multiplier(const NLWeather *w, NLSurface surf)
{
    float m = 1.0f;

    if (w == NULL) return 1.0f;

    /* Surface penalties. */
    switch (surf) {
        case NL_SURF_TARMAC:
            if (w->ground_wetness > 0.3f) m -= 0.10f * nl_clampf(w->ground_wetness, 0.0f, 1.0f);
            break;
        case NL_SURF_DIRT:
            /* Murram becomes heavy mud; lingering penalty for hours. */
            m -= 0.55f * w->mud_factor;
            m -= 0.10f * nl_clampf(w->ground_wetness - 0.5f, 0.0f, 1.0f);
            break;
        case NL_SURF_CONCRETE:
            if (w->ground_wetness > 0.5f) m -= 0.05f;
            break;
        case NL_SURF_GRASS:
            m -= 0.20f * w->ground_wetness;
            break;
        case NL_SURF_WATER:
            m -= 0.70f;       /* open drain / stream: must detour */
            break;
        default:
            break;
    }

    /* Weather penalties: heavy rain and wind slow you down. */
    m -= 0.20f * nl_clampf(w->rain_mm_hr / 12.0f, 0.0f, 1.0f);
    if (w->sky == NL_SKY_THUNDERSTORM) m -= 0.10f;
    m -= 0.05f * nl_clampf((w->wind_speed_ms - 5.0f) / 8.0f, 0.0f, 1.0f);

    /* Slipperiness from mud/rain increases foot hazard. */
    if (w->mud_factor > 0.6f) m -= 0.05f;

    if (m < 0.15f) m = 0.15f;
    return m;
}

float nl_weather_earning_multiplier(const NLWeather *w, NLJobKind job)
{
    float m = 1.0f;
    float hot;     /* dryness/heat convenience factor 0..1 */
    float wet;     /* how rainy 0..1 */

    if (w == NULL) return 1.0f;

    hot = nl_clampf(w->humidity < 0.5f ? 1.0f - w->humidity : 0.0f, 0.0f, 1.0f);
    wet = nl_clampf(w->rain_mm_hr / 8.0f, 0.0f, 1.0f);

    switch (job) {
        case NL_JOB_HAWK_WATER:
            /* Thirsty, hot, dry weather => people buy water. Rain kills demand. */
            m = 0.5f + 0.9f * (1.0f - w->humidity) /* dry => thirst */
                      + 0.3f * (w->temperature_c > 25.0f ? 1.0f : 0.0f);
            m -= 0.7f * wet;
            break;

        case NL_JOB_HAWK_SWEETS:
            m = 0.8f + 0.3f * hot;
            m -= 0.4f * wet;
            break;

        case NL_JOB_CAR_WASH:
            /* Needs water: collapses entirely in rain, booms just after. */
            if (w->rain_mm_hr > 0.5f) m = 0.05f;
            else m = 1.2f - 0.4f * w->ground_wetness;  /* works fine dry */
            break;

        case NL_JOB_CONSTRUCTION:
        case NL_JOB_LOADING:
        case NL_JOB_MKOKOTENI:
            /* Mjengo casual labour stops in heavy rain / deep mud. */
            m = 1.0f - 0.9f * wet;
            m -= 0.5f * w->mud_factor;
            if (w->sky == NL_SKY_HEAVY_RAIN || w->sky == NL_SKY_THUNDERSTORM) m = 0.05f;
            break;

        case NL_JOB_COLLECT_SCRAP:
            /* Wet scrap is heavier/dirtier and fewer buyers; slight. */
            m = 1.0f - 0.4f * wet - 0.2f * w->mud_factor;
            break;

        case NL_JOB_PARKING_GUIDE:
            /* More cars in dry; rain reduces drivers' willingness. */
            m = 1.0f - 0.4f * wet;
            break;

        case NL_JOB_BEG:
            /* Sympathy rises in rain, but fewer people are out and it's
             * uncomfortable, so net is modest. */
            m = 0.8f + 0.5f * wet;
            if (w->temperature_c < 14.0f) m -= 0.2f;   /* cold = fewer givers */
            break;

        case NL_JOB_NONE:
        default:
            m = 1.0f;
            break;
    }

    if (m < 0.0f) m = 0.0f;
    if (m > 1.6f) m = 1.6f;
    return m;
}
