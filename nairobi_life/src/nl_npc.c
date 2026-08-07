/* nl_npc.c - Nairobi Life: street-life NPCs and road traffic.
 *
 * Two modules live here (per the contract): the nl_npc.c section and the
 * nl_vehicle.c (inside nl_npc.c) section.
 *
 * The NPCs are ordinary Nairobians: pedestrians streaming to the CBD at rush
 * hour, hawkers in the market, matatu touts, police, county askaris, thugs that
 * come out after dark, and street kids. They react hard to weather - when the
 * rain starts they sprint for the nearest awning unless they carry an umbrella.
 *
 * Vehicles (matatus, bodas, cars, lorries, handcarts) follow the roads[] network
 * and grind to gridlock in rush hour. Nothing ever drives into Kibera's narrow
 * unpaved paths.
 *
 * Performance: 512 NPCs and 160 vehicles at 144 FPS. We never do O(n^2):
 *  - collision/blocking uses the world spatial grid (nl_world_blocked)
 *  - each NPC re-plans (picks a new target / re-evaluates weather) only every
 *    ~30 frames, offset by its own index, so the expensive decisions are
 *    staggered across the frame budget.
 *  - nearest-NPC search caps its linear scan and early-outs by distance.
 *
 * Pure C99 simulation. No raylib.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "nl_core.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ------------------------------------------------------------------ */
/*  Name + appearance tables                                          */
/* ------------------------------------------------------------------ */

static const char *NL_NAMES[] = {
    "Wanjiku", "Otieno", "Kamau", "Achieng", "Mutua", "Njeri", "Kiprop",
    "Wafula", "Akinyi", "Omondi", "Mwangi", "Chebet", "Barasa", "Nyokabi",
    "Kipchoge", "Atieno", "Njoroge", "Wairimu", "Odhiambo", "Muthoni",
    "Simiyu", "Kerubo", "Owino", "Naisula", "Gichuki", "Naomi"
};
static const int NL_NAME_COUNT = (int)(sizeof(NL_NAMES) / sizeof(NL_NAMES[0]));

/* Realistic Kenyan skin tones (dark brown range) as RGB. */
static const uint8_t NL_SKIN[][3] = {
    { 60, 38, 26 }, { 75, 48, 32 }, { 90, 60, 40 },
    { 50, 32, 22 }, { 105, 70, 48 }
};
static const int NL_SKIN_COUNT = (int)(sizeof(NL_SKIN) / sizeof(NL_SKIN[0]));

/* Clothing colours - varied, not caricature. */
static const uint8_t NL_CLOTH[][3] = {
    { 180, 40, 40 }, { 40, 90, 160 }, { 220, 200, 60 }, { 60, 140, 70 },
    { 200, 120, 40 }, { 150, 60, 150 }, { 80, 80, 90 }, { 210, 210, 210 },
    { 30, 100, 120 }, { 190, 80, 30 }
};
static const int NL_CLOTH_COUNT = (int)(sizeof(NL_CLOTH) / sizeof(NL_CLOTH[0]));

/* Re-plan interval in frames (staggered by index). */
#define NL_REPLAN_FRAMES 30

