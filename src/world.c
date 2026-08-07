#include "common.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

static uint32_t g_seed = 12345;
Building g_buildings[MAX_BUILDINGS];
int g_building_count;
Prop g_props[MAX_PROPS];
int g_prop_count;
NavNode g_nav_nodes[MAX_NAV_NODES];
int g_nav_count;
RoadSegment g_roads[200];
int g_road_count;
static bool g_initialized = false;

static uint32_t rand_next(void) {
    g_seed = g_seed * 1103515245 + 12345;
    return (g_seed >> 16) & 0x7FFF;
}

static float rand_float(void) {
    return (float)rand_next() / 32767.0f;
}

static int rand_int(int min, int max) {
    return min + (int)(rand_float() * (max - min + 1));
}

static void set_seed(uint32_t seed) {
    g_seed = seed;
}

static uint32_t building_color(BuildingType type) {
    switch (type) {
        case BUILDING_SLUM: return 0x8B6914;
        case BUILDING_ESTATE: return 0xC19A6B;
        case BUILDING_CBD: return 0xA0A0A0;
    }
    return 0x8B6914;
}

static uint32_t prop_color(PropType type) {
    switch (type) {
        case PROP_MATATU: return 0xFF6600;
        case PROP_KIOSK: return 0x8B4513;
        case PROP_BARRIER: return 0xFF0000;
        case PROP_STREETLIGHT: return 0xFFFF00;
        case PROP_TRASH: return 0x555555;
    }
    return 0xFFFFFF;
}

void WorldInit(uint32_t seed) {
    if (g_initialized) return;
    set_seed(seed);

    g_building_count = 0;
    g_prop_count = 0;
    g_nav_count = 0;
    g_road_count = 0;

    int grid_w = WORLD_WIDTH / ROAD_GRID_SIZE;
    int grid_h = WORLD_HEIGHT / ROAD_GRID_SIZE;

    for (int gx = 0; gx <= grid_w; gx++) {
        int x = gx * ROAD_GRID_SIZE;
        g_roads[g_road_count++] = (RoadSegment){x, 0, false};
    }
    for (int gy = 0; gy <= grid_h; gy++) {
        int y = gy * ROAD_GRID_SIZE;
        g_roads[g_road_count++] = (RoadSegment){0, y, true};
    }

    for (int gx = 0; gx < grid_w; gx++) {
        for (int gy = 0; gy < grid_h; gy++) {
            int block_x = gx * ROAD_GRID_SIZE + 40;
            int block_y = gy * ROAD_GRID_SIZE + 40;
            int block_w = ROAD_GRID_SIZE - 80;
            int block_h = ROAD_GRID_SIZE - 80;

            int building_count = rand_int(2, 6);
            for (int i = 0; i < building_count && g_building_count < MAX_BUILDINGS; i++) {
                int bw = rand_int(60, 140);
                int bh = rand_int(60, 140);
                int bx = block_x + rand_int(0, block_w - bw);
                int by = block_y + rand_int(0, block_h - bh);

                BuildingType type;
                float r = rand_float();
                if (gx >= grid_w / 2 - 1 && gx <= grid_w / 2 + 1 && gy >= grid_h / 2 - 1 && gy <= grid_h / 2 + 1) {
                    type = BUILDING_CBD;
                } else if (r < 0.4f) {
                    type = BUILDING_SLUM;
                } else if (r < 0.7f) {
                    type = BUILDING_ESTATE;
                } else {
                    type = BUILDING_CBD;
                }

                g_buildings[g_building_count++] = (Building){
                    .pos = {bx, by},
                    .size = {bw, bh},
                    .type = type,
                    .color = building_color(type)
                };
            }

            int prop_count = rand_int(3, 8);
            for (int i = 0; i < prop_count && g_prop_count < MAX_PROPS; i++) {
                int px = block_x + rand_int(0, block_w);
                int py = block_y + rand_int(0, block_h);

                PropType ptype;
                float r = rand_float();
                if (r < 0.15f) ptype = PROP_MATATU;
                else if (r < 0.3f) ptype = PROP_KIOSK;
                else if (r < 0.45f) ptype = PROP_BARRIER;
                else if (r < 0.7f) ptype = PROP_STREETLIGHT;
                else ptype = PROP_TRASH;

                g_props[g_prop_count++] = (Prop){
                    .pos = {px, py},
                    .type = ptype,
                    .color = prop_color(ptype)
                };
            }
        }
    }

    for (int gx = 0; gx <= grid_w; gx++) {
        for (int gy = 0; gy <= grid_h; gy++) {
            if (g_nav_count >= MAX_NAV_NODES) break;
            int nx = gx * ROAD_GRID_SIZE;
            int ny = gy * ROAD_GRID_SIZE;
            g_nav_nodes[g_nav_count++] = (NavNode){
                .pos = {nx, ny},
                .walkable = true,
                .connection_count = 0
            };
        }
    }

    for (int i = 0; i < g_nav_count; i++) {
        int gx = (int)(g_nav_nodes[i].pos.x / ROAD_GRID_SIZE);
        int gy = (int)(g_nav_nodes[i].pos.y / ROAD_GRID_SIZE);
        int idx = gy * (grid_w + 1) + gx;

        if (gx > 0) g_nav_nodes[i].connections[g_nav_nodes[i].connection_count++] = idx - 1;
        if (gx < grid_w) g_nav_nodes[i].connections[g_nav_nodes[i].connection_count++] = idx + 1;
        if (gy > 0) g_nav_nodes[i].connections[g_nav_nodes[i].connection_count++] = idx - (grid_w + 1);
        if (gy < grid_h) g_nav_nodes[i].connections[g_nav_nodes[i].connection_count++] = idx + (grid_w + 1);
    }

    g_initialized = true;
}

