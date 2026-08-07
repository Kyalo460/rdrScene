/* nl_econ.c - Nairobi Life: economy, jobs and player-needs simulation.
 *
 * Realistic Nairobi street survival. All money is Kenyan Shillings (KSh).
 * See nl_core.h for the full contract. This module implements the entire
 * "nl_econ.c" section of the API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "nl_core.h"

/* ----------------------------------------------------------------------- */
/*  Local helpers                                                          */
/* ----------------------------------------------------------------------- */

static bool nl_job_is_hawking(NLJobKind k) {
    return k == NL_JOB_HAWK_WATER || k == NL_JOB_HAWK_SWEETS;
}

static bool nl_job_is_customer_facing(NLJobKind k) {
    return k == NL_JOB_HAWK_WATER || k == NL_JOB_HAWK_SWEETS ||
           k == NL_JOB_CAR_WASH   || k == NL_JOB_PARKING_GUIDE ||
           k == NL_JOB_MKOKOTENI  || k == NL_JOB_BEG;
}

/* Place a position near a building of the requested kind, or a district,
 * falling back to a random spot inside the world bounds if none exist. */
static NLVec2 nl_place_near(NLGame *g, NLBuildingKind want_kind,
                            NLDistrict want_dist, float spread) {
    NLVec2 p = { 0.0f, 0.0f };
    bool found = false;

    if (g->building_count > 0) {
        for (int tries = 0; tries < 16; tries++) {
            int bi = (int)nl_rand_range(&g->rng_state, 0, g->building_count - 1);
            NLBuilding *b = &g->buildings[bi];
            if (b->kind == want_kind || b->district == want_dist ||
                want_kind == (NLBuildingKind)-1) {
                p.x = b->pos.x + nl_randf_range(&g->rng_state, -spread, b->size.x + spread);
                p.y = b->pos.y + nl_randf_range(&g->rng_state, -spread, b->size.y + spread);
                found = true;
                break;
            }
        }
    }

    if (!found) {
        p.x = nl_randf_range(&g->rng_state, 100.0f, NL_WORLD_W - 100.0f);
        p.y = nl_randf_range(&g->rng_state, 100.0f, NL_WORLD_H - 100.0f);
    }

    p.x = nl_clampf(p.x, 20.0f, NL_WORLD_W - 20.0f);
    p.y = nl_clampf(p.y, 20.0f, NL_WORLD_H - 20.0f);
    return p;
}

/* ----------------------------------------------------------------------- */
/*  Init                                                                   */
/* ----------------------------------------------------------------------- */

void nl_econ_init(NLGame *g) {
    NLPrices *p = &g->prices;

    p->water_bottle_buy  = 18;   /* wholesale 500ml                     */
    p->water_bottle_sell = 50;   /* hawked in traffic                   */
    p->sweets_pack_buy   = 50;   /* pack of mints/sweets                */
    p->sweets_sell_each  = 8;    /* ~5-10 each resold                   */
    p->scrap_per_kg      = 28;   /* scrap metal at a dealer             */
    p->ugali_beans_meal  = 90;   /* kibandaski: ugali + sukuma + beans  */
    p->chapati           = 20;
    p->mandazi           = 10;
    p->tea               = 25;   /* chai 20-30                          */
    p->matatu_fare       = 60;   /* base; doubles in rain/rush hour     */
    p->boda_fare         = 130;  /* boda boda ride                      */
    p->shack_rent_daily  = 100;  /* 3000/month mabati room in Kibera    */
    p->hostel_bed        = 250;  /* lodging bed for a night             */
    p->public_toilet     = 15;   /* 10-20                              */
    p->clinic_visit      = 500;
    p->malaria_treatment = 1200; /* 1000-1500                         */
    p->phone_charge      = 20;
    p->water_jerrycan    = 10;   /* 20L; spikes in dry-season shortage  */

    g->job_count   = 0;
    g->active_job  = -1;
    g->job_progress = 0.0f;
    g->job_elapsed_hours = 0.0f;
    g->day_earnings = 0;
    g->day_spending = 0;
    g->day_start_cash = g->player.cash;
    g->day_worked_hours = 0.0f;
}

/* ----------------------------------------------------------------------- */
/*  Job generation                                                         */
/* ----------------------------------------------------------------------- */

