/* nl_world.c - Nairobi Life: procedural world generation and queries.
 *
 * A 4km x 4km slice of Nairobi (1 world unit = 1 metre), laid out to mirror
 * the real geography: Kibera (informal settlement) in the south-west, the CBD
 * (high-rise grid) in the centre, the Industrial Area to the south-east, a
 * middle-income estate to the north, an open-air market between the estate and
 * the CBD, and a highway verge (roadside) along the eastern and northern edges
 * tying everything together.
 *
 * Pure C99 simulation. No rendering primitives here.
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
/*  District layout regions (metres, within the 4000 x 4000 world).   */
/* ------------------------------------------------------------------ */

/* The world is partitioned into axis-aligned rectangles. nl_world_district_at
 * tests these in priority order. Coordinates are top-left (x,y) with y growing
 * "north" (up). The regions below tile the 0..4000 square without gaps. */
typedef struct {
    NLDistrict dist;
    float x0, y0, x1, y1;
} NLRegion;

static const NLRegion NL_REGIONS[] = {
    /* Kibera: big informal settlement, south-west quadrant. */
    { NL_DIST_KIBERA,    0.0f,   2200.0f, 1700.0f, 4000.0f },
    /* CBD: tall towers, dead centre of the city. */
    { NL_DIST_CBD,       1700.0f, 1700.0f, 2700.0f, 2700.0f },
    /* Industrial Area: warehouses, south-east. */
    { NL_DIST_INDUSTRIAL, 2700.0f, 2200.0f, 4000.0f, 4000.0f },
    /* Market: dense kiosks, just north of Kibera, west of CBD. */
    { NL_DIST_MARKET,    0.0f,    1700.0f, 1700.0f, 2200.0f },
    /* Estate: regular apartment blocks, northern half. */
    { NL_DIST_ESTATE,    1700.0f, 0.0f,    4000.0f, 1700.0f },
    /* Roadside verge: thin strip handled below as a fallback; the remaining
     * wedge between market/CBD/estate on the east gets marked roadside. */
    { NL_DIST_ROADSIDE,  2700.0f, 1700.0f, 4000.0f, 2200.0f },
};
static const int NL_REGION_COUNT = (int)(sizeof(NL_REGIONS) / sizeof(NL_REGIONS[0]));

/* ------------------------------------------------------------------ */
/*  Spatial grid for nl_world_blocked (uniform buckets over buildings)*/
/* ------------------------------------------------------------------ */

#define NL_GRID_DIV 32          /* 32 x 32 buckets over 4000m => 125m cells */
#define NL_GRID_CELL (NL_WORLD_W / (float)NL_GRID_DIV)

typedef struct {
    int indices[8];             /* short lists; capped at 8 per cell */
    int count;
} NLGridCell;

typedef struct {
    NLGridCell cells[NL_GRID_DIV * NL_GRID_DIV];
} NLBuildGrid;

static NLBuildGrid g_build_grid;

static int nl_grid_cell_index(float x, float y)
{
    int cx = (int)(x / NL_GRID_CELL);
    int cy = (int)(y / NL_GRID_CELL);

    if (cx < 0) cx = 0; else if (cx >= NL_GRID_DIV) cx = NL_GRID_DIV - 1;
    if (cy < 0) cy = 0; else if (cy >= NL_GRID_DIV) cy = NL_GRID_DIV - 1;
    return cy * NL_GRID_DIV + cx;
}

static void nl_grid_reset(void)
{
    int i;
    for (i = 0; i < NL_GRID_DIV * NL_GRID_DIV; ++i) {
        g_build_grid.cells[i].count = 0;
    }
}



/* We store building indices by their centre into the grid. */
static void nl_grid_add(int bindex, const NLBuilding *b)
{
    NLGridCell *cell;
    int ci;
    float cx = b->pos.x + b->size.x * 0.5f;
    float cy = b->pos.y + b->size.y * 0.5f;

    ci = nl_grid_cell_index(cx, cy);
    cell = &g_build_grid.cells[ci];
    if (cell->count < 8) {
        cell->indices[cell->count++] = bindex;
    }
}

static bool nl_rects_overlap(float ax, float ay, float aw, float ah,
                             float bx, float by, float bw, float bh)
{
    return (ax < bx + bw) && (ax + aw > bx) && (ay < by + bh) && (ay + ah > by);
}

/* ------------------------------------------------------------------ */
/*  Geometry helpers                                                  */
/* ------------------------------------------------------------------ */