/* Local road-proximity check (nl_world.c keeps its own private version). */
static bool nl_npc_road_near(const NLGame *g, NLVec2 p, float dist)
{
    int i;
    for (i = 0; i < g->road_count; ++i) {
        const NLRoad *r = &g->roads[i];
        NLVec2 ab = { r->b.x - r->a.x, r->b.y - r->a.y };
        float len2 = ab.x * ab.x + ab.y * ab.y;
        float t;
        NLVec2 proj;
        float d;
        if (len2 < 1e-6f) continue;
        t = ((p.x - r->a.x) * ab.x + (p.y - r->a.y) * ab.y) / len2;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        proj.x = r->a.x + ab.x * t;
        proj.y = r->a.y + ab.y * t;
        d = nl_vec_dist(p, proj);
        if (d < dist + r->width * 0.5f) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  Spawning                                                          */
/* ------------------------------------------------------------------ */

static void nl_npc_pick_spawn(NLGame *g, NLNpc *n, NLNpcKind kind)
{
    NLVec2 p;
    int tries = 0;
    NLDistrict want = NL_DIST_CBD;

    switch (kind) {
        case NL_NPC_OFFICE_WORKER: want = NL_DIST_CBD; break;
        case NL_NPC_HAWKER:        want = NL_DIST_MARKET; break;
        case NL_NPC_SHOPKEEPER:    want = NL_DIST_MARKET; break;
        case NL_NPC_MATATU_TOUT:   want = NL_DIST_CBD; break;
        case NL_NPC_POLICE:        want = NL_DIST_CBD; break;
        case NL_NPC_ASKARI:        want = NL_DIST_CBD; break;
        case NL_NPC_THUG:          want = NL_DIST_KIBERA; break;
        case NL_NPC_STREET_KID:    want = NL_DIST_KIBERA; break;
        case NL_NPC_BODA_RIDER:    want = NL_DIST_ROADSIDE; break;
        case NL_NPC_PEDESTRIAN:
        default:                   want = NL_DIST_ESTATE; break;
    }

    do {
        p.x = nl_randf_range(&g->rng_state, 0.0f, NL_WORLD_W);
        p.y = nl_randf_range(&g->rng_state, 0.0f, NL_WORLD_H);
        tries++;
    } while (nl_world_district_at(g, p) != want && tries < 30);

    n->pos = p;
    n->target = p;
    n->state = NL_AI_IDLE;
    n->state_timer = 0.0f;
    n->patience = nl_randf_range(&g->rng_state, 4.0f, 14.0f);
    n->bob_phase = nl_randf(&g->rng_state) * M_PI * 2.0f;
    n->facing = nl_randf(&g->rng_state) * M_PI * 2.0f;
    n->active = true;
    n->has_umbrella = (nl_randf(&g->rng_state) < 0.12f);
    n->knows_player = false;
    n->health = 100;
    n->kind = kind;

    {
        int si = nl_rand_range(&g->rng_state, 0, NL_SKIN_COUNT - 1);
        int ci = nl_rand_range(&g->rng_state, 0, NL_CLOTH_COUNT - 1);
        n->skin_r = NL_SKIN[si][0];
        n->skin_g = NL_SKIN[si][1];
        n->skin_b = NL_SKIN[si][2];
        n->cloth_r = NL_CLOTH[ci][0];
        n->cloth_g = NL_CLOTH[ci][1];
        n->cloth_b = NL_CLOTH[ci][2];
        strncpy(n->name, NL_NAMES[nl_rand_range(&g->rng_state, 0, NL_NAME_COUNT - 1)],
                sizeof(n->name) - 1);
        n->name[sizeof(n->name) - 1] = '\0';
    }

    /* Per-kind tuning. */
    switch (kind) {
        case NL_NPC_OFFICE_WORKER:
            n->wealth_level = nl_randf_range(&g->rng_state, 0.7f, 1.0f);
            n->aggression = nl_randf_range(&g->rng_state, 0.0f, 0.2f);
            n->speed = nl_randf_range(&g->rng_state, 1.2f, 1.6f);
            break;
        case NL_NPC_HAWKER:
            n->wealth_level = nl_randf_range(&g->rng_state, 0.2f, 0.5f);
            n->aggression = nl_randf_range(&g->rng_state, 0.3f, 0.6f);
            n->speed = nl_randf_range(&g->rng_state, 1.0f, 1.4f);
            break;
        case NL_NPC_STREET_KID:
            n->wealth_level = nl_randf_range(&g->rng_state, 0.0f, 0.15f);
            n->aggression = nl_randf_range(&g->rng_state, 0.2f, 0.5f);
            n->speed = nl_randf_range(&g->rng_state, 1.3f, 1.8f);
            break;
        case NL_NPC_THUG:
            n->wealth_level = nl_randf_range(&g->rng_state, 0.1f, 0.4f);
            n->aggression = nl_randf_range(&g->rng_state, 0.7f, 1.0f);
            n->speed = nl_randf_range(&g->rng_state, 1.4f, 1.9f);
            break;
        case NL_NPC_POLICE:
        case NL_NPC_ASKARI:
            n->wealth_level = nl_randf_range(&g->rng_state, 0.4f, 0.7f);
            n->aggression = nl_randf_range(&g->rng_state, 0.4f, 0.8f);
            n->speed = nl_randf_range(&g->rng_state, 1.3f, 1.7f);
            break;
        case NL_NPC_MATATU_TOUT:
            n->wealth_level = nl_randf_range(&g->rng_state, 0.2f, 0.5f);
            n->aggression = nl_randf_range(&g->rng_state, 0.5f, 0.9f);
            n->speed = nl_randf_range(&g->rng_state, 1.4f, 1.9f);
            break;
        case NL_NPC_BODA_RIDER:
            n->wealth_level = nl_randf_range(&g->rng_state, 0.2f, 0.5f);
            n->aggression = nl_randf_range(&g->rng_state, 0.3f, 0.6f);
            n->speed = nl_randf_range(&g->rng_state, 1.5f, 2.0f);
            break;
        case NL_NPC_SHOPKEEPER:
        case NL_NPC_PEDESTRIAN:
        default:
            n->wealth_level = nl_randf_range(&g->rng_state, 0.2f, 0.7f);
            n->aggression = nl_randf_range(&g->rng_state, 0.0f, 0.4f);
            n->speed = nl_randf_range(&g->rng_state, 1.0f, 1.6f);
            break;
    }
}

void nl_npc_spawn_all(NLGame *g)
{
    int i;
    int counts[NL_NPC_COUNT];
    int total = 0;

    if (g == NULL) return;

    /* Population distribution by kind. */
    counts[NL_NPC_PEDESTRIAN]   = 180;
    counts[NL_NPC_HAWKER]       = 60;
    counts[NL_NPC_SHOPKEEPER]   = 30;
    counts[NL_NPC_MATATU_TOUT]  = 24;
    counts[NL_NPC_POLICE]       = 12;
    counts[NL_NPC_ASKARI]       = 14;
    counts[NL_NPC_THUG]         = 40;
    counts[NL_NPC_STREET_KID]   = 50;
    counts[NL_NPC_OFFICE_WORKER]= 70;
    counts[NL_NPC_BODA_RIDER]   = 32;

    for (i = 0; i < NL_NPC_COUNT; ++i) total += counts[i];
    if (total > NL_MAX_NPCS) total = NL_MAX_NPCS;

    g->npc_count = 0;
    for (i = 0; i < NL_NPC_COUNT && g->npc_count < NL_MAX_NPCS; ++i) {
        int n = counts[i];
        int k;
        for (k = 0; k < n && g->npc_count < NL_MAX_NPCS; ++k) {
            nl_npc_pick_spawn(g, &g->npcs[g->npc_count], (NLNpcKind)i);
            g->npc_count++;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Target selection                                                  */
/* ------------------------------------------------------------------ */

static NLVec2 nl_npc_find_shelter(const NLGame *g, NLVec2 p)
{
    int i;
    NLVec2 best = p;
    float best_d = 1e9f;

    for (i = 0; i < g->building_count; ++i) {
        const NLBuilding *b = &g->buildings[i];
        if (!b->enterable) continue;
        NLVec2 c = { b->pos.x + b->size.x * 0.5f, b->pos.y + b->size.y * 0.5f };
        float d = nl_vec_dist(p, c);
        if (d < best_d) { best_d = d; best = c; }
    }
    return best;
}

static void nl_npc_new_target(NLGame *g, NLNpc *n)
{
    /* Choose a plausible destination on a road or plaza, then walk toward it. */
    NLDistrict d = nl_world_district_at(g, n->pos);
    NLVec2 t;
    int tries = 0;

    (void)d;
    do {
        t.x = nl_randf_range(&g->rng_state, 0.0f, NL_WORLD_W);
        t.y = nl_randf_range(&g->rng_state, 0.0f, NL_WORLD_H);
        tries++;
    } while (!nl_npc_road_near(g, t, 8.0f) && tries < 20);

    n->target = t;
    n->state = NL_AI_WALKING;
}

/* ------------------------------------------------------------------ */
/*  Per-NPC update                                                   */
/* ------------------------------------------------------------------ */

static void nl_npc_replan(NLGame *g, NLNpc *n, const NLWeather *w,
                          bool rush, bool night)
{
    NLDistrict d = nl_world_district_at(g, n->pos);

    /* Weather reaction: run for shelter unless carrying an umbrella. */
    if (w->rain_mm_hr > 2.0f && !n->has_umbrella) {
        n->state = NL_AI_SHELTERING;
        n->target = nl_npc_find_shelter(g, n->pos);
        n->speed = nl_randf_range(&g->rng_state, 2.4f, 3.2f); /* sprint */
        return;
    }
    if (w->rain_mm_hr > 2.0f && n->has_umbrella) {
        /* Keep walking, but slower. */
        if (n->state == NL_AI_SHELTERING || n->state == NL_AI_IDLE)
            nl_npc_new_target(g, n);
        n->speed = nl_clampf(n->speed * 0.7f, 0.6f, 1.6f);
        return;
    }

    /* Time-of-day behaviour. */
    switch (n->kind) {
        case NL_NPC_THUG:
            if (night ||
                (d == NL_DIST_KIBERA && !g->clock.is_daylight) ||
                nl_vec_dist(n->pos, g->player.pos) > 600.0f) {
                if (nl_vec_dist(n->pos, g->player.pos) < 350.0f &&
                    g->player.police_heat < 30.0f) {
                    n->state = NL_AI_CHASING;
                    n->target = g->player.pos;
                } else if (n->state != NL_AI_CHASING) {
                    nl_npc_new_target(g, n);
                }
            } else {
                /* day: lie low */
                n->state = NL_AI_IDLE;
            }
            break;

        case NL_NPC_ASKARI:
            if (d == NL_DIST_CBD || d == NL_DIST_MARKET) {
                if (g->player.police_heat > 40.0f) {
                    n->state = NL_AI_HARASSING;
                    n->target = g->player.pos;       /* confiscate goods */
                } else {
                    nl_npc_new_target(g, n);          /* patrol */
                }
            } else if (nl_vec_dist(n->pos, g->player.pos) > 500.0f) {
                n->active = false;   /* only active in CBD/market */
            } else {
                n->active = true;
                nl_npc_new_target(g, n);
            }
            break;

        case NL_NPC_POLICE:
            if (nl_vec_dist(n->pos, g->player.pos) < 200.0f &&
                g->player.police_heat > 25.0f) {
                n->state = NL_AI_CHASING;
                n->target = g->player.pos;
            } else if (n->state != NL_AI_CHASING) {
                nl_npc_new_target(g, n);
            }
            break;

        case NL_NPC_HAWKER:
        case NL_NPC_SHOPKEEPER:
            if (rush || nl_clock_is_rush_hour(&g->clock)) {
                nl_npc_new_target(g, n);
            } else if (n->state == NL_AI_IDLE) {
                nl_npc_new_target(g, n);
            }
            break;

        case NL_NPC_MATATU_TOUT:
            /* Lurk at CBD stage, shout for passengers. */
            n->state = NL_AI_TALKING;
            {
                NLVec2 t = { 2000.0f, 1950.0f };
                n->target = t;
            }
            break;

        case NL_NPC_BODA_RIDER:
            if (!nl_npc_road_near(g, n->pos, 10.0f)) nl_npc_new_target(g, n);
            else if (n->state == NL_AI_IDLE) nl_npc_new_target(g, n);
            break;

        case NL_NPC_OFFICE_WORKER:
            if (rush) nl_npc_new_target(g, n);     /* commute */
            else if (n->state == NL_AI_IDLE) nl_npc_new_target(g, n);
            break;

        case NL_NPC_STREET_KID:
            if (night) nl_npc_new_target(g, n);
            else if (n->state == NL_AI_IDLE) nl_npc_new_target(g, n);
            break;

        case NL_NPC_PEDESTRIAN:
        default:
            if (n->state == NL_AI_IDLE || n->state == NL_AI_SHELTERING)
                nl_npc_new_target(g, n);
            break;
    }
}

void nl_npc_update(NLGame *g, float dt, float sim_seconds)
{
    int i;
    const NLWeather *w = &g->weather;
    bool rush = nl_clock_is_rush_hour(&g->clock);
    bool night = !g->clock.is_daylight;
    float move_mult;

    (void)sim_seconds;

    move_mult = nl_weather_move_multiplier(w, NL_SURF_TARMAC);

    for (i = 0; i < g->npc_count; ++i) {
        NLNpc *n = &g->npcs[i];
        NLVec2 to_t;
        float dist;
        NLVec2 step;
        NLVec2 desired;
        NLSurface surf;

        if (!n->active) continue;

        /* Stagger expensive re-planning across frames. */
        if ((g->frame_counter + i) % NL_REPLAN_FRAMES == 0) {
            nl_npc_replan(g, n, w, rush, night);
        }

        /* Sheltering: stay put under roof once reached. */
        if (n->state == NL_AI_SHELTERING) {
            dist = nl_vec_dist(n->pos, n->target);
            if (dist < 6.0f) {
                n->vel.x = 0.0f; n->vel.y = 0.0f;
                /* Once rain stops, leave shelter. */
                if (w->rain_mm_hr < 1.0f) {
                    n->state = NL_AI_IDLE;
                    nl_npc_new_target(g, n);
                }
                continue;
            }
        }

        to_t.x = n->target.x - n->pos.x;
        to_t.y = n->target.y - n->pos.y;
        dist = sqrtf(to_t.x * to_t.x + to_t.y * to_t.y);
        if (dist < 3.0f) {
            /* Arrived: pick a new destination (or idle briefly). */
            n->state_timer += dt;
            if (n->state_timer > n->patience || n->state == NL_AI_CHASING ||
                n->state == NL_AI_HARASSING) {
                n->state_timer = 0.0f;
                if (n->kind == NL_NPC_MATATU_TOUT) {
                    n->state = NL_AI_TALKING;
                } else if (n->kind == NL_NPC_ASKARI && g->player.police_heat > 40.0f) {
                    n->state = NL_AI_HARASSING;
                    n->target = g->player.pos;
                } else {
                    nl_npc_new_target(g, n);
                }
            } else {
                n->vel.x = 0.0f; n->vel.y = 0.0f;
                if (n->state != NL_AI_TALKING && n->state != NL_AI_WORKING &&
                    n->state != NL_AI_BUYING && n->state != NL_AI_HARASSING)
                    n->state = NL_AI_IDLE;
                continue;
            }
        }

        desired = nl_vec_norm(to_t);
        surf = nl_world_surface_at(g, n->pos);

        /* Effective speed: base * weather movement multiplier * rush penalty. */
        {
            float sp = n->speed * move_mult;
            if (rush && surf == NL_SURF_TARMAC) sp *= 0.7f;
            if (n->state == NL_AI_CHASING || n->state == NL_AI_HARASSING)
                sp *= 1.15f;

            step.x = desired.x * sp * dt;
            step.y = desired.y * sp * dt;

            /* Collision avoidance: if next position is blocked, slide & re-target. */
            {
                NLVec2 nx = { n->pos.x + step.x, n->pos.y + step.y };
                if (nl_world_blocked(g, nx, 1.2f)) {
                    /* Try to steer around: nudge sideways. */
                    NLVec2 perp = { -desired.y, desired.x };
                    float sd = sp * dt;
                    nx.x = n->pos.x + perp.x * sd;
                    nx.y = n->pos.y + perp.y * sd;
                    if (nl_world_blocked(g, nx, 1.2f)) {
                        nl_npc_new_target(g, n);
                        continue;
                    }
                    step = (NLVec2){ nx.x - n->pos.x, nx.y - n->pos.y };
                }
            }

            n->pos.x += step.x;
            n->pos.y += step.y;
            n->vel = step;
            if (dist > 1e-3f) n->facing = atan2f(desired.y, desired.x);
            n->bob_phase += sp * 6.0f * dt;
        }

        /* Animate walk cycle only when moving. */
        if (n->state == NL_AI_IDLE || n->state == NL_AI_TALKING ||
            n->state == NL_AI_WORKING || n->state == NL_AI_BUYING) {
            n->bob_phase += dt * 1.0f;
        }

        /* Keep inside world. */
        if (n->pos.x < 2.0f) n->pos.x = 2.0f;
        if (n->pos.y < 2.0f) n->pos.y = 2.0f;
        if (n->pos.x > NL_WORLD_W - 2.0f) n->pos.x = NL_WORLD_W - 2.0f;
        if (n->pos.y > NL_WORLD_H - 2.0f) n->pos.y = NL_WORLD_H - 2.0f;
    }
}

const char *nl_npc_kind_name(NLNpcKind k)
{
    switch (k) {
        case NL_NPC_PEDESTRIAN:  return "Pedestrian";
        case NL_NPC_HAWKER:      return "Hawker";
        case NL_NPC_SHOPKEEPER:  return "Shopkeeper";
        case NL_NPC_MATATU_TOUT: return "Matatu Tout";
        case NL_NPC_POLICE:      return "Police";
        case NL_NPC_ASKARI:      return "County Askari";
        case NL_NPC_THUG:        return "Thug";
        case NL_NPC_STREET_KID:  return "Street Kid";
        case NL_NPC_OFFICE_WORKER: return "Office Worker";
        case NL_NPC_BODA_RIDER:  return "Boda Rider";
        default:                 return "NPC";
    }
}

int nl_npc_nearest(const NLGame *g, NLVec2 p, float max_dist)
{
    int i;
    int best_i = -1;
    float best_d = (max_dist > 0.0f) ? max_dist : 1e9f;

    for (i = 0; i < g->npc_count; ++i) {
        float d;
        if (!g->npcs[i].active) continue;
        d = nl_vec_dist(p, g->npcs[i].pos);
        if (d < best_d) { best_d = d; best_i = i; }
    }
    return best_i;
}

/* ===================================================================== */
/*  nl_vehicle.c (inside nl_npc.c)                                       */
/* ===================================================================== */

static int nl_vehicle_road_count_paved(const NLGame *g)
{
    int i, n = 0;
    for (i = 0; i < g->road_count; ++i) {
        if (g->roads[i].paved && g->roads[i].traffic_density > 0) n++;
    }
    return n;
}

void nl_vehicle_spawn_all(NLGame *g)
{
    int i;
    int n_paved;

    if (g == NULL) return;

    g->vehicle_count = 0;
    n_paved = nl_vehicle_road_count_paved(g);
    if (n_paved <= 0) return;

    /* Distribute vehicles across paved, traffic-bearing roads. */
    for (i = 0; i < NL_MAX_VEHICLES && g->vehicle_count < NL_MAX_VEHICLES; ++i) {
        NLVehicle *v = &g->vehicles[g->vehicle_count];
        NLVehicleKind kind;
        int ri;
        int guard = 0;

        /* Kind mix: matatus & bodas dominate. */
        {
            float f = nl_randf(&g->rng_state);
            if (f < 0.40f) kind = NL_VEH_MATATU;
            else if (f < 0.65f) kind = NL_VEH_BODA;
            else if (f < 0.82f) kind = NL_VEH_CAR;
            else if (f < 0.93f) kind = NL_VEH_HANDCART;
            else kind = NL_VEH_LORRY;
        }

        /* Pick a paved road with traffic. Lorries only industrial. */
        do {
            ri = nl_rand_range(&g->rng_state, 0, g->road_count - 1);
            guard++;
        } while (guard < 40 &&
                 (!g->roads[ri].paved ||
                  g->roads[ri].traffic_density <= 0 ||
                  (kind == NL_VEH_LORRY &&
                   nl_world_district_at(g, (NLVec2){
                       (g->roads[ri].a.x + g->roads[ri].b.x) * 0.5f,
                       (g->roads[ri].a.y + g->roads[ri].b.y) * 0.5f }) != NL_DIST_INDUSTRIAL)));

        memset(v, 0, sizeof(*v));
        v->kind = kind;
        v->road_index = ri;
        v->road_t = nl_randf(&g->rng_state);
        v->direction = (nl_randf(&g->rng_state) < 0.5f) ? 1 : -1;
        v->active = true;
        v->headlights = false;
        v->honking = false;
        v->honk_timer = 0.0f;

        {
            NLVec2 a = g->roads[ri].a, b = g->roads[ri].b;
            float t = v->road_t;
            if (v->direction < 0) t = 1.0f - t;
            v->pos.x = a.x + (b.x - a.x) * t;
            v->pos.y = a.y + (b.y - a.y) * t;
            v->angle = atan2f((b.y - a.y) * v->direction, (b.x - a.x) * v->direction);
        }

        switch (kind) {
            case NL_VEH_MATATU:   v->speed = nl_randf_range(&g->rng_state, 8.0f, 12.0f);
                                  v->r = 180; v->g = 40; v->b = 40; break;
            case NL_VEH_BODA:     v->speed = nl_randf_range(&g->rng_state, 10.0f, 16.0f);
                                  v->r = 60; v->g = 60; v->b = 60; break;
            case NL_VEH_CAR:      v->speed = nl_randf_range(&g->rng_state, 9.0f, 14.0f);
                                  v->r = 90; v->g = 90; v->b = 100; break;
            case NL_VEH_LORRY:    v->speed = nl_randf_range(&g->rng_state, 6.0f, 10.0f);
                                  v->r = 120; v->g = 110; v->b = 70; break;
            case NL_VEH_HANDCART: v->speed = nl_randf_range(&g->rng_state, 1.0f, 2.0f);
                                  v->r = 120; v->g = 80; v->b = 50; break;
            default:              v->speed = 8.0f; break;
        }
        g->vehicle_count++;
    }
}

void nl_vehicle_update(NLGame *g, float dt, float sim_seconds)
{
    int i;
    const NLWeather *w = &g->weather;
    bool rush = nl_clock_is_rush_hour(&g->clock);
    bool night = !g->clock.is_daylight;
    float rain = w->rain_mm_hr;

    (void)sim_seconds;

    for (i = 0; i < g->vehicle_count; ++i) {
        NLVehicle *v = &g->vehicles[i];
        const NLRoad *r;
        float road_len;
        float prog;
        float spd;

        if (!v->active) continue;
        if (v->road_index < 0 || v->road_index >= g->road_count) {
            v->active = false;
            continue;
        }
        r = &g->roads[v->road_index];

        /* Nothing drives into Kibera's narrow unpaved paths. */
        if (!r->paved) {
            v->active = false;
            continue;
        }

        road_len = nl_vec_dist(r->a, r->b);
        if (road_len < 1e-3f) { v->active = false; continue; }

        /* Rush hour gridlock: speed collapses near arterials. */
        spd = v->speed;
        if (rush) spd *= (0.25f + 0.5f * (1.0f - (float)r->traffic_density / 10.0f));
        /* Rain slows everyone. */
        if (rain > 1.0f) spd *= (1.0f - nl_clampf(rain * 0.03f, 0.0f, 0.5f));
        /* Boda weaves & is faster; handcart is slow. */
        if (v->kind == NL_VEH_BODA) spd *= 1.2f;
        if (v->kind == NL_VEH_HANDCART) spd *= 0.5f;

        /* Advance progress along the road. */
        prog = (spd * dt) / road_len;
        v->road_t += prog * (float)v->direction;
        if (v->road_t > 1.0f) {
            v->road_t -= 1.0f;
            /* Pick a connected / next road at the junction (simple: reverse or
             * hop to another paved road sharing an endpoint). */
            {
                int j;
                int next = -1;
                float bestd = 1e9f;
                NLVec2 end = (v->direction > 0) ? r->b : r->a;
                for (j = 0; j < g->road_count; ++j) {
                    float d;
                    if (j == v->road_index) continue;
                    if (!g->roads[j].paved) continue;
                    d = nl_vec_dist(end, g->roads[j].a);
                    if (d < 30.0f) { if (d < bestd) { bestd = d; next = j; v->direction = 1; } }
                    d = nl_vec_dist(end, g->roads[j].b);
                    if (d < 30.0f) { if (d < bestd) { bestd = d; next = j; v->direction = -1; } }
                }
                if (next >= 0) {
                    v->road_index = next;
                    v->road_t = (v->direction > 0) ? 0.0f : 1.0f;
                    r = &g->roads[v->road_index];
                } else {
                    /* Dead end: turn around. */
                    v->direction = -v->direction;
                    v->road_t = nl_clampf(v->road_t, 0.0f, 1.0f);
                }
            }
        } else if (v->road_t < 0.0f) {
            v->road_t += 1.0f;
            {
                int j;
                int next = -1;
                float bestd = 1e9f;
                NLVec2 end = (v->direction > 0) ? r->b : r->a;
                for (j = 0; j < g->road_count; ++j) {
                    float d;
                    if (j == v->road_index) continue;
                    if (!g->roads[j].paved) continue;
                    d = nl_vec_dist(end, g->roads[j].a);
                    if (d < 30.0f) { if (d < bestd) { bestd = d; next = j; v->direction = 1; } }
                    d = nl_vec_dist(end, g->roads[j].b);
                    if (d < 30.0f) { if (d < bestd) { bestd = d; next = j; v->direction = -1; } }
                }
                if (next >= 0) {
                    v->road_index = next;
                    v->road_t = (v->direction > 0) ? 0.0f : 1.0f;
                    r = &g->roads[v->road_index];
                } else {
                    v->direction = -v->direction;
                    v->road_t = nl_clampf(v->road_t, 0.0f, 1.0f);
                }
            }
        }

        /* Position from road_t. */
        {
            NLVec2 a = r->a, b = r->b;
            float t = v->road_t;
            if (v->direction < 0) t = 1.0f - t;
            v->pos.x = a.x + (b.x - a.x) * t;
            v->pos.y = a.y + (b.y - a.y) * t;
            v->angle = atan2f((b.y - a.y) * (float)v->direction,
                              (b.x - a.x) * (float)v->direction);
        }

        /* Headlights at night or heavy rain. */
        v->headlights = night || (rain > 4.0f);

        /* Matatus honk in traffic / rush. */
        if (v->kind == NL_VEH_MATATU) {
            v->honk_timer -= dt;
            if (rush && v->honk_timer <= 0.0f) {
                v->honking = true;
                v->honk_timer = nl_randf_range(&g->rng_state, 3.0f, 8.0f);
            } else if (v->honk_timer < -0.3f) {
                v->honking = false;
            }
        } else {
            v->honking = false;
        }

        /* Lateral weave for bodas. */
        if (v->kind == NL_VEH_BODA) {
            float off = sinf(g->real_seconds * 2.0f + (float)i) * 1.5f;
            NLVec2 dir = { r->b.x - r->a.x, r->b.y - r->a.y };
            NLVec2 nrm = nl_vec_norm((NLVec2){ -dir.y, dir.x });
            v->pos.x += nrm.x * off;
            v->pos.y += nrm.y * off;
        }
    }
}