void nl_jobs_generate(NLGame *g) {
    g->job_count = 0;

    /* Helper macro to append a job. */
#define NL_ADD_JOB() \
    (g->job_count < NL_MAX_JOBS ? &g->jobs[g->job_count++] : NULL)

    NLJob *j;

    /* Hawking bottled water along a road / roadside. */
    j = NL_ADD_JOB();
    if (j) {
        j->kind = NL_JOB_HAWK_WATER;
        j->pos = nl_place_near(g, (NLBuildingKind)-1, NL_DIST_ROADSIDE, 40.0f);
        j->radius = 60.0f;
        j->pay_min = 250;
        j->pay_max = 550;
        j->energy_cost = 9.0f;
        j->duration_hours = 6.0f;
        j->rain_penalty = 0.25f;   /* traffic jams = better hawking, but wet */
        j->heat_risk = 30.0f;      /* illegal street vending, askaris */
        j->available = true;
        j->requires_stock = true;
        snprintf(j->name, sizeof(j->name), "Hawk water");
        snprintf(j->desc, sizeof(j->desc),
                 "Sell 500ml water bottles to matatu & boda passengers stuck in traffic.");
    }

    /* Hawking sweets/mints along a road. */
    j = NL_ADD_JOB();
    if (j) {
        j->kind = NL_JOB_HAWK_SWEETS;
        j->pos = nl_place_near(g, (NLBuildingKind)-1, NL_DIST_ROADSIDE, 30.0f);
        j->radius = 50.0f;
        j->pay_min = 150;
        j->pay_max = 320;
        j->energy_cost = 8.0f;
        j->duration_hours = 6.0f;
        j->rain_penalty = 0.3f;
        j->heat_risk = 30.0f;
        j->available = true;
        j->requires_stock = true;
        snprintf(j->name, sizeof(j->name), "Hawk sweets");
        snprintf(j->desc, sizeof(j->desc),
                 "Walk the jam selling mints and sweets to bored drivers and pedestrians.");
    }

    /* Collect scrap metal near Kibera / market. */
    j = NL_ADD_JOB();
    if (j) {
        j->kind = NL_JOB_COLLECT_SCRAP;
        j->pos = nl_place_near(g, (NLBuildingKind)-1, NL_DIST_KIBERA, 60.0f);
        j->radius = 200.0f;
        j->pay_min = 120;
        j->pay_max = 350;
        j->energy_cost = 11.0f;
        j->duration_hours = 7.0f;
        j->rain_penalty = 0.5f;   /* muddy, fewer buyers */
        j->heat_risk = 0.0f;
        j->available = true;
        j->requires_stock = false;
        snprintf(j->name, sizeof(j->name), "Collect scrap");
        snprintf(j->desc, sizeof(j->desc),
                 "Roam Kibera with a gunia collecting scrap metal & plastic to sell by the kilo.");
    }

    /* Mkokoteni - push a handcart for hire, near market/estate. */
    j = NL_ADD_JOB();
    if (j) {
        j->kind = NL_JOB_MKOKOTENI;
        j->pos = nl_place_near(g, NL_BLD_SHOP, NL_DIST_MARKET, 50.0f);
        j->radius = 120.0f;
        j->pay_min = 200;
        j->pay_max = 420;
        j->energy_cost = 13.0f;
        j->duration_hours = 7.0f;
        j->rain_penalty = 0.45f;
        j->heat_risk = 0.0f;
        j->available = true;
        j->requires_stock = false;
        snprintf(j->name, sizeof(j->name), "Mkokoteni");
        snprintf(j->desc, sizeof(j->desc),
                 "Push a mkokoteni handcart, hauling goods for mama mbogas and shopkeepers.");
    }

    /* Car wash near CBD / estate. */
    j = NL_ADD_JOB();
    if (j) {
        j->kind = NL_JOB_CAR_WASH;
        j->pos = nl_place_near(g, NL_BLD_KIOSK, NL_DIST_ESTATE, 40.0f);
        j->radius = 50.0f;
        j->pay_min = 200;
        j->pay_max = 500;
        j->energy_cost = 10.0f;
        j->duration_hours = 6.0f;
        j->rain_penalty = 0.1f;   /* cars get dirty, but people stay home */
        j->heat_risk = 10.0f;
        j->available = true;
        j->requires_stock = false;
        snprintf(j->name, sizeof(j->name), "Car wash");
        snprintf(j->desc, sizeof(j->desc),
                 "Wash cars at a roadside washbay - 50-150 KSh per car when the traffic flows.");
    }

    /* Parking guide near CBD. */
    j = NL_ADD_JOB();
    if (j) {
        j->kind = NL_JOB_PARKING_GUIDE;
        j->pos = nl_place_near(g, NL_BLD_TOWER, NL_DIST_CBD, 40.0f);
        j->radius = 50.0f;
        j->pay_min = 150;
        j->pay_max = 380;
        j->energy_cost = 7.0f;
        j->duration_hours = 6.0f;
        j->rain_penalty = 0.4f;
        j->heat_risk = 20.0f;     /* makangas & askaris compete */
        j->available = true;
        j->requires_stock = false;
        snprintf(j->name, sizeof(j->name), "Parking guide");
        snprintf(j->desc, sizeof(j->desc),
                 "Wave cars into bays as a makanga and collect 20-50 KSh tips from drivers.");
    }

    /* Loading at Industrial Area. */
    j = NL_ADD_JOB();
    if (j) {
        j->kind = NL_JOB_LOADING;
        j->pos = nl_place_near(g, NL_BLD_WAREHOUSE, NL_DIST_INDUSTRIAL, 60.0f);
        j->radius = 100.0f;
        j->pay_min = 400;
        j->pay_max = 700;
        j->energy_cost = 15.0f;   /* back-breaking labour */
        j->duration_hours = 8.0f;
        j->rain_penalty = 0.5f;
        j->heat_risk = 0.0f;
        j->available = true;
        j->requires_stock = false;
        snprintf(j->name, sizeof(j->name), "Loading");
        snprintf(j->desc, sizeof(j->desc),
                 "Casual loading at Industrial Area godowns - lift sacks until your back gives out.");
    }

    /* Construction (mjengo) near estate / CBD. */
    j = NL_ADD_JOB();
    if (j) {
        j->kind = NL_JOB_CONSTRUCTION;
        j->pos = nl_place_near(g, NL_BLD_APARTMENT, NL_DIST_ESTATE, 50.0f);
        j->radius = 100.0f;
        j->pay_min = 500;
        j->pay_max = 800;
        j->energy_cost = 16.0f;
        j->duration_hours = 8.0f;
        j->rain_penalty = 0.6f;   /* sites shut in heavy rain */
        j->heat_risk = 0.0f;
        j->available = true;
        j->requires_stock = false;
        snprintf(j->name, sizeof(j->name), "Mjengo");
        snprintf(j->desc, sizeof(j->desc),
                 "Casual mjengo labour mixing mortar and carrying blocks. 500-800 KSh if you last the day.");
    }

    /* Begging near CBD / estate. */
    j = NL_ADD_JOB();
    if (j) {
        j->kind = NL_JOB_BEG;
        j->pos = nl_place_near(g, NL_BLD_TOWER, NL_DIST_CBD, 40.0f);
        j->radius = 50.0f;
        j->pay_min = 100;
        j->pay_max = 300;
        j->energy_cost = 4.0f;
        j->duration_hours = 8.0f;
        j->rain_penalty = 0.7f;
        j->heat_risk = 5.0f;
        j->available = true;
        j->requires_stock = false;
        snprintf(j->name, sizeof(j->name), "Beg");
        snprintf(j->desc, sizeof(j->desc),
                 "Sit at a CBD junction with cap out. 100-300 KSh on a good day, nothing on a bad one.");
    }

#undef NL_ADD_JOB
}