static float nl_point_to_rect_dist(NLVec2 p, NLVec2 rpos, NLVec2 rsize)
{
    float dx = 0.0f, dy = 0.0f;
    float left = rpos.x, top = rpos.y;
    float right = rpos.x + rsize.x, bottom = rpos.y + rsize.y;

    if (p.x < left)       dx = left - p.x;
    else if (p.x > right) dx = p.x - right;
    if (p.y < top)        dy = top - p.y;
    else if (p.y > bottom) dy = p.y - bottom;

    return sqrtf(dx * dx + dy * dy);
}

static bool nl_road_near(const NLGame *g, NLVec2 p, float dist)
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
/*  Road network generation                                           */
/* ------------------------------------------------------------------ */

static void nl_add_road(NLGame *g, float ax, float ay, float bx, float by,
                        bool paved, int traffic)
{
    NLRoad *r;
    if (g->road_count >= 128) return;
    r = &g->roads[g->road_count];
    r->a.x = ax; r->a.y = ay;
    r->b.x = bx; r->b.y = by;
    r->width = paved ? 14.0f : 6.0f;
    r->paved = paved;
    r->traffic_density = traffic;
    g->road_count++;
}

static void nl_generate_roads(NLGame *g)
{
    int i;
    float x, y;

    g->road_count = 0;

    /* --- Arterial ring / cross: the highway verge + major arterials. --- */
    /* North-South arterial (Mombasa Road / Uhuru Highway style) through centre. */
    nl_add_road(g, 2000.0f, 0.0f, 2000.0f, 4000.0f, true, 9);
    /* East-West arterial (Haile Selassie / Ngong Road corridor). */
    nl_add_road(g, 0.0f, 2000.0f, 4000.0f, 2000.0f, true, 9);
    /* Outer bypass along the edges (roadside verge alignment). */
    nl_add_road(g, 0.0f, 400.0f, 4000.0f, 400.0f, true, 6);
    nl_add_road(g, 4000.0f, 0.0f, 4000.0f, 4000.0f, true, 6);

    /* --- CBD grid: tight paved streets. --- */
    for (i = 0; i <= 4; ++i) {
        x = 1700.0f + (float)i * 200.0f;
        nl_add_road(g, x, 1700.0f, x, 2700.0f, true, 8);
    }
    for (i = 0; i <= 4; ++i) {
        y = 1700.0f + (float)i * 200.0f;
        nl_add_road(g, 1700.0f, y, 2700.0f, y, true, 8);
    }

    /* --- Estate regular roads. --- */
    for (i = 0; i <= 6; ++i) {
        x = 1800.0f + (float)i * (2200.0f / 6.0f);
        nl_add_road(g, x, 0.0f, x, 1700.0f, true, 4);
    }
    for (i = 0; i <= 3; ++i) {
        y = 300.0f + (float)i * (1400.0f / 3.0f);
        nl_add_road(g, 1700.0f, y, 4000.0f, y, true, 4);
    }

    /* --- Industrial Area wide lorry roads. --- */
    for (i = 0; i <= 4; ++i) {
        y = 2300.0f + (float)i * (1700.0f / 4.0f);
        nl_add_road(g, 2700.0f, y, 4000.0f, y, true, 5);
    }
    for (i = 0; i <= 3; ++i) {
        x = 2900.0f + (float)i * (1100.0f / 3.0f);
        nl_add_road(g, x, 2200.0f, x, 4000.0f, true, 5);
    }

    /* --- Market: a few narrower paved access roads. --- */
    nl_add_road(g, 0.0f, 1950.0f, 1700.0f, 1950.0f, true, 5);
    nl_add_road(g, 850.0f, 1700.0f, 850.0f, 2200.0f, true, 5);

    /* --- Kibera: unpaved, narrow, winding footpaths only. Matatus cannot
     *     enter; these are not in the vehicle road list (separate walking
     *     use). We add a sparse set of very narrow footpaths as "roads" with
     *     paved=false and traffic 0. --- */
    for (i = 0; i <= 5; ++i) {
        float yy = 2300.0f + nl_randf(&g->rng_state) * 50.0f
                   + (float)i * (1600.0f / 5.0f);
        float wob = 120.0f * nl_fbm1((float)i * 1.7f, 99u, 3);
        nl_add_road(g, 100.0f + wob, yy, 1600.0f - wob, yy + 40.0f, false, 0);
    }
    for (i = 0; i <= 4; ++i) {
        float xx = 300.0f + (float)i * (1300.0f / 4.0f);
        float wob = 100.0f * nl_fbm1((float)i * 2.3f + 5.0f, 77u, 3);
        nl_add_road(g, xx, 2300.0f + wob, xx + 50.0f, 3900.0f - wob, false, 0);
    }

    /* --- Connecting roadside arterials (east + roadside wedge). --- */
    nl_add_road(g, 2700.0f, 1700.0f, 2700.0f, 2200.0f, true, 6);
    nl_add_road(g, 2700.0f, 1950.0f, 4000.0f, 1950.0f, true, 6);
}