void WorldUpdate(float dt) {
    (void)dt;
    for (int i = 0; i < g_prop_count; i++) {
        if (g_props[i].type == PROP_MATATU) {
            g_props[i].pos.x += 20.0f * dt;
            if (g_props[i].pos.x > WORLD_WIDTH) g_props[i].pos.x = 0;
        }
    }
}

void WorldDraw(void) {
    for (int i = 0; i < g_road_count; i++) {
        RoadSegment *r = &g_roads[i];
        if (r->horizontal) {
        } else {
        }
    }

    for (int i = 0; i < g_building_count; i++) {
        Building *b = &g_buildings[i];
    }

    for (int i = 0; i < g_prop_count; i++) {
        Prop *p = &g_props[i];
    }
}

Vector2 WorldGetSpawnPoint(Faction faction) {
    Vector2 spawn = {WORLD_WIDTH / 2, WORLD_HEIGHT / 2};

    switch (faction) {
        case FACTION_PLAYER:
            spawn.x = WORLD_WIDTH / 2;
            spawn.y = WORLD_HEIGHT / 2;
            break;
        case FACTION_CIVILIAN: {
            int grid_w = WORLD_WIDTH / ROAD_GRID_SIZE;
            int gx = rand_int(0, grid_w);
            int gy = rand_int(0, grid_w);
            spawn.x = gx * ROAD_GRID_SIZE + rand_int(20, ROAD_GRID_SIZE - 20);
            spawn.y = gy * ROAD_GRID_SIZE + rand_int(20, ROAD_GRID_SIZE - 20);
            break;
        }
        case FACTION_GANG_MUNGIKI: {
            int grid_w = WORLD_WIDTH / ROAD_GRID_SIZE;
            int gx = rand_int(0, grid_w / 3);
            int gy = rand_int(0, grid_w / 3);
            spawn.x = gx * ROAD_GRID_SIZE + rand_int(50, ROAD_GRID_SIZE - 50);
            spawn.y = gy * ROAD_GRID_SIZE + rand_int(50, ROAD_GRID_SIZE - 50);
            break;
        }
        case FACTION_POLICE: {
            spawn.x = ROAD_GRID_SIZE * 2;
            spawn.y = ROAD_GRID_SIZE * 2;
            break;
        }
    }
    return spawn;
}

bool WorldIsWalkable(Vector2 pos) {
    if (pos.x < 0 || pos.x >= WORLD_WIDTH || pos.y < 0 || pos.y >= WORLD_HEIGHT) {
        return false;
    }

    int grid_x = (int)(pos.x / ROAD_GRID_SIZE);
    int grid_y = (int)(pos.y / ROAD_GRID_SIZE);
    int local_x = (int)pos.x % ROAD_GRID_SIZE;
    int local_y = (int)pos.y % ROAD_GRID_SIZE;

    if (local_x < 40 || local_x > ROAD_GRID_SIZE - 40 ||
        local_y < 40 || local_y > ROAD_GRID_SIZE - 40) {
        return true;
    }

    for (int i = 0; i < g_building_count; i++) {
        Building *b = &g_buildings[i];
        if (pos.x >= b->pos.x && pos.x <= b->pos.x + b->size.x &&
            pos.y >= b->pos.y && pos.y <= b->pos.y + b->size.y) {
            return false;
        }
    }

    return true;
}