/* ----------------------------------------------------------------------- */
/*  Job control                                                            */
/* ----------------------------------------------------------------------- */

bool nl_job_start(NLGame *g, int job_index) {
    if (job_index < 0 || job_index >= g->job_count) return false;
    NLJob *j = &g->jobs[job_index];
    if (!j->available) return false;

    if (j->requires_stock) {
        if (j->kind == NL_JOB_HAWK_WATER && g->player.stock_water <= 0) {
            nl_log_push(&g->log, NL_MSG_WARN, "No water bottles to hawk. Buy stock first.");
            return false;
        }
        if (j->kind == NL_JOB_HAWK_SWEETS && g->player.stock_sweets <= 0) {
            nl_log_push(&g->log, NL_MSG_WARN, "No sweets to hawk. Buy stock first.");
            return false;
        }
    }

    if (g->player.energy < 10.0f) {
        nl_log_push(&g->log, NL_MSG_WARN, "Too exhausted to start %s.", j->name);
        return false;
    }

    g->active_job = job_index;
    g->job_progress = 0.0f;
    g->job_elapsed_hours = 0.0f;
    g->player.is_working = true;
    j->available = false;
    nl_log_push(&g->log, NL_MSG_INFO, "Started hustle: %s.", j->name);
    return true;
}