/* ------------------------------------------------------------------ */
/*  Building placement                                               */
/* ------------------------------------------------------------------ */

static bool nl_can_place(const NLGame *g, NLVec2 pos, NLVec2 size,
                         float road_pad)
{
    int i;

    /* Keep inside world. */
    if (pos.x < 0.0f || pos.y < 0.0f) return false;
    if (pos.x + size.x > NL_WORLD_W || pos.y + size.y > NL_WORLD_H) return false;

    /* Keep clear of roads. */
    if (nl_road_near(g, (NLVec2){ pos.x + size.x * 0.5f, pos.y + size.y * 0.5f },
                     road_pad + size.x * 0.5f + road_pad)) {
        /* more precise: test all four corners + centre vs road corridor */
    }
    {
        NLVec2 corners[5];
        int c;
        corners[0] = pos;
        corners[1] = (NLVec2){ pos.x + size.x, pos.y };
        corners[2] = (NLVec2){ pos.x, pos.y + size.y };
        corners[3] = (NLVec2){ pos.x + size.x, pos.y + size.y };
        corners[4] = (NLVec2){ pos.x + size.x * 0.5f, pos.y + size.y * 0.5f };
        for (c = 0; c < 5; ++c) {
            if (nl_road_near(g, corners[c], road_pad)) return false;
        }
    }

    /* Keep clear of existing buildings. */
    for (i = 0; i < g->building_count; ++i) {
        const NLBuilding *b = &g->buildings[i];
        if (nl_rects_overlap(pos.x, pos.y, size.x, size.y,
                             b->pos.x, b->pos.y, b->size.x, b->size.y)) {
            return false;
        }
    }
    return true;
}

static void nl_add_building(NLGame *g, NLVec2 pos, NLVec2 size,
                            NLBuildingKind kind, NLDistrict dist,
                            uint8_t r, uint8_t g8, uint8_t b8,
                            int floors, bool enterable, bool lit)
{
    NLBuilding *b;
    if (g->building_count >= NL_MAX_BUILDINGS) return;
    b = &g->buildings[g->building_count];
    memset(b, 0, sizeof(*b));
    b->pos = pos;
    b->size = size;
    b->kind = kind;
    b->district = dist;
    b->r = r; b->g = g8; b->b = b8;
    b->floors = floors;
    b->enterable = enterable;
    b->lit_at_night = lit;
    b->roof_pitch = (kind == NL_BLD_SHACK) ? 0.25f : 0.05f;
    g->building_count++;
    nl_grid_add(g->building_count - 1, b);
}

