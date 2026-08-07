/* test_sim.c - headless verification of the Nairobi Life simulation.
 *
 * Runs the clock/weather/economy models over a full simulated year without
 * opening a window, and checks that the outputs match real Nairobi climate
 * normals and that no values go non-finite or out of range.
 *
 * Build: see the "test" target in the Makefile.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "nl_core.h"

static int g_fail = 0;

static void check(int cond, const char *what)
{
    if (!cond) { printf("  FAIL: %s\n", what); g_fail++; }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Nairobi Life simulation verification ===\n\n");

    /* ---------------------------------------------------------------- */
    printf("[1] Sunrise/sunset must barely move near the equator\n");
    {
        float min_rise = 99.0f, max_rise = -99.0f;
        float min_set  = 99.0f, max_set  = -99.0f;
        for (int m = 1; m <= 12; m++) {
            NLClock c;
            nl_clock_init(&c, 2026, m, 15, 12.0f);
            if (c.sunrise < min_rise) min_rise = c.sunrise;
            if (c.sunrise > max_rise) max_rise = c.sunrise;
            if (c.sunset  < min_set)  min_set  = c.sunset;
            if (c.sunset  > max_set)  max_set  = c.sunset;
            printf("    %-4s sunrise %5.2f  sunset %5.2f  daylen %5.2f h\n",
                   nl_month_name(m), c.sunrise, c.sunset, c.sunset - c.sunrise);
        }
        /* Real Nairobi: sunrise ~06:15-06:35, sunset ~18:25-18:45. */
        check(min_rise > 5.9f && max_rise < 6.9f, "sunrise within 05:54-06:54");
        check(min_set  > 18.0f && max_set < 19.0f, "sunset within 18:00-19:00");
        check((max_rise - min_rise) < 0.5f, "sunrise varies < 30 min over year");
    }

    /* ---------------------------------------------------------------- */
    printf("\n[2] Annual climate: simulate every day of 2026\n");
    {
        static NLGame g;   /* large; keep off the stack */
        nl_clock_init(&g.clock, 2026, 1, 1, 0.0f);
        g.rng_state = 20260101u;
        nl_weather_init(&g.weather, &g.clock, g.rng_state);

        double month_rain[13] = {0};
        double month_tmax[13], month_tmin[13];
        int    month_days[13] = {0};
        for (int i = 0; i < 13; i++) { month_tmax[i] = -100.0; month_tmin[i] = 100.0; }

        int bad_finite = 0, bad_range = 0;
        int storms_in_july = 0;

        /* Step the whole year at 10 simulated minutes per step. */
        const float step = 600.0f;
        int steps = (int)((365.0 * 24.0 * 3600.0) / step);
        int last_day = g.clock.day_index;
        double day_rain_accum = 0.0;

        for (int s = 0; s < steps; s++) {
            nl_clock_update(&g.clock, step);
            nl_weather_update(&g.weather, &g.clock, step, &g.rng_state);

            const NLWeather *w = &g.weather;
            int m = g.clock.month;

            if (!isfinite(w->temperature_c) || !isfinite(w->rain_mm_hr) ||
                !isfinite(w->humidity) || !isfinite(w->wind_speed_ms) ||
                !isfinite(w->ambient_intensity))
                bad_finite++;

            if (w->humidity < 0.0f || w->humidity > 1.0001f ||
                w->cloud_cover < 0.0f || w->cloud_cover > 1.0001f ||
                w->rain_mm_hr < 0.0f ||
                w->ambient_intensity < 0.0f || w->ambient_intensity > 1.0001f ||
                w->ground_wetness < 0.0f || w->ground_wetness > 1.0001f)
                bad_range++;

            /* Accumulate rainfall: mm/hr over a 10 min slice. */
            day_rain_accum += w->rain_mm_hr * (step / 3600.0);

            if (w->temperature_c > month_tmax[m]) month_tmax[m] = w->temperature_c;
            if (w->temperature_c < month_tmin[m]) month_tmin[m] = w->temperature_c;

            if (m == 7 && w->sky == NL_SKY_THUNDERSTORM) storms_in_july++;

            if (g.clock.day_index != last_day) {
                month_rain[m] += day_rain_accum;
                month_days[m]++;
                day_rain_accum = 0.0;
                last_day = g.clock.day_index;
            }
        }

        printf("    Month  rain_mm   Tmin   Tmax\n");
        double total = 0.0;
        for (int m = 1; m <= 12; m++) {
            printf("    %-5s %8.1f %6.1f %6.1f\n", nl_month_name(m),
                   month_rain[m], month_tmin[m], month_tmax[m]);
            total += month_rain[m];
        }
        printf("    Annual total rainfall: %.0f mm\n", total);

        check(bad_finite == 0, "all weather values finite");
        check(bad_range == 0,  "weather values within valid ranges");
        check(storms_in_july == 0, "no thunderstorms in July (cool dry season)");

        /* Nairobi annual rainfall is roughly 700-1050 mm. */
        check(total > 550.0 && total < 1400.0, "annual rainfall plausible (550-1400mm)");

        /* Long rains (Mar-May) must beat the July-August dry period. */
        double longrains = month_rain[3] + month_rain[4] + month_rain[5];
        double drymid    = month_rain[7] + month_rain[8];
        printf("    Long rains (MAM)=%.0f mm vs dry (JA)=%.0f mm\n", longrains, drymid);
        check(longrains > drymid * 2.0, "long rains much wetter than Jul-Aug");

        /* Temperature envelope. */
        check(month_tmax[2] > 22.0 && month_tmax[2] < 34.0, "Feb max plausible");
        check(month_tmin[7] > 4.0  && month_tmin[7] < 17.0, "Jul min plausible");
        /* July should be cooler than February. */
        check(month_tmax[7] < month_tmax[2], "July cooler than February");
    }

    /* ---------------------------------------------------------------- */
    printf("\n[3] Season mapping\n");
    {
        check(nl_season_for_month(1)  == NL_SEASON_SHORT_DRY,   "Jan short dry");
        check(nl_season_for_month(4)  == NL_SEASON_LONG_RAINS,  "Apr long rains");
        check(nl_season_for_month(7)  == NL_SEASON_COOL_DRY,    "Jul cool dry");
        check(nl_season_for_month(11) == NL_SEASON_SHORT_RAINS, "Nov short rains");
        printf("    Apr=%s  Jul=%s  Nov=%s\n",
               nl_season_name(nl_season_for_month(4)),
               nl_season_name(nl_season_for_month(7)),
               nl_season_name(nl_season_for_month(11)));
    }

    /* ---------------------------------------------------------------- */
    printf("\n[4] World generation and NPCs\n");
    {
        static NLGame g;
        nl_game_init(&g, 4242u);
        printf("    buildings=%d props=%d roads=%d npcs=%d vehicles=%d jobs=%d\n",
               g.building_count, g.prop_count, g.road_count,
               g.npc_count, g.vehicle_count, g.job_count);
        check(g.building_count > 50, "buildings generated");
        check(g.prop_count     > 50, "props generated");
        check(g.road_count     >  4, "roads generated");
        check(g.npc_count      > 20, "npcs spawned");
        check(g.job_count      >  3, "jobs generated");

        check(g.building_count <= NL_MAX_BUILDINGS, "buildings within bounds");
        check(g.prop_count     <= NL_MAX_PROPS,     "props within bounds");
        check(g.npc_count      <= NL_MAX_NPCS,      "npcs within bounds");
        check(g.vehicle_count  <= NL_MAX_VEHICLES,  "vehicles within bounds");
        check(g.job_count      <= NL_MAX_JOBS,      "jobs within bounds");

        /* The player must not start stuck inside a wall. */
        check(!nl_world_blocked(&g, g.player.pos, 0.45f),
              "player start position is not blocked");

        /* Districts must actually be distinguishable across the map. */
        int seen[NL_DIST_COUNT];
        memset(seen, 0, sizeof(seen));
        for (int y = 50; y < (int)NL_WORLD_H; y += 137) {
            for (int x = 50; x < (int)NL_WORLD_W; x += 137) {
                NLVec2 p = { (float)x, (float)y };
                NLDistrict d = nl_world_district_at(&g, p);
                if (d >= 0 && d < NL_DIST_COUNT) seen[d]++;
            }
        }
        int distinct = 0;
        for (int i = 0; i < NL_DIST_COUNT; i++) {
            if (seen[i] > 0) { distinct++; printf("    %-11s %d samples\n",
                                                  nl_district_name(i), seen[i]); }
        }
        check(distinct >= 4, "at least 4 districts present in the world");
    }

    /* ---------------------------------------------------------------- */
    printf("\n[5] Ten simulated days of survival (no input, idle player)\n");
    {
        static NLGame g;
        nl_game_init(&g, 99u);
        g.screen = NL_SCREEN_PLAYING;

        int bad = 0;
        /* 10 in-game days at 60x, stepping 1/60 s of real time. */
        double sim_target = 10.0 * 24.0 * 3600.0;
        double simmed = 0.0;
        float dt = 1.0f / 60.0f;
        long iter = 0;

        while (simmed < sim_target && iter < 2000000) {
            nl_clock_update(&g.clock, dt * g.time_scale);
            nl_weather_update(&g.weather, &g.clock, dt * g.time_scale, &g.rng_state);
            nl_world_update(&g, dt, dt * g.time_scale);
            nl_npc_update(&g, dt, dt * g.time_scale);
            nl_vehicle_update(&g, dt, dt * g.time_scale);
            nl_player_needs_update(&g, dt * g.time_scale);
            nl_econ_update(&g, dt, dt * g.time_scale);

            const NLPlayer *p = &g.player;
            if (!isfinite(p->health) || !isfinite(p->hunger) ||
                !isfinite(p->pos.x)  || !isfinite(p->pos.y)) bad++;
            if (p->health < -0.01f || p->health > 100.01f) bad++;
            if (p->hunger < -0.01f || p->hunger > 100.01f) bad++;

            simmed += dt * g.time_scale;
            iter++;
        }

        printf("    after 10 days: health=%.1f hunger=%.1f thirst=%.1f energy=%.1f\n",
               g.player.health, g.player.hunger, g.player.thirst, g.player.energy);
        printf("    cash=KSh %d  debt=KSh %d  illness=%s\n",
               g.player.cash, g.player.debt, nl_illness_name(g.player.illness));
        check(bad == 0, "player stats stayed finite and in range");

        /* An idle player who never eats MUST starve. If they survive 10 days
         * doing nothing, the survival pressure is not real. */
        check(g.player.health <= 50.0f,
              "idle player deteriorates without food/water");
        /* NPC and puddle counts must stay in bounds after long simulation. */
        check(g.npc_count <= NL_MAX_NPCS,    "npc count stable");
        check(g.puddle_count <= NL_MAX_PUDDLES, "puddle count stable");
    }

    /* ---------------------------------------------------------------- */
    printf("\n[6] Weather effect multipliers\n");
    {
        NLWeather dry;  memset(&dry, 0, sizeof(dry));
        dry.sky = NL_SKY_CLEAR; dry.temperature_c = 27.0f;
        dry.ground_wetness = 0.0f; dry.mud_factor = 0.0f;

        NLWeather wet;  memset(&wet, 0, sizeof(wet));
        wet.sky = NL_SKY_HEAVY_RAIN; wet.temperature_c = 17.0f;
        wet.rain_mm_hr = 25.0f; wet.ground_wetness = 1.0f;
        wet.mud_factor = 1.0f; wet.wind_speed_ms = 8.0f;

        float dry_dirt = nl_weather_move_multiplier(&dry, NL_SURF_DIRT);
        float wet_dirt = nl_weather_move_multiplier(&wet, NL_SURF_DIRT);
        printf("    move on dirt: dry=%.2f wet=%.2f\n", dry_dirt, wet_dirt);
        check(wet_dirt < dry_dirt, "wet murram slows movement");
        check(wet_dirt > 0.05f, "mud does not fully freeze movement");

        float dry_wash = nl_weather_earning_multiplier(&dry, NL_JOB_CAR_WASH);
        float wet_wash = nl_weather_earning_multiplier(&wet, NL_JOB_CAR_WASH);
        printf("    car wash earnings: dry=%.2f wet=%.2f\n", dry_wash, wet_wash);
        check(wet_wash < dry_wash, "car wash collapses in rain");

        float dry_water = nl_weather_earning_multiplier(&dry, NL_JOB_HAWK_WATER);
        float wet_water = nl_weather_earning_multiplier(&wet, NL_JOB_HAWK_WATER);
        printf("    hawk water earnings: hot/dry=%.2f rain=%.2f\n",
               dry_water, wet_water);
        check(dry_water > wet_water, "water sells better in hot dry weather");
    }

    /* ---------------------------------------------------------------- */
    printf("\n=== %s ===\n", g_fail == 0 ? "ALL CHECKS PASSED" : "FAILURES PRESENT");
    if (g_fail) printf("%d check(s) failed\n", g_fail);
    return g_fail ? 1 : 0;
}