void nl_job_cancel(NLGame *g, bool quiet) {
    if (g->active_job < 0) return;
    NLJob *j = &g->jobs[g->active_job];
    j->available = true;
    g->active_job = -1;
    g->job_progress = 0.0f;
    g->job_elapsed_hours = 0.0f;
    g->player.is_working = false;
    if (!quiet) {
        nl_log_push(&g->log, NL_MSG_WARN, "Quit %s with nothing earned.", j->name);
    }
}

int nl_job_payout(NLGame *g, const NLJob *job) {
    float base = (float)nl_rand_range(&g->rng_state, job->pay_min, job->pay_max);

    /* Weather multiplier: rain can hurt or help depending on job. */
    float wmult = nl_weather_earning_multiplier(&g->weather, job->kind);

    /* Rush-hour bonus for hawkers - Nairobi jams are prime selling time. */
    float rush = 1.0f;
    if (nl_job_is_hawking(job->kind) && nl_clock_is_rush_hour(&g->clock)) {
        rush = 1.35f;
    }

    /* Morale directly multiplies earnings. */
    float morale = nl_clampf(g->player.morale / 100.0f, 0.3f, 1.3f);

    /* Energy: tired workers earn less. */
    float energy = nl_clampf(g->player.energy / 100.0f, 0.4f, 1.0f);

    /* Reputation: street cred opens doors and tips. */
    float rep = nl_clampf(0.8f + g->player.reputation / 250.0f, 0.8f, 1.2f);

    /* Hygiene penalty for customer-facing jobs. */
    float hygiene = 1.0f;
    if (nl_job_is_customer_facing(job->kind)) {
        hygiene = nl_clampf(0.6f + g->player.hygiene / 250.0f, 0.6f, 1.0f);
    }

    float total = base * wmult * rush * morale * energy * rep * hygiene;
    int pay = (int)(total + 0.5f);
    if (pay < 0) pay = 0;
    return pay;
}

/* ----------------------------------------------------------------------- */
/*  Econ update (active job progression)                                   */
/* ----------------------------------------------------------------------- */

void nl_econ_update(NLGame *g, float dt, float sim_seconds) {
    (void)dt;
    if (g->active_job < 0) return;
    NLJob *j = &g->jobs[g->active_job];
    NLPlayer *p = &g->player;

    float dHours = sim_seconds / 3600.0f;
    g->job_elapsed_hours += dHours;
    g->day_worked_hours  += dHours;
    g->job_progress = nl_clampf(g->job_elapsed_hours / j->duration_hours, 0.0f, 1.0f);

    /* Energy cost scales with how hard the job is. */
    p->energy -= j->energy_cost * dHours;
    if (p->energy < 0.0f) p->energy = 0.0f;

    /* Police heat from hawking / street vending. */
    if (j->heat_risk > 0.0f) {
        g->player.police_heat += j->heat_risk * dHours * 0.5f;
        if (g->player.police_heat > 100.0f) g->player.police_heat = 100.0f;
    }

    /* Hygiene drops faster for dirty jobs. */
    if (j->kind == NL_JOB_COLLECT_SCRAP || j->kind == NL_JOB_LOADING) {
        p->hygiene -= 6.0f * dHours;
    } else {
        p->hygiene -= 2.0f * dHours;
    }
    if (p->hygiene < 0.0f) p->hygiene = 0.0f;

    /* Consume stock for hawking jobs over time. */
    if (j->kind == NL_JOB_HAWK_WATER && p->stock_water > 0) {
        int sold = (int)(2.0f * dHours + nl_randf(&g->rng_state) * 0.5f);
        if (sold > p->stock_water) sold = p->stock_water;
        p->stock_water -= sold;
    }
    if (j->kind == NL_JOB_HAWK_SWEETS && p->stock_sweets > 0) {
        int sold = (int)(3.0f * dHours + nl_randf(&g->rng_state) * 0.5f);
        if (sold > p->stock_sweets) sold = p->stock_sweets;
        p->stock_sweets -= sold;
    }

    /* Cancel automatically if energy is depleted. */
    if (p->energy <= 0.0f) {
        nl_log_push(&g->log, NL_MSG_BAD, "Collapsed from exhaustion - %s abandoned.", j->name);
        nl_job_cancel(g, true);
        return;
    }

    /* Cancel if a heavy storm makes the job impossible. */
    if (g->weather.sky == NL_SKY_HEAVY_RAIN || g->weather.sky == NL_SKY_THUNDERSTORM) {
        if (j->rain_penalty >= 0.5f && g->weather.rain_mm_hr > 8.0f) {
            nl_log_push(&g->log, NL_MSG_WARN,
                        "Storm killed the %s - packed up and went home.", j->name);
            nl_job_cancel(g, true);
            return;
        }
    }

    /* Pay out on completion. */
    if (g->job_progress >= 1.0f) {
        /* For hawkers, only pay if they still have something to show. */
        if (j->requires_stock && p->stock_water <= 0 && p->stock_sweets <= 0) {
            nl_log_push(&g->log, NL_MSG_WARN, "Sold out before finishing %s.", j->name);
        }
        int pay = nl_job_payout(g, j);
        nl_player_earn(g, pay, j->name);
        nl_log_push(&g->log, NL_MSG_MONEY, "Finished %s: earned %d KSh.", j->name, pay);
        j->available = true;
        g->active_job = -1;
        g->job_progress = 0.0f;
        g->job_elapsed_hours = 0.0f;
        g->player.is_working = false;
    }
}