static void nl_generate_buildings(NLGame *g)
{
    int i;
    NLDistrict d;

    g->building_count = 0;
    nl_grid_reset();

    /* --- Kibera: hundreds of tiny mabati shacks on dirt. --- */
    {
        const int target = 360;
        int placed = 0;
        int tries = 0;
        while (placed < target && tries < 4000 &&
               g->building_count < NL_MAX_BUILDINGS) {
            float x = nl_randf_range(&g->rng_state, 20.0f, 1660.0f);
            float y = nl_randf_range(&g->rng_state, 2220.0f, 3980.0f);
            float w = nl_randf_range(&g->rng_state, 3.0f, 6.0f);
            float h = nl_randf_range(&g->rng_state, 3.0f, 6.0f);
            static const uint8_t mabati[][3] = {
                { 150, 90, 60 }, { 110, 110, 120 }, { 70, 90, 120 },
                { 130, 120, 90 }, { 90, 80, 70 }
            };
            int ci = nl_rand_range(&g->rng_state, 0, 4);
            tries++;
            if (!nl_can_place(g, (NLVec2){ x, y }, (NLVec2){ w, h }, 5.0f))
                continue;
            nl_add_building(g, (NLVec2){ x, y }, (NLVec2){ w, h },
                            NL_BLD_SHACK, NL_DIST_KIBERA,
                            mabati[ci][0], mabati[ci][1], mabati[ci][2],
                            1, true, false);
            placed++;
        }
    }

    /* --- CBD: tall towers + street-level shops. --- */
    for (i = 0; i < g->road_count; ++i) { (void)i; }
    {
        int gx, gy;
        for (gy = 0; gy < 4; ++gy) {
            for (gx = 0; gx < 4; ++gx) {
                float x = 1740.0f + (float)gx * 200.0f;
                float y = 1740.0f + (float)gy * 200.0f;
                if (gx < 3 && gy < 3) {
                    int floors = nl_rand_range(&g->rng_state, 8, 26);
                    if (nl_can_place(g, (NLVec2){ x, y },
                                     (NLVec2){ 140.0f, 140.0f }, 4.0f)) {
                        nl_add_building(g, (NLVec2){ x, y },
                                        (NLVec2){ 140.0f, 140.0f },
                                        NL_BLD_TOWER, NL_DIST_CBD,
                                        120, 130, 150, floors, true, true);
                    }
                }
                /* Street-level shop along avenues. */
                if ((gx + gy) % 2 == 0) {
                    if (nl_can_place(g, (NLVec2){ x, y },
                                     (NLVec2){ 60.0f, 30.0f }, 3.0f)) {
                        nl_add_building(g, (NLVec2){ x, y },
                                        (NLVec2){ 60.0f, 30.0f },
                                        NL_BLD_SHOP, NL_DIST_CBD,
                                        180, 160, 120, 1, true, true);
                    }
                }
            }
        }
    }

    /* --- Industrial Area: big warehouses. --- */
    {
        int gx, gy;
        for (gy = 0; gy < 4; ++gy) {
            for (gx = 0; gx < 3; ++gx) {
                float x = 2800.0f + (float)gx * 360.0f;
                float y = 2400.0f + (float)gy * 360.0f;
                if (nl_can_place(g, (NLVec2){ x, y },
                                 (NLVec2){ 300.0f, 200.0f }, 4.0f)) {
                    nl_add_building(g, (NLVec2){ x, y },
                                    (NLVec2){ 300.0f, 200.0f },
                                    NL_BLD_WAREHOUSE, NL_DIST_INDUSTRIAL,
                                    140, 140, 145, nl_rand_range(&g->rng_state, 1, 3),
                                    true, true);
                }
            }
        }
    }

    /* --- Estate: regular apartment blocks. --- */
    {
        int gx, gy;
        for (gy = 0; gy < 3; ++gy) {
            for (gx = 0; gx < 6; ++gx) {
                float x = 1820.0f + (float)gx * (2200.0f / 6.0f) + 20.0f;
                float y = 320.0f + (float)gy * (1400.0f / 3.0f) + 20.0f;
                if (nl_can_place(g, (NLVec2){ x, y },
                                 (NLVec2){ 120.0f, 90.0f }, 3.0f)) {
                    nl_add_building(g, (NLVec2){ x, y },
                                    (NLVec2){ 120.0f, 90.0f },
                                    NL_BLD_APARTMENT, NL_DIST_ESTATE,
                                    170, 170, 175, nl_rand_range(&g->rng_state, 3, 6),
                                    true, true);
                }
            }
        }
    }

    /* --- Market: dense kiosks in rows with narrow aisles. --- */
    {
        int gx, gy;
        for (gy = 0; gy < 5; ++gy) {
            for (gx = 0; gx < 8; ++gx) {
                float x = 80.0f + (float)gx * 200.0f;
                float y = 1740.0f + (float)gy * 90.0f;
                if (nl_can_place(g, (NLVec2){ x, y },
                                 (NLVec2){ 70.0f, 50.0f }, 2.0f)) {
                    nl_add_building(g, (NLVec2){ x, y },
                                    (NLVec2){ 70.0f, 50.0f },
                                    NL_BLD_KIOSK, NL_DIST_MARKET,
                                    200, 170, 110, 1, true, true);
                }
            }
        }
    }

    /* --- Civic / community buildings in sensible spots. --- */
    /* Police post near CBD edge. */
    if (nl_can_place(g, (NLVec2){ 2650.0f, 2650.0f }, (NLVec2){ 50.0f, 40.0f }, 3.0f))
        nl_add_building(g, (NLVec2){ 2650.0f, 2650.0f }, (NLVec2){ 50.0f, 40.0f },
                        NL_BLD_POLICE_POST, NL_DIST_CBD, 60, 70, 120, 2, true, true);
    /* Clinic in Kibera (real health outposts exist in the slum). */
    if (nl_can_place(g, (NLVec2){ 200.0f, 3600.0f }, (NLVec2){ 40.0f, 30.0f }, 4.0f))
        nl_add_building(g, (NLVec2){ 200.0f, 3600.0f }, (NLVec2){ 40.0f, 30.0f },
                        NL_BLD_CLINIC, NL_DIST_KIBERA, 220, 220, 230, 1, true, true);
    /* Church in estate. */
    if (nl_can_place(g, (NLVec2){ 2000.0f, 800.0f }, (NLVec2){ 60.0f, 45.0f }, 3.0f))
        nl_add_building(g, (NLVec2){ 2000.0f, 800.0f }, (NLVec2){ 60.0f, 45.0f },
                        NL_BLD_CHURCH, NL_DIST_ESTATE, 200, 190, 180, 1, true, true);
    /* Water points: Kibera + market (communal points, real Nairobi slums). */
    if (nl_can_place(g, (NLVec2){ 800.0f, 2400.0f }, (NLVec2){ 18.0f, 18.0f }, 3.0f))
        nl_add_building(g, (NLVec2){ 800.0f, 2400.0f }, (NLVec2){ 18.0f, 18.0f },
                        NL_BLD_WATER_POINT, NL_DIST_KIBERA, 90, 150, 200, 1, false, false);
    if (nl_can_place(g, (NLVec2){ 1300.0f, 1900.0f }, (NLVec2){ 18.0f, 18.0f }, 3.0f))
        nl_add_building(g, (NLVec2){ 1300.0f, 1900.0f }, (NLVec2){ 18.0f, 18.0f },
                        NL_BLD_WATER_POINT, NL_DIST_MARKET, 90, 150, 200, 1, false, false);
    /* Industrial police post. */
    if (nl_can_place(g, (NLVec2){ 3800.0f, 3900.0f }, (NLVec2){ 40.0f, 30.0f }, 3.0f))
        nl_add_building(g, (NLVec2){ 3800.0f, 3900.0f }, (NLVec2){ 40.0f, 30.0f },
                        NL_BLD_POLICE_POST, NL_DIST_INDUSTRIAL, 60, 70, 120, 1, true, true);

    /* Silence unused warning for d. */
    (void)d;
}