/* ----------------------------------------------------------------------- */
/*  Player needs                                                          */
/* ----------------------------------------------------------------------- */

void nl_player_needs_update(NLGame *g, float sim_seconds) {
    NLPlayer *p = &g->player;
    NLWeather *w = &g->weather;
    float dHours = sim_seconds / 3600.0f;

    bool is_night = !g->clock.is_daylight;

    /* --- Wetness & warmth ------------------------------------------------- */
    float rain = w->rain_mm_hr;
    if (rain > 0.5f && !p->under_roof) {
        p->wetness += nl_clampf(rain * 0.06f * dHours, 0.0f, 1.0f);
    }
    /* Dries slowly; faster in sun and wind. */
    float dry = (0.02f + (1.0f - w->cloud_cover) * 0.05f + w->wind_speed_ms * 0.01f) * dHours;
    p->wetness -= dry;
    if (p->wetness < 0.0f) p->wetness = 0.0f;
    if (p->wetness > 1.0f) p->wetness = 1.0f;

    /* warmth from apparent temperature and wetness. */
    float target_warmth = nl_clampf((w->apparent_c - 8.0f) / 18.0f * 100.0f, 0.0f, 100.0f);
    target_warmth -= p->wetness * 45.0f;
    if (p->under_roof) target_warmth = nl_clampf(target_warmth + 25.0f, 0.0f, 100.0f);
    p->warmth += (target_warmth - p->warmth) * nl_clampf(dHours * 0.5f, 0.0f, 1.0f);
    if (p->warmth < 0.0f) p->warmth = 0.0f;
    if (p->warmth > 100.0f) p->warmth = 100.0f;

    /* --- Hunger: ~5-6 in-game hours to get properly hungry --------------- */
    float hunger_drain = 18.0f * dHours;       /* ~100 over 5.5h */
    if (p->is_working || p->is_sprinting) hunger_drain *= 1.4f;
    p->hunger -= hunger_drain;
    if (p->hunger < 0.0f) p->hunger = 0.0f;
    if (p->hunger > 100.0f) p->hunger = 100.0f;

    /* --- Thirst: faster, especially in heat ------------------------------ */
    float thirst_drain = 16.0f * dHours;
    if (w->temperature_c > 26.0f) {
        thirst_drain *= 1.0f + (w->temperature_c - 26.0f) * 0.06f;
    }
    if (p->is_working || p->is_sprinting) thirst_drain *= 1.5f;
    p->thirst -= thirst_drain;
    if (p->thirst < 0.0f) p->thirst = 0.0f;
    if (p->thirst > 100.0f) p->thirst = 100.0f;

    /* --- Energy: drains with activity, faster when hungry/ill/cold ------- */
    float energy_drain = 3.0f * dHours;        /* idle baseline */
    if (p->is_sprinting) energy_drain += 14.0f * dHours;
    else if (p->is_working) energy_drain += 10.0f * dHours;
    if (p->hunger < 20.0f) energy_drain *= 1.4f;
    if (p->warmth < 25.0f) energy_drain *= 1.3f;
    if (p->illness != NL_ILL_NONE) energy_drain *= 1.25f;
    /* Slow regen when resting, fed and sheltered. */
    if (!p->is_working && !p->is_sprinting && p->hunger > 40.0f &&
        p->thirst > 40.0f && (p->under_roof || !is_night)) {
        energy_drain -= 8.0f * dHours;
    }
    p->energy -= energy_drain;
    if (p->energy < 0.0f) p->energy = 0.0f;
    if (p->energy > 100.0f) p->energy = 100.0f;

    /* --- Bladder fills over time ----------------------------------------- */
    p->bladder += 9.0f * dHours;
    if (p->bladder > 100.0f) {
        p->bladder = 100.0f;
        p->morale -= 6.0f * dHours;   /* desperate, no toilet */
    }
    if (p->bladder < 0.0f) p->bladder = 0.0f;

    /* --- Hygiene slowly drops -------------------------------------------- */
    p->hygiene -= 1.5f * dHours;
    if (p->hygiene < 0.0f) p->hygiene = 0.0f;
    if (p->hygiene > 100.0f) p->hygiene = 100.0f;

    /* --- Health ----------------------------------------------------------- */
    float health_delta = 0.0f;
    if (p->hunger <= 0.0f)    health_delta -= 6.0f * dHours;
    if (p->thirst <= 0.0f)    health_delta -= 10.0f * dHours;
    if (p->injury > 0.0f)     health_delta -= p->injury * 4.0f * dHours;
    if (p->illness != NL_ILL_NONE) {
        health_delta -= (0.5f + p->illness_severity * 5.0f) * dHours;
    }
    /* Recovery: only when fed, hydrated and rested. */
    if (p->hunger > 50.0f && p->thirst > 50.0f && p->energy > 40.0f &&
        p->illness == NL_ILL_NONE && p->injury <= 0.0f) {
        health_delta += 2.0f * dHours;
    }
    p->health += health_delta;
    if (p->health < 0.0f) p->health = 0.0f;
    if (p->health > 100.0f) p->health = 100.0f;

    /* --- Illness progression --------------------------------------------- */
    if (p->illness != NL_ILL_NONE) {
        p->illness_severity += 0.03f * dHours;  /* untreated worsens */
        if (p->illness_severity > 1.0f) p->illness_severity = 1.0f;
        p->energy -= p->illness_severity * 4.0f * dHours;
    } else {
        /* New illness risk. */
        if (p->warmth < 20.0f && p->wetness > 0.5f) {
            if (nl_randf(&g->rng_state) < 0.04f * dHours) {
                p->illness = NL_ILL_COLD;
                p->illness_severity = 0.3f;
                nl_log_push(&g->log, NL_MSG_BAD,
                            "Shivering and soaked - you've caught a cold.");
            }
        }
        /* Nairobi sits at ~1,795 m and is classified negligible-risk for
         * malaria, so mosquitoes are NOT a realistic street hazard here.
         * The real health risk after heavy rain is waterborne: open drains
         * and pit latrines overflow into the footpaths of the informal
         * settlements, which is what actually drives cholera outbreaks. */
        if (w->ground_wetness > 0.6f &&
            nl_world_district_at(g, p->pos) == NL_DIST_KIBERA &&
            p->hygiene < 40.0f &&
            nl_randf(&g->rng_state) < 0.012f * dHours) {
            p->illness = NL_ILL_CHOLERA;
            p->illness_severity = 0.3f;
            nl_log_push(&g->log, NL_MSG_BAD,
                        "The drains have overflowed. Stomach cramps set in.");
        }
    }

    /* --- Morale ----------------------------------------------------------- */
    float morale_delta = 0.0f;
    if (p->hunger < 20.0f) morale_delta -= 3.0f * dHours;
    if (p->thirst < 20.0f) morale_delta -= 3.0f * dHours;
    if (p->wetness > 0.6f && !p->under_roof) morale_delta -= 4.0f * dHours;
    if (p->bladder >= 100.0f) morale_delta -= 3.0f * dHours;
    if (p->debt > 0) morale_delta -= 1.0f * dHours;
    if (p->warmth > 50.0f && p->under_roof && !is_night) morale_delta += 1.0f * dHours;
    p->morale += morale_delta;
    if (p->morale < 0.0f) p->morale = 0.0f;
    if (p->morale > 100.0f) p->morale = 100.0f;

    /* --- Police heat decays slowly --------------------------------------- */
    g->player.police_heat -= 1.5f * dHours;
    if (g->player.police_heat < 0.0f) g->player.police_heat = 0.0f;
}

/* ----------------------------------------------------------------------- */
/*  Money                                                                  */
/* ----------------------------------------------------------------------- */

void nl_player_spend(NLGame *g, int amount, const char *what) {
    if (amount <= 0) return;
    int pay = amount;
    if (g->player.cash >= pay) {
        g->player.cash -= pay;
    } else if (g->player.cash + g->player.mpesa >= pay) {
        int from_cash = g->player.cash;
        g->player.cash = 0;
        g->player.mpesa -= (pay - from_cash);
        if (g->player.mpesa < 0) g->player.mpesa = 0;
    } else {
        g->player.cash = 0;
        g->player.mpesa = 0;
        g->player.debt += pay;     /* forced into debt (shylock) */
        nl_log_push(&g->log, NL_MSG_BAD, "No money for %s - now in debt %d KSh.",
                    what, g->player.debt);
    }
    g->day_spending += pay;
}

void nl_player_earn(NLGame *g, int amount, const char *what) {
    (void)what;
    if (amount <= 0) return;
    g->player.cash += amount;
    g->day_earnings += amount;
    /* Earning lifts morale a little. */
    g->player.morale = nl_clampf(g->player.morale + (float)amount * 0.01f, 0.0f, 100.0f);
}

/* ----------------------------------------------------------------------- */
/*  Eating                                                                 */
/* ----------------------------------------------------------------------- */