/* ------------------------------------------------------------------ */
/*  Props                                                            */
/* ------------------------------------------------------------------ */

static void nl_add_prop(NLGame *g, NLVec2 pos, NLPropKind kind,
                        float scale, float rot, uint8_t r, uint8_t gg, uint8_t b)
{
    NLProp *p;
    if (g->prop_count >= NL_MAX_PROPS) return;
    p = &g->props[g->prop_count];
    memset(p, 0, sizeof(*p));
    p->pos = pos;
    p->kind = kind;
    p->scale = scale;
    p->rot = rot;
    p->r = r; p->g = gg; p->b = b;
    g->prop_count++;
}

static void nl_generate_props(NLGame *g)
{
    int i;

    g->prop_count = 0;

    /* Streetlights only along paved, traffic-bearing roads. */
    for (i = 0; i < g->road_count; ++i) {
        const NLRoad *r = &g->roads[i];
        float len, n, t;
        if (!r->paved || r->traffic_density <= 0) continue;
        len = nl_vec_dist(r->a, r->b);
        n = len / 60.0f;
        if (n < 1.0f) n = 1.0f;
        for (t = 0.0f; t <= n; t += 1.0f) {
            float f = (n > 0.0f) ? (t / n) : 0.0f;
            NLVec2 p = { r->a.x + (r->b.x - r->a.x) * f,
                         r->a.y + (r->b.y - r->a.y) * f };
            float side = (nl_randf(&g->rng_state) < 0.5f) ? 1.0f : -1.0f;
            NLVec2 dir = { r->b.x - r->a.x, r->b.y - r->a.y };
            NLVec2 nrm = nl_vec_norm((NLVec2){ -dir.y, dir.x });
            p.x += nrm.x * (r->width * 0.5f + 1.5f) * side;
            p.y += nrm.y * (r->width * 0.5f + 1.5f) * side;
            if (p.x < 0 || p.y < 0 || p.x > NL_WORLD_W || p.y > NL_WORLD_H)
                continue;
            nl_add_prop(g, p, NL_PROP_STREETLIGHT, 4.0f, 0.0f, 40, 40, 45);
        }
    }

    /* Trees in estate and roadside. */
    for (i = 0; i < 220 && g->prop_count < NL_MAX_PROPS; ++i) {
        float x = nl_randf_range(&g->rng_state, 1720.0f, 3980.0f);
        float y = nl_randf_range(&g->rng_state, 40.0f, 1680.0f);
        if (nl_road_near(g, (NLVec2){ x, y }, 4.0f)) continue;
        if (nl_world_blocked(g, (NLVec2){ x, y }, 1.0f)) continue;
        nl_add_prop(g, (NLVec2){ x, y }, NL_PROP_TREE,
                    nl_randf_range(&g->rng_state, 2.5f, 4.5f),
                    nl_randf(&g->rng_state) * M_PI * 2.0f,
                    40, 110, 45);
    }

    /* Trash concentrated in Kibera & market. */
    for (i = 0; i < 400 && g->prop_count < NL_MAX_PROPS; ++i) {
        float x = nl_randf_range(&g->rng_state, 20.0f, 1700.0f);
        float y = nl_randf_range(&g->rng_state, 1720.0f, 3990.0f);
        NLDistrict d = nl_world_district_at(g, (NLVec2){ x, y });
        if (d != NL_DIST_KIBERA && d != NL_DIST_MARKET) continue;
        if (nl_world_blocked(g, (NLVec2){ x, y }, 1.0f)) continue;
        nl_add_prop(g, (NLVec2){ x, y }, NL_PROP_TRASH,
                    nl_randf_range(&g->rng_state, 0.6f, 1.6f),
                    nl_randf(&g->rng_state) * M_PI * 2.0f,
                    90, 80, 60);
    }

    /* Laundry lines in Kibera. */
    for (i = 0; i < 220 && g->prop_count < NL_MAX_PROPS; ++i) {
        float x = nl_randf_range(&g->rng_state, 20.0f, 1600.0f);
        float y = nl_randf_range(&g->rng_state, 2220.0f, 3980.0f);
        if (nl_world_blocked(g, (NLVec2){ x, y }, 1.0f)) continue;
        nl_add_prop(g, (NLVec2){ x, y }, NL_PROP_LAUNDRY_LINE,
                    nl_randf_range(&g->rng_state, 2.0f, 4.0f),
                    nl_randf(&g->rng_state) * M_PI,
                    200, 200, 210);
    }

    /* Crates & trash in market stalls. */
    for (i = 0; i < 200 && g->prop_count < NL_MAX_PROPS; ++i) {
        float x = nl_randf_range(&g->rng_state, 60.0f, 1650.0f);
        float y = nl_randf_range(&g->rng_state, 1740.0f, 2180.0f);
        if (nl_world_blocked(g, (NLVec2){ x, y }, 1.0f)) continue;
        nl_add_prop(g, (NLVec2){ x, y }, NL_PROP_CRATE,
                    nl_randf_range(&g->rng_state, 0.8f, 1.8f),
                    nl_randf(&g->rng_state) * M_PI,
                    150, 110, 60);
    }

    /* Signboards on CBD shops. */
    for (i = 0; i < g->building_count && g->prop_count < NL_MAX_PROPS; ++i) {
        const NLBuilding *b = &g->buildings[i];
        if (b->kind != NL_BLD_SHOP) continue;
        if (nl_randf(&g->rng_state) > 0.6f) continue;
        nl_add_prop(g, (NLVec2){ b->pos.x + b->size.x * 0.5f, b->pos.y - 1.0f },
                    NL_PROP_SIGNBOARD, nl_randf_range(&g->rng_state, 1.5f, 3.0f),
                    0.0f, 220, 60, 60);
    }

    /* Tyres & barrels in industrial. */
    for (i = 0; i < 160 && g->prop_count < NL_MAX_PROPS; ++i) {
        float x = nl_randf_range(&g->rng_state, 2720.0f, 3980.0f);
        float y = nl_randf_range(&g->rng_state, 2220.0f, 3980.0f);
        if (nl_world_blocked(g, (NLVec2){ x, y }, 1.0f)) continue;
        nl_add_prop(g, (NLVec2){ x, y },
                    (nl_randf(&g->rng_state) < 0.5f) ? NL_PROP_TYRE : NL_PROP_BARREL,
                    nl_randf_range(&g->rng_state, 0.8f, 1.4f),
                    nl_randf(&g->rng_state) * M_PI, 30, 30, 35);
    }

    /* Open drains: along Kibera footpaths (water strips). */
    for (i = 0; i < g->road_count; ++i) {
        const NLRoad *r = &g->roads[i];
        if (r->paved) continue;            /* drains mainly in unpaved slum */
        NLVec2 mid = { (r->a.x + r->b.x) * 0.5f, (r->a.y + r->b.y) * 0.5f };
        if (mid.x < 1700.0f && mid.y > 2200.0f) {
            nl_add_prop(g, mid, NL_PROP_DRAIN, r->width * 0.4f + 1.0f,
                        atan2f(r->b.y - r->a.y, r->b.x - r->a.x),
                        60, 70, 80);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

void nl_world_generate(NLGame *g, uint32_t seed)
{
    if (g == NULL) return;

    g->rng_state = seed ? seed : 0x12345678u;
    g->building_count = 0;
    g->prop_count = 0;
    g->road_count = 0;
    g->puddle_count = 0;

    nl_generate_roads(g);
    nl_generate_buildings(g);
    nl_generate_props(g);
}

NLDistrict nl_world_district_at(const NLGame *g, NLVec2 p)
{
    int i;
    (void)g;
    for (i = 0; i < NL_REGION_COUNT; ++i) {
        const NLRegion *r = &NL_REGIONS[i];
        if (p.x >= r->x0 && p.x < r->x1 && p.y >= r->y0 && p.y < r->y1) {
            return r->dist;
        }
    }
    /* Fallback anywhere else along the eastern verge => roadside. */
    return NL_DIST_ROADSIDE;
}

NLSurface nl_world_surface_at(const NLGame *g, NLVec2 p)
{
    NLDistrict d;

    if (p.x < 0.0f || p.y < 0.0f || p.x > NL_WORLD_W || p.y > NL_WORLD_H)
        return NL_SURF_DIRT;

    /* On a road? */
    if (nl_road_near(g, p, 0.0f)) {
        const NLRoad *near = NULL;
        int i;
        float best = 1e9f;
        for (i = 0; i < g->road_count; ++i) {
            const NLRoad *r = &g->roads[i];
            NLVec2 ab = { r->b.x - r->a.x, r->b.y - r->a.y };
            float len2 = ab.x * ab.x + ab.y * ab.y;
            float t, d;
            NLVec2 proj;
            if (len2 < 1e-6f) continue;
            t = ((p.x - r->a.x) * ab.x + (p.y - r->a.y) * ab.y) / len2;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            proj.x = r->a.x + ab.x * t;
            proj.y = r->a.y + ab.y * t;
            d = nl_vec_dist(p, proj);
            if (d < best) { best = d; near = r; }
        }
        if (near) {
            if (near->paved) return NL_SURF_TARMAC;
            return NL_SURF_DIRT;
        }
    }

    d = nl_world_district_at(g, p);

    /* Water strips from drains. */
    {
        int i;
        for (i = 0; i < g->prop_count; ++i) {
            const NLProp *pr = &g->props[i];
            if (pr->kind == NL_PROP_DRAIN) {
                if (nl_vec_dist(p, pr->pos) < pr->scale + 1.0f)
                    return NL_SURF_WATER;
            }
        }
    }

    switch (d) {
        case NL_DIST_KIBERA:     return NL_SURF_DIRT;
        case NL_DIST_CBD:        return NL_SURF_CONCRETE;
        case NL_DIST_INDUSTRIAL: return NL_SURF_CONCRETE;
        case NL_DIST_ESTATE:     return NL_SURF_GRASS;
        case NL_DIST_MARKET:     return NL_SURF_DIRT;
        case NL_DIST_ROADSIDE:   return NL_SURF_GRASS;
        default:                 return NL_SURF_DIRT;
    }
}

bool nl_world_blocked(const NLGame *g, NLVec2 p, float radius)
{
    int cx, cy, dx, dy;
    int base_cx, base_cy;

    if (p.x < 0.0f || p.y < 0.0f || p.x > NL_WORLD_W || p.y > NL_WORLD_H)
        return true;

    base_cx = (int)(p.x / NL_GRID_CELL);
    base_cy = (int)(p.y / NL_GRID_CELL);
    if (base_cx < 0) base_cx = 0; else if (base_cx >= NL_GRID_DIV) base_cx = NL_GRID_DIV - 1;
    if (base_cy < 0) base_cy = 0; else if (base_cy >= NL_GRID_DIV) base_cy = NL_GRID_DIV - 1;

    /* Scan a 3x3 neighbourhood of grid cells only. */
    for (dy = -1; dy <= 1; ++dy) {
        for (dx = -1; dx <= 1; ++dx) {
            cx = base_cx + dx;
            cy = base_cy + dy;
            if (cx < 0 || cy < 0 || cx >= NL_GRID_DIV || cy >= NL_GRID_DIV)
                continue;
            {
                const NLGridCell *cell = &g_build_grid.cells[cy * NL_GRID_DIV + cx];
                int k;
                for (k = 0; k < cell->count; ++k) {
                    int bi = cell->indices[k];
                    const NLBuilding *b = &g->buildings[bi];
                    float d = nl_point_to_rect_dist(p, b->pos, b->size);
                    if (d < radius) return true;
                }
            }
        }
    }
    return false;
}

bool nl_world_under_roof(const NLGame *g, NLVec2 p)
{
    int i;
    /* Under an enterable building's footprint (awning/roof). */
    for (i = 0; i < g->building_count; ++i) {
        const NLBuilding *b = &g->buildings[i];
        if (!b->enterable) continue;
        if (p.x >= b->pos.x - 1.0f && p.x <= b->pos.x + b->size.x + 1.0f &&
            p.y >= b->pos.y - 1.0f && p.y <= b->pos.y + b->size.y + 1.0f) {
            return true;
        }
    }
    return false;
}

void nl_world_update(NLGame *g, float dt, float sim_seconds)
{
    const NLWeather *w;
    int i;
    float rain = 0.0f;
    float evap;
    (void)sim_seconds;

    if (g == NULL) return;
    w = &g->weather;
    rain = w->rain_mm_hr;

    /* Grow / spawn puddles on dirt & low spots when raining. */
    if (rain > 1.0f) {
        /* Sample candidate spots on dirt surfaces. */
        int attempts = 6;
        for (i = 0; i < attempts && g->puddle_count < NL_MAX_PUDDLES; ++i) {
            NLVec2 p = { nl_randf_range(&g->rng_state, 0.0f, NL_WORLD_W),
                         nl_randf_range(&g->rng_state, 0.0f, NL_WORLD_H) };
            NLSurface s = nl_world_surface_at(g, p);
            if (s == NL_SURF_DIRT || s == NL_SURF_GRASS) {
                /* Merge into an existing nearby puddle if present. */
                int j;
                bool merged = false;
                for (j = 0; j < g->puddle_count; ++j) {
                    if (nl_vec_dist(g->puddles[j].pos, p) < 30.0f) {
                        g->puddles[j].radius += 0.4f * dt * (rain * 0.1f);
                        g->puddles[j].depth = nl_clampf(
                            g->puddles[j].depth + dt * (rain * 0.002f), 0.0f, 1.0f);
                        merged = true;
                        break;
                    }
                }
                if (!merged && g->puddle_count < NL_MAX_PUDDLES) {
                    NLPuddle *pd = &g->puddles[g->puddle_count++];
                    pd->pos = p;
                    pd->radius = nl_randf_range(&g->rng_state, 1.5f, 4.0f);
                    pd->depth = nl_clampf(rain * 0.002f * dt, 0.0f, 1.0f);
                }
            }
        }
    }

    /* Evaporation driven by temperature, sun and low humidity. */
    evap = dt * (0.02f + w->temperature_c * 0.002f) *
           (1.0f - w->humidity * 0.7f);
    if (!g->clock.is_daylight) evap *= 0.4f;  /* slower at night */
    for (i = 0; i < g->puddle_count; ++i) {
        g->puddles[i].depth -= evap;
        g->puddles[i].radius -= evap * 2.0f;
        if (g->puddles[i].depth < 0.02f || g->puddles[i].radius < 0.5f) {
            /* Swap-remove. */
            g->puddles[i] = g->puddles[g->puddle_count - 1];
            g->puddle_count--;
            i--;
        }
    }
}

const char *nl_district_name(NLDistrict d)
{
    switch (d) {
        case NL_DIST_KIBERA:     return "Kibera";
        case NL_DIST_CBD:        return "CBD";
        case NL_DIST_INDUSTRIAL: return "Industrial Area";
        case NL_DIST_ESTATE:     return "Estate";
        case NL_DIST_MARKET:     return "Market";
        case NL_DIST_ROADSIDE:   return "Roadside";
        default:                 return "Unknown";
    }
}

const char *nl_building_name(NLBuildingKind k)
{
    switch (k) {
        case NL_BLD_SHACK:        return "Mabati Shack";
        case NL_BLD_TOWER:        return "Office Tower";
        case NL_BLD_SHOP:         return "Duka (Shop)";
        case NL_BLD_KIOSK:        return "Kibanda (Kiosk)";
        case NL_BLD_WAREHOUSE:    return "Warehouse";
        case NL_BLD_APARTMENT:    return "Apartment Block";
        case NL_BLD_CHURCH:       return "Church";
        case NL_BLD_POLICE_POST:  return "Police Post";
        case NL_BLD_CLINIC:       return "Clinic";
        case NL_BLD_WATER_POINT:  return "Water Point";
        default:                  return "Building";
    }
}