bool nl_player_eat(NLGame *g, int meal_index) {
    NLPlayer *p = &g->player;
    NLPrices *pr = &g->prices;
    int cost;
    int hunger_restore;
    int thirst_restore = 0;
    bool cheap = false;
    const char *label;

    switch (meal_index) {
        case 0: /* ugali + beans (sukuma) at a kibanda */
            cost = pr->ugali_beans_meal;
            hunger_restore = 55;
            label = "ugali & beans";
            break;
        case 1: /* chapati */
            cost = pr->chapati;
            hunger_restore = 22;
            cheap = true;
            label = "chapati";
            break;
        case 2: /* mandazi */
            cost = pr->mandazi;
            hunger_restore = 12;
            cheap = true;
            label = "mandazi";
            break;
        case 3: /* tea (chai) */
            cost = pr->tea;
            hunger_restore = 8;
            thirst_restore = 18;
            label = "chai";
            break;
        default:
            return false;
    }

    if (g->player.cash < cost && g->player.mpesa < cost) {
        nl_log_push(&g->log, NL_MSG_WARN, "Can't afford %s (%d KSh).", label, cost);
        return false;
    }

    nl_player_spend(g, cost, label);
    p->hunger = nl_clampf(p->hunger + (float)hunger_restore, 0.0f, 100.0f);
    if (thirst_restore > 0)
        p->thirst = nl_clampf(p->thirst + (float)thirst_restore, 0.0f, 100.0f);
    p->last_meal_hour = g->clock.hour;

    nl_log_push(&g->log, NL_MSG_GOOD, "Ate %s for %d KSh. (-%d hunger)", label, cost,
                hunger_restore);

    /* Morale bump from a proper meal. */
    if (meal_index == 0) {
        p->morale = nl_clampf(p->morale + 8.0f, 0.0f, 100.0f);
    }

    /* Cheapest meals carry a food-poison risk. */
    if (cheap && nl_randf(&g->rng_state) < 0.06f && p->illness == NL_ILL_NONE) {
        p->illness = NL_ILL_FOOD_POISON;
        p->illness_severity = 0.25f;
        nl_log_push(&g->log, NL_MSG_BAD, "That %s was off - food poisoning!", label);
    }

    /* Cholera risk from unsafe cheap water implied when very low hygiene. */
    if (meal_index == 3 && p->hygiene < 25.0f &&
        nl_randf(&g->rng_state) < 0.03f && p->illness == NL_ILL_NONE) {
        p->illness = NL_ILL_CHOLERA;
        p->illness_severity = 0.3f;
        nl_log_push(&g->log, NL_MSG_BAD, "Dirty cup - cholera cramps hit you.");
    }

    return true;
}

/* ----------------------------------------------------------------------- */
/*  Sleeping                                                               */
/* ----------------------------------------------------------------------- */

void nl_player_sleep(NLGame *g, float hours, bool sheltered) {
    NLPlayer *p = &g->player;
    NLWeather *w = &g->weather;

    float restore = hours * 25.0f;   /* per hour when sheltered & dry */

    if (!sheltered) {
        restore *= 0.35f;            /* rough sleep is poor */
        /* Rain while sleeping rough is nearly useless and risky. */
        if (w->rain_mm_hr > 0.5f) {
            restore *= 0.2f;
            p->wetness = nl_clampf(p->wetness + 0.4f, 0.0f, 1.0f);
            if (p->illness == NL_ILL_NONE && nl_randf(&g->rng_state) < 0.10f * hours) {
                p->illness = NL_ILL_COLD;
                p->illness_severity = 0.3f;
                nl_log_push(&g->log, NL_MSG_BAD,
                            "Slept in the rain - woke up sick.");
            }
        }
        p->morale = nl_clampf(p->morale - 3.0f * hours, 0.0f, 100.0f);
    } else {
        p->morale = nl_clampf(p->morale + 4.0f * hours, 0.0f, 100.0f);
        if (w->rain_mm_hr <= 0.5f) p->wetness = nl_clampf(p->wetness - 0.3f, 0.0f, 1.0f);
    }

    p->energy = nl_clampf(p->energy + restore, 0.0f, 100.0f);
    p->sleep_debt = nl_clampf(p->sleep_debt - hours, 0.0f, 24.0f);

    /* Sleeping roughly advances the day clock in the real game loop, but we
     * also reduce bladder a touch and restore a little health if rested. */
    if (sheltered && p->hunger > 30.0f && p->thirst > 30.0f) {
        p->health = nl_clampf(p->health + 2.0f * hours, 0.0f, 100.0f);
    }

    nl_log_push(&g->log, NL_MSG_INFO,
                sheltered ? "Slept sheltered for %.1f h (+%.0f energy)."
                          : "Slept rough for %.1f h (poor rest).",
                hours, restore);
}

/* ----------------------------------------------------------------------- */
/*  New day                                                                */
/* ----------------------------------------------------------------------- */

void nl_player_new_day(NLGame *g) {
    NLPlayer *p = &g->player;

    g->player.days_survived += 1;
    p->days_survived = g->player.days_survived;

    /* Charge daily rent if the player has shelter. */
    if (p->has_shelter_tonight) {
        int rent = g->prices.shack_rent_daily;
        if (g->player.cash >= rent) {
            nl_player_spend(g, rent, "rent");
            nl_log_push(&g->log, NL_MSG_MONEY, "Paid daily rent %d KSh.", rent);
        } else {
            g->player.debt += rent;
            g->day_spending += rent;
            nl_log_push(&g->log, NL_MSG_BAD,
                        "Couldn't pay %d KSh rent - debt now %d KSh.", rent,
                        g->player.debt);
        }
    }

    /* Sleep debt grows if energy was low at day end. */
    if (p->energy < 50.0f) {
        p->sleep_debt = nl_clampf(p->sleep_debt + 4.0f, 0.0f, 24.0f);
    }

    nl_log_push(&g->log, NL_MSG_INFO,
                "Day %d done. Earned %d KSh, spent %d KSh, debt %d KSh.",
                g->player.days_survived, g->day_earnings, g->day_spending,
                g->player.debt);

    /* Reset day stats. */
    g->day_earnings = 0;
    g->day_spending = 0;
    g->day_start_cash = g->player.cash;
    g->day_worked_hours = 0.0f;
}

/* ----------------------------------------------------------------------- */
/*  Name tables                                                            */
/* ----------------------------------------------------------------------- */

const char *nl_job_kind_name(NLJobKind k) {
    switch (k) {
        case NL_JOB_NONE:         return "None";
        case NL_JOB_HAWK_WATER:   return "Hawk water";
        case NL_JOB_HAWK_SWEETS:  return "Hawk sweets";
        case NL_JOB_COLLECT_SCRAP:return "Collect scrap";
        case NL_JOB_MKOKOTENI:    return "Mkokoteni";
        case NL_JOB_CAR_WASH:     return "Car wash";
        case NL_JOB_PARKING_GUIDE:return "Parking guide";
        case NL_JOB_LOADING:      return "Loading";
        case NL_JOB_CONSTRUCTION: return "Mjengo";
        case NL_JOB_BEG:          return "Beg";
        default:                  return "Unknown";
    }
}

const char *nl_illness_name(NLIllness i) {
    switch (i) {
        case NL_ILL_NONE:       return "Healthy";
        case NL_ILL_COLD:       return "Cold / flu";
        case NL_ILL_FOOD_POISON:return "Food poisoning";
        case NL_ILL_MALARIA:    return "Malaria";
        case NL_ILL_CHOLERA:    return "Cholera";
        default:                return "Unknown";
    }
}
