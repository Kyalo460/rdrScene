/* nl_render.c - Nairobi Life: procedural top-down 2D renderer (raylib 5.5).
 *
 * NOTE ON FRAME OWNERSHIP:
 *   nl_render_frame() calls BeginDrawing() / EndDrawing() itself. The main loop
 *   simply calls nl_render_frame(g) once per frame; it must NOT wrap this call
 *   in another BeginDrawing/EndDrawing pair.
 *
 * NOTE ON WINDOW OWNERSHIP:
 *   nl_render_init() / nl_render_shutdown() create and destroy renderer-local
 *   resources (light buffer, scratch font, particle pool). They must NOT call
 *   InitWindow()/CloseWindow() - the main loop owns the window.
 *
 * All drawing is procedural: no image/texture assets exist. Everything is
 * culled to the camera view for performance (target 144 FPS @ 1600x900).
 *
 * Lighting model (top-down faux-GI):
 *   - The lit scene is drawn to a RenderTexture2D ("lit") under a Camera2D.
 *   - A separate light-accumulation RenderTexture2D ("light") is rendered with
 *     additive blending: a full-screen ambient wash (warm/orange at sunrise &
 *     sunset, blue-grey by day, near-black at night) plus bright radial pools
 *     from streetlights, vehicle headlights and lit windows.
 *   - The light texture is multiplied over the lit scene. At night the ambient
 *     wash is very dim so only light-pool areas are visible. Kibera has no
 *     streetlights, so it renders nearly black.
 *
 * Rain is a fixed pool of NL_MAX_RAINDROPS, advanced in nl_render_rain_update().
 */

#include "raylib.h"   /* include raylib FIRST */
#include "nl_core.h"  /* then the game types */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ------------------------------------------------------------------ */
/*  Internal renderer state                                            */
/* ------------------------------------------------------------------ */

static RenderTexture2D g_lit;     /* scene drawn here (world space) */
static RenderTexture2D g_light;   /* light accumulation (world space) */
static Font             g_font;    /* default font (kept for convenience) */
static bool             g_inited = false;

/* Rain particle pool. */
typedef struct {
    float x, y;          /* world-space position */
    float vx, vy;        /* velocity (m/s) in world space */
    float len;           /* streak length */
    float life;          /* seconds remaining before respawn */
    bool  active;
} NLRainDrop;

static NLRainDrop g_rain[NL_MAX_RAINDROPS];
static int        g_rain_count = 0;     /* active drops this frame */
static float      g_spawn_accum = 0.0f; /* spawn accumulator */
static float      g_rain_time = 0.0f;   /* global rain clock for splashes */

/* Small helpers to keep code readable. */
static Color  rgb8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    Color c; c.r = r; c.g = g; c.b = b; c.a = a; return c;
}
static float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
static float lerpf_local(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* ------------------------------------------------------------------ */
/*  Camera                                                            */
/* ------------------------------------------------------------------ */

static Camera2D nl_make_camera(const NLGame *g)
{
    Camera2D cam;
    cam.offset = (Vector2){ (float)NL_SCREEN_W * 0.5f, (float)NL_SCREEN_H * 0.5f };
    cam.target = (Vector2){ g->camera.x, g->camera.y };
    cam.rotation = 0.0f;
    cam.zoom = g->camera_zoom;
    return cam;
}

/* Visible world-space rect for culling (with margin). */
static void nl_view_rect(const NLGame *g, float margin, float *x0, float *y0,
                         float *x1, float *y1)
{
    float halfW = ((float)NL_SCREEN_W * 0.5f) / g->camera_zoom;
    float halfH = ((float)NL_SCREEN_H * 0.5f) / g->camera_zoom;
    *x0 = g->camera.x - halfW - margin;
    *y0 = g->camera.y - halfH - margin;
    *x1 = g->camera.x + halfW + margin;
    *y1 = g->camera.y + halfH + margin;
    if (*x0 < 0.0f) *x0 = 0.0f;
    if (*y0 < 0.0f) *y0 = 0.0f;
    if (*x1 > NL_WORLD_W) *x1 = NL_WORLD_W;
    if (*y1 > NL_WORLD_H) *y1 = NL_WORLD_H;
}

static bool in_view(float x0, float y0, float x1, float y1,
                    float rx0, float ry0, float rx1, float ry1)
{
    return (x0 < rx1) && (x1 > rx0) && (y0 < ry1) && (y1 > ry0);
}

/* ------------------------------------------------------------------ */
/*  Surface colour helpers                                            */
/* ------------------------------------------------------------------ */

static Color nl_surface_color(NLSurface s, float ground_wetness)
{
    Color c;
    switch (s) {
        case NL_SURF_TARMAC:   c = rgb8(46, 46, 50, 255);   break;
        case NL_SURF_DIRT:     c = rgb8(150, 78, 48, 255);  break; /* Nairobi red soil */
        case NL_SURF_CONCRETE: c = rgb8(120, 122, 128, 255); break;
        case NL_SURF_GRASS:    c = rgb8(96, 110, 52, 255);   break;
        case NL_SURF_WATER:    c = rgb8(40, 52, 64, 255);    break;
        default:               c = rgb8(80, 80, 80, 255);    break;
    }
    /* Wet ground: darken and shift toward cool sheen. */
    if (ground_wetness > 0.0f) {
        float w = ground_wetness * 0.45f;
        c.r = (uint8_t)(c.r * (1.0f - w) + 30 * w);
        c.g = (uint8_t)(c.g * (1.0f - w) + 34 * w);
        c.b = (uint8_t)(c.b * (1.0f - w) + 44 * w);
    }
    return c;
}

/* Sun direction from clock: low & east at sunrise, high at midday,
 * low & west at sunset. Returns a normalised offset for shadows. */
static void nl_sun_offset(const NLClock *c, float *ox, float *elev)
{
    float sr = c->sunrise, ss = c->sunset, h = c->hour;
    float t;
    if (h >= sr && h <= ss) {
        /* Day: 0 at sunrise -> 1 at sunset. */
        t = (h - sr) / ((ss - sr) > 0.01f ? (ss - sr) : 1.0f);
        *ox = lerpf_local(1.0f, -1.0f, t);          /* east -> west */
        *elev = sinf(t * M_PI);                     /* arc height */
    } else {
        *ox = 0.0f; *elev = 0.0f;
    }
    if (*elev < 0.05f) *elev = 0.05f;
}

/* ------------------------------------------------------------------ */
/*  Ground layer (district rectangles + roads + puddles)              */
/* ------------------------------------------------------------------ */

static void nl_draw_ground(const NLGame *g, float vx0, float vy0, float vx1, float vy1)
{
    /* Mirror of the region table in nl_world.c (kept local for rendering). */
    static const struct { NLDistrict dist; float x0,y0,x1,y1; } R[] = {
        { NL_DIST_KIBERA,    0.0f,   2200.0f, 1700.0f, 4000.0f },
        { NL_DIST_CBD,       1700.0f, 1700.0f, 2700.0f, 2700.0f },
        { NL_DIST_INDUSTRIAL, 2700.0f, 2200.0f, 4000.0f, 4000.0f },
        { NL_DIST_MARKET,    0.0f,    1700.0f, 1700.0f, 2200.0f },
        { NL_DIST_ESTATE,    1700.0f, 0.0f,    4000.0f, 1700.0f },
        { NL_DIST_ROADSIDE,  2700.0f, 1700.0f, 4000.0f, 2200.0f },
    };
    int i;

    /* Base fill (default dirt) to avoid gaps. */
    DrawRectangle(0, 0, (int)NL_WORLD_W, (int)NL_WORLD_H,
                  nl_surface_color(NL_SURF_DIRT, g->weather.ground_wetness));

    for (i = 0; i < 6; ++i) {
        NLSurface s;
        float x0 = R[i].x0, y0 = R[i].y0, x1 = R[i].x1, y1 = R[i].y1;
        if (!in_view(x0, y0, x1, y1, vx0, vy0, vx1, vy1)) continue;
        switch (R[i].dist) {
            case NL_DIST_KIBERA:     s = NL_SURF_DIRT;     break;
            case NL_DIST_CBD:        s = NL_SURF_CONCRETE; break;
            case NL_DIST_INDUSTRIAL: s = NL_SURF_CONCRETE; break;
            case NL_DIST_ESTATE:     s = NL_SURF_GRASS;    break;
            case NL_DIST_MARKET:     s = NL_SURF_DIRT;     break;
            case NL_DIST_ROADSIDE:   s = NL_SURF_GRASS;    break;
            default:                 s = NL_SURF_DIRT;     break;
        }
        DrawRectangle((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0),
                      nl_surface_color(s, g->weather.ground_wetness));
    }

    /* A faint grid for spatial reference on dirt/grass. */
    {
        float step = 200.0f;
        float x;
        Color faint = rgb8(0, 0, 0, 18);
        for (x = (float)((int)(vx0 / step) * (int)step); x < vx1; x += step) {
            if (x < 0 || x > NL_WORLD_W) continue;
            DrawLineEx((Vector2){x, vy0}, (Vector2){x, vy1}, 1.0f, faint);
        }
        {
            float y;
            for (y = (float)((int)(vy0 / step) * (int)step); y < vy1; y += step) {
                if (y < 0 || y > NL_WORLD_H) continue;
                DrawLineEx((Vector2){vx0, y}, (Vector2){vx1, y}, 1.0f, faint);
            }
        }
    }
}

static void nl_draw_roads(const NLGame *g, float vx0, float vy0, float vx1, float vy1)
{
    int i;
    for (i = 0; i < g->road_count; ++i) {
        const NLRoad *r = &g->roads[i];
        float ax = r->a.x, ay = r->a.y, bx = r->b.x, by = r->b.y;
        float minx = fminf(ax, bx) - r->width;
        float maxx = fmaxf(ax, bx) + r->width;
        float miny = fminf(ay, by) - r->width;
        float maxy = fmaxf(ay, by) + r->width;
        float dx = bx - ax, dy = by - ay;
        float len = sqrtf(dx*dx + dy*dy);
        float ang;
        Color road_col;
        if (!in_view(minx, miny, maxx, maxy, vx0, vy0, vx1, vy1)) continue;
        if (len < 1e-3f) continue;

        ang = atan2f(dy, dx);

        road_col = r->paved ? rgb8(38, 38, 42, 255)
                            : rgb8(120, 64, 40, 255); /* murram verge */
        /* Widen murram visual a touch. */
        float w = r->width + (r->paved ? 0.0f : 2.0f);

        /* Road body. */
        DrawRectanglePro((Rectangle){ ax, ay, len, w },
                         (Vector2){ 0.0f, w * 0.5f }, ang * 180.0f / M_PI, road_col);

        /* Lane markings on paved roads. */
        if (r->paved) {
            float dash = 10.0f, gap = 10.0f;
            float t = 0.0f;
            Color line = rgb8(220, 220, 200, 200);
            while (t < len) {
                float s0 = t, s1 = t + dash;
                if (s1 > len) s1 = len;
                Vector2 p0 = { ax + dx * (s0/len), ay + dy * (s0/len) };
                Vector2 p1 = { ax + dx * (s1/len), ay + dy * (s1/len) };
                DrawLineEx(p0, p1, 1.6f, line);
                t += dash + gap;
            }
        }
    }
}

static void nl_draw_puddles(const NLGame *g, float vx0, float vy0, float vx1, float vy1)
{
    int i;
    for (i = 0; i < g->puddle_count; ++i) {
        const NLPuddle *p = &g->puddles[i];
        if (!in_view(p->pos.x - p->radius, p->pos.y - p->radius,
                     p->pos.x + p->radius, p->pos.y + p->radius,
                     vx0, vy0, vx1, vy1)) continue;
        /* Reflective ellipse; brighter/larger as depth grows. */
        float a = 60 + p->depth * 120;
        DrawEllipse((int)p->pos.x, (int)p->pos.y, p->radius, p->radius * 0.6f,
                    rgb8(90, 120, 150, (uint8_t)a));
        DrawEllipse((int)p->pos.x, (int)p->pos.y, p->radius * 0.5f, p->radius * 0.3f,
                    rgb8(180, 210, 235, (uint8_t)(a * 0.6f)));
    }
}

/* ------------------------------------------------------------------ */
/*  Buildings                                                         */
/* ------------------------------------------------------------------ */

static Color nl_building_base(const NLBuilding *b)
{
    return rgb8(b->r, b->g, b->b, 255);
}

static void nl_draw_buildings(const NLGame *g, float vx0, float vy0, float vx1, float vy1)
{
    int i;
    float ox, elev;
    nl_sun_offset(&g->clock, &ox, &elev);

    for (i = 0; i < g->building_count; ++i) {
        const NLBuilding *b = &g->buildings[i];
        float x = b->pos.x, y = b->pos.y;
        float w = b->size.x, h = b->size.y;
        if (!in_view(x, y, x + w, y + h, vx0, vy0, vx1, vy1)) continue;

        Color base = nl_building_base(b);
        float ext = (float)b->floors * 1.4f;           /* fake-3D extrusion */
        if (ext > 26.0f) ext = 26.0f;
        float shx = ox * 10.0f * elev;
        float shy = 6.0f;

        /* Drop shadow. */
        DrawRectanglePro((Rectangle){ x + shx, y + shy, w, h },
                         (Vector2){ 0.0f, 0.0f }, 0.0f, rgb8(0, 0, 0, 70));

        /* Extruded side (right/top faces) for towers/blocks => sense of height. */
        if (ext > 1.0f) {
            Color side = rgb8((uint8_t)(base.r * 0.6f),
                              (uint8_t)(base.g * 0.6f),
                              (uint8_t)(base.b * 0.6f), 255);
            DrawRectanglePro((Rectangle){ x + w, y, ext, h },
                             (Vector2){ 0.0f, 0.0f }, 0.0f, side);
            DrawRectanglePro((Rectangle){ x, y + h, w, ext },
                             (Vector2){ 0.0f, 0.0f }, 0.0f, side);
            /* Top face offset. */
            DrawRectangle((int)(x + ext * 0.4f), (int)(y + ext * 0.4f),
                          (int)(w - ext * 0.4f), (int)(h - ext * 0.4f),
                          rgb8((uint8_t)(base.r * 1.15f),
                               (uint8_t)(base.g * 1.15f),
                               (uint8_t)(base.b * 1.15f), 255));
        }

        /* Main footprint. */
        DrawRectangle((int)x, (int)y, (int)w, (int)h, base);

        /* Corrugated iron suggestion for shacks: thin parallel lines. */
        if (b->kind == NL_BLD_SHACK) {
            int k;
            Color corr = rgb8((uint8_t)(base.r * 0.7f),
                              (uint8_t)(base.g * 0.7f),
                              (uint8_t)(base.b * 0.7f), 255);
            for (k = 2; k < (int)w - 1; k += 2) {
                DrawLineEx((Vector2){ x + k, y + 1 },
                           (Vector2){ x + k, y + h - 1 },
                           0.8f, corr);
            }
        }

        /* Roof outline. */
        DrawRectangleLinesEx((Rectangle){ x, y, w, h }, 0.8f,
                             rgb8((uint8_t)(base.r*0.5f),
                                  (uint8_t)(base.g*0.5f),
                                  (uint8_t)(base.b*0.5f), 255));

        /* Lit windows at night for towers/apartments. */
        if ((b->kind == NL_BLD_TOWER || b->kind == NL_BLD_APARTMENT)
            && b->lit_at_night && !g->clock.is_daylight) {
            int cols = (int)fmaxf(2.0f, w / 14.0f);
            int rows = (int)fmaxf(2.0f, h / 14.0f);
            int cx, cy;
            uint32_t seed = (uint32_t)(i * 2654435761u);
            for (cy = 0; cy < rows; ++cy) {
                for (cx = 0; cx < cols; ++cx) {
                    if (nl_rand(&seed) & 3) continue; /* ~25% lit */
                    float wx = x + 6.0f + cx * (w - 10.0f) / (float)cols;
                    float wy = y + 6.0f + cy * (h - 10.0f) / (float)rows;
                    DrawRectangle((int)wx, (int)wy, 4, 5,
                                  rgb8(255, 220, 130, 230));
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Props                                                             */
/* ------------------------------------------------------------------ */

static void nl_draw_props(const NLGame *g, float vx0, float vy0, float vx1, float vy1,
                          float dt)
{
    int i;
    float wind = g->weather.wind_speed_ms;
    float wind_phase = g->real_seconds * (1.0f + wind * 0.2f);

    for (i = 0; i < g->prop_count; ++i) {
        const NLProp *p = &g->props[i];
        float s = p->scale;
        float sway = 0.0f;
        if (p->kind == NL_PROP_TREE || p->kind == NL_PROP_LAUNDRY_LINE) {
            sway = sinf(wind_phase + p->pos.x * 0.05f) * (0.05f + wind * 0.03f);
        }
        if (!in_view(p->pos.x - s*2, p->pos.y - s*2,
                     p->pos.x + s*2, p->pos.y, vx0, vy0, vx1, vy1)) continue;

        switch (p->kind) {
            case NL_PROP_STREETLIGHT: {
                /* pole */
                DrawLineEx((Vector2){p->pos.x, p->pos.y},
                           (Vector2){p->pos.x, p->pos.y - 6.0f * s},
                           1.2f, rgb8(60, 60, 66, 255));
                DrawCircleV((Vector2){p->pos.x, p->pos.y - 6.0f * s},
                            1.6f * s, rgb8(230, 220, 160, 255));
                break;
            }
            case NL_PROP_POLE:
                DrawLineEx((Vector2){p->pos.x, p->pos.y},
                           (Vector2){p->pos.x, p->pos.y - 8.0f * s},
                           1.4f, rgb8(80, 80, 80, 255));
                break;
            case NL_PROP_TREE: {
                float tb = p->pos.y - 4.0f * s;
                DrawLineEx((Vector2){p->pos.x, p->pos.y},
                           (Vector2){p->pos.x, tb}, s * 0.8f,
                           rgb8(90, 60, 35, 255));
                DrawCircleV((Vector2){p->pos.x + sway * 6.0f, tb - 3.0f * s},
                            3.0f * s, rgb8(p->r, p->g, p->b, 255));
                DrawCircleV((Vector2){p->pos.x - 2.0f * s + sway * 6.0f, tb - 1.0f * s},
                            2.0f * s, rgb8((uint8_t)(p->r*0.8f), p->g, (uint8_t)(p->b*0.8f), 255));
                break;
            }
            case NL_PROP_TRASH:
                DrawCircleV((Vector2){p->pos.x, p->pos.y}, 1.2f * s,
                            rgb8(p->r, p->g, p->b, 255));
                break;
            case NL_PROP_TYRE:
                DrawRing((Vector2){p->pos.x, p->pos.y}, 1.2f * s, 2.0f * s,
                         0.0f, 360.0f, 10, rgb8(35, 35, 38, 255));
                break;
            case NL_PROP_CRATE:
                DrawRectanglePro((Rectangle){p->pos.x - 1.5f*s, p->pos.y - 1.5f*s,
                                             3.0f*s, 3.0f*s},
                                 (Vector2){1.5f*s, 1.5f*s},
                                 p->rot * 180.0f / M_PI,
                                 rgb8(p->r, p->g, p->b, 255));
                DrawRectangleLinesEx((Rectangle){p->pos.x - 1.5f*s, p->pos.y - 1.5f*s,
                                                 3.0f*s, 3.0f*s},
                                     0.6f, rgb8((uint8_t)(p->r*0.6f),
                                                (uint8_t)(p->g*0.6f),
                                                (uint8_t)(p->b*0.6f), 255));
                break;
            case NL_PROP_BARREL:
                DrawCircleV((Vector2){p->pos.x, p->pos.y}, 2.0f * s,
                            rgb8(p->r, p->g, p->b, 255));
                DrawCircleV((Vector2){p->pos.x, p->pos.y}, 1.2f * s,
                            rgb8((uint8_t)(p->r*0.7f),
                                 (uint8_t)(p->g*0.7f),
                                 (uint8_t)(p->b*0.7f), 255));
                break;
            case NL_PROP_SIGNBOARD:
                DrawRectanglePro((Rectangle){p->pos.x - 3.0f*s, p->pos.y - 2.0f*s,
                                             6.0f*s, 4.0f*s},
                                 (Vector2){3.0f*s, 2.0f*s}, p->rot * 180.0f / M_PI,
                                 rgb8(p->r, p->g, p->b, 255));
                DrawRectangleLinesEx((Rectangle){p->pos.x - 3.0f*s, p->pos.y - 2.0f*s,
                                                 6.0f*s, 4.0f*s},
                                     0.8f, rgb8(20, 20, 20, 255));
                break;
            case NL_PROP_DRAIN:
                DrawRectanglePro((Rectangle){p->pos.x - 8.0f*s, p->pos.y - 1.5f*s,
                                             16.0f*s, 3.0f*s},
                                 (Vector2){8.0f*s, 1.5f*s},
                                 p->rot * 180.0f / M_PI,
                                 rgb8(p->r, p->g, p->b, 255));
                break;
            case NL_PROP_LAUNDRY_LINE: {
                float dx = cosf(p->rot + sway) * 6.0f * s;
                float dy = sinf(p->rot + sway) * 6.0f * s;
                DrawLineEx((Vector2){p->pos.x - dx, p->pos.y - 3.0f*s},
                           (Vector2){p->pos.x + dx, p->pos.y - 3.0f*s - dy},
                           0.8f, rgb8(200, 200, 200, 230));
                /* hanging clothes */
                DrawRectangle((int)(p->pos.x - dx*0.3f), (int)(p->pos.y - 3.0f*s),
                              3, 3, rgb8(200, 80, 80, 235));
                DrawRectangle((int)(p->pos.x + dx*0.2f), (int)(p->pos.y - 3.0f*s),
                              3, 3, rgb8(80, 120, 200, 235));
                break;
            }
            default:
                break;
        }
    }
    (void)dt;
}

/* ------------------------------------------------------------------ */
/*  Vehicles                                                          */
/* ------------------------------------------------------------------ */

static void nl_draw_vehicles(const NLGame *g, float vx0, float vy0, float vx1, float vy1)
{
    int i;
    for (i = 0; i < g->vehicle_count; ++i) {
        const NLVehicle *v = &g->vehicles[i];
        float len, wid, s;
        if (!v->active) continue;
        switch (v->kind) {
            case NL_VEH_MATATU:   len = 9.0f;  wid = 4.0f; break;
            case NL_VEH_BODA:     len = 3.0f;  wid = 1.4f; break;
            case NL_VEH_CAR:      len = 7.0f;  wid = 3.4f; break;
            case NL_VEH_LORRY:    len = 16.0f; wid = 5.0f; break;
            case NL_VEH_HANDCART: len = 3.0f;  wid = 2.0f; break;
            default:              len = 6.0f;  wid = 3.0f; break;
        }
        (void)s;
        if (!in_view(v->pos.x - len, v->pos.y - wid,
                     v->pos.x + len, v->pos.y + wid, vx0, vy0, vx1, vy1)) continue;

        float ang = v->angle * 180.0f / M_PI;
        Color body = rgb8(v->r, v->g, v->b, 255);

        /* Shadow. */
        DrawRectanglePro((Rectangle){ v->pos.x + 2.0f, v->pos.y + 2.0f, len, wid },
                         (Vector2){ len*0.5f, wid*0.5f }, ang, rgb8(0,0,0,60));
        /* Body. */
        DrawRectanglePro((Rectangle){ v->pos.x, v->pos.y, len, wid },
                         (Vector2){ len*0.5f, wid*0.5f }, ang, body);

        if (v->kind == NL_VEH_MATATU) {
            /* Vivid livery stripe. */
            Color stripe = rgb8(255, 210, 40, 255);
            DrawRectanglePro((Rectangle){ v->pos.x, v->pos.y, len, wid*0.35f },
                             (Vector2){ len*0.5f, wid*0.175f }, ang, stripe);
            /* windows */
            DrawRectanglePro((Rectangle){ v->pos.x + len*0.1f, v->pos.y,
                                          len*0.5f, wid*0.5f },
                             (Vector2){ len*0.25f, wid*0.25f }, ang,
                             rgb8(40, 40, 50, 230));
        } else if (v->kind == NL_VEH_BODA) {
            /* rider */
            DrawCircleV((Vector2){ v->pos.x, v->pos.y },
                        wid*0.5f, rgb8(60, 60, 70, 255));
        } else if (v->kind == NL_VEH_HANDCART) {
            DrawLineEx((Vector2){ v->pos.x, v->pos.y },
                       (Vector2){ v->pos.x + cosf(v->angle)*4.0f,
                                  v->pos.y + sinf(v->angle)*4.0f },
                       1.0f, rgb8(40, 40, 40, 255));
        } else if (v->kind == NL_VEH_LORRY) {
            DrawRectanglePro((Rectangle){ v->pos.x - len*0.35f, v->pos.y,
                                          len*0.3f, wid },
                             (Vector2){ len*0.15f, wid*0.5f }, ang,
                             rgb8(80, 80, 90, 255));
        }

        /* Headlight cones at night/rain when enabled. */
        if (v->headlights) {
            float hx = v->pos.x + cosf(v->angle) * len * 0.5f;
            float hy = v->pos.y + sinf(v->angle) * len * 0.5f;
            float spread = 0.35f;
            Vector2 tip = { hx + cosf(v->angle) * 26.0f,
                            hy + sinf(v->angle) * 26.0f };
            Vector2 l = { hx + cosf(v->angle - spread) * 10.0f,
                          hy + sinf(v->angle - spread) * 10.0f };
            Vector2 r = { hx + cosf(v->angle + spread) * 10.0f,
                          hy + sinf(v->angle + spread) * 10.0f };
            DrawTriangle((Vector2){hx,hy}, l, tip,
                         rgb8(255, 240, 190, 40));
            DrawTriangle((Vector2){hx,hy}, r, tip,
                         rgb8(255, 240, 190, 40));
            DrawCircleV((Vector2){hx, hy}, 1.2f, rgb8(255, 245, 200, 255));
        }
    }
}

/* ------------------------------------------------------------------ */
/*  NPCs                                                              */
/* ------------------------------------------------------------------ */

static Color nl_npc_cloth(const NLNpc *n)
{
    /* Uniform overrides for authority kinds. */
    switch (n->kind) {
        case NL_NPC_POLICE:  return rgb8(30, 50, 120, 255);
        case NL_NPC_ASKARI:  return rgb8(70, 90, 60, 255);
        default:             return rgb8(n->cloth_r, n->cloth_g, n->cloth_b, 255);
    }
}

static void nl_draw_npcs(const NLGame *g, float vx0, float vy0, float vx1, float vy1)
{
    int i;
    for (i = 0; i < g->npc_count; ++i) {
        const NLNpc *n = &g->npcs[i];
        float rad;
        float bob;
        if (!n->active) continue;
        rad = (n->kind == NL_NPC_STREET_KID) ? 1.4f : 2.0f;
        if (n->kind == NL_NPC_THUG || n->kind == NL_NPC_POLICE
            || n->kind == NL_NPC_ASKARI) rad = 2.3f;
        if (!in_view(n->pos.x - rad*2, n->pos.y - rad*2,
                     n->pos.x + rad*2, n->pos.y + rad*2, vx0, vy0, vx1, vy1)) continue;

        bob = sinf(n->bob_phase) * 0.6f;
        {
            Color cloth = nl_npc_cloth(n);
            Color skin  = rgb8(n->skin_r, n->skin_g, n->skin_b, 255);
            /* shadow */
            DrawCircleV((Vector2){ n->pos.x + 1.0f, n->pos.y + 1.5f },
                        rad, rgb8(0,0,0,55));
            /* body */
            DrawCircleV((Vector2){ n->pos.x, n->pos.y + bob }, rad, cloth);
            /* head */
            DrawCircleV((Vector2){ n->pos.x + cosf(n->facing)*rad*0.8f,
                                   n->pos.y + bob + sinf(n->facing)*rad*0.8f },
                        rad*0.55f, skin);
            /* facing indicator (small dot at front) */
            DrawCircleV((Vector2){ n->pos.x + cosf(n->facing)*rad*1.2f,
                                   n->pos.y + bob + sinf(n->facing)*rad*1.2f },
                        0.7f, rgb8(255,255,255,200));
            /* umbrella when raining */
            if (n->has_umbrella && g->weather.rain_mm_hr > 0.5f) {
                DrawLineEx((Vector2){ n->pos.x, n->pos.y - rad },
                           (Vector2){ n->pos.x, n->pos.y - rad - 5.0f },
                           0.8f, rgb8(120,120,120,255));
                DrawCircleV((Vector2){ n->pos.x, n->pos.y - rad - 5.0f },
                            rad*1.6f, rgb8(210, 80, 80, 230));
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Player                                                            */
/* ------------------------------------------------------------------ */

static void nl_draw_player(const NLGame *g, float vx0, float vy0, float vx1, float vy1)
{
    const NLPlayer *p = &g->player;
    if (!in_view(p->pos.x - 8, p->pos.y - 8, p->pos.x + 8, p->pos.y + 8,
                 vx0, vy0, vx1, vy1)) return;
    /* shadow */
    DrawCircleV((Vector2){ p->pos.x + 1.5f, p->pos.y + 2.0f }, 3.2f, rgb8(0,0,0,80));
    /* distinct body */
    DrawCircleV((Vector2){ p->pos.x, p->pos.y }, 3.0f, rgb8(0, 200, 255, 255));
    DrawCircleV((Vector2){ p->pos.x, p->pos.y - 1.0f }, 1.6f, rgb8(240, 200, 160, 255));
    /* facing indicator (arrow) */
    {
        float a = p->facing;
        Vector2 tip = { p->pos.x + cosf(a)*7.0f, p->pos.y + sinf(a)*7.0f };
        Vector2 l = { p->pos.x + cosf(a+2.6f)*3.0f, p->pos.y + sinf(a+2.6f)*3.0f };
        Vector2 r = { p->pos.x + cosf(a-2.6f)*3.0f, p->pos.y + sinf(a-2.6f)*3.0f };
        DrawTriangle(tip, l, r, rgb8(255, 255, 255, 255));
    }
    if (p->has_shelter_tonight == false && g->weather.rain_mm_hr > 0.5f
        && p->wetness > 0.1f) {
        /* nothing extra; wetness is abstract */
    }
}

/* ------------------------------------------------------------------ */
/*  Light accumulation pass                                           */
/* ------------------------------------------------------------------ */

static void nl_draw_light(const NLGame *g, float vx0, float vy0, float vx1, float vy1)
{
    const NLWeather *w = &g->weather;
    float ai = w->ambient_intensity;
    Color amb = rgb8((uint8_t)(w->ambient_r * 255),
                     (uint8_t)(w->ambient_g * 255),
                     (uint8_t)(w->ambient_b * 255),
                     (uint8_t)(ai * 255));

    /* Full-screen ambient wash (additive over black light buffer). */
    DrawRectangle(0, 0, (int)NL_WORLD_W, (int)NL_WORLD_H, amb);

    /* Streetlight pools. */
    {
        int i;
        for (i = 0; i < g->prop_count; ++i) {
            const NLProp *p = &g->props[i];
            if (p->kind != NL_PROP_STREETLIGHT) continue;
            if (!in_view(p->pos.x - 40, p->pos.y - 40,
                         p->pos.x + 40, p->pos.y + 40, vx0, vy0, vx1, vy1)) continue;
            DrawCircleGradient((int)p->pos.x, (int)(p->pos.y - 6.0f * p->scale),
                               34.0f, rgb8(255, 210, 130, 120), rgb8(255, 210, 130, 0));
        }
    }

    /* Vehicle headlight pools (bright at front). */
    {
        int i;
        for (i = 0; i < g->vehicle_count; ++i) {
            const NLVehicle *v = &g->vehicles[i];
            if (!v->active || !v->headlights) continue;
            float hx = v->pos.x + cosf(v->angle) * 6.0f;
            float hy = v->pos.y + sinf(v->angle) * 6.0f;
            if (!in_view(hx - 30, hy - 30, hx + 30, hy + 30, vx0, vy0, vx1, vy1)) continue;
            DrawCircleGradient((int)hx, (int)hy, 28.0f,
                               rgb8(255, 240, 190, 110), rgb8(255, 240, 190, 0));
        }
    }

    /* Lit windows pools (night). */
    if (!g->clock.is_daylight) {
        int i;
        for (i = 0; i < g->building_count; ++i) {
            const NLBuilding *b = &g->buildings[i];
            if (!b->lit_at_night) continue;
            if (b->kind != NL_BLD_TOWER && b->kind != NL_BLD_APARTMENT) continue;
            if (!in_view(b->pos.x, b->pos.y, b->pos.x + b->size.x, b->pos.y + b->size.y,
                         vx0, vy0, vx1, vy1)) continue;
            DrawCircleGradient((int)(b->pos.x + b->size.x*0.5f),
                               (int)(b->pos.y + b->size.y*0.5f),
                               fmaxf(20.0f, b->size.x * 0.6f),
                               rgb8(255, 220, 130, 60), rgb8(255, 220, 130, 0));
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Rain drawing + splashes                                          */
/* ------------------------------------------------------------------ */

static void nl_draw_rain(const NLGame *g, float vx0, float vy0, float vx1, float vy1)
{
    int i;
    const NLWeather *w = &g->weather;
    if (w->rain_mm_hr < 0.2f) return;

    /* Angled streaks from wind. */
    float wrad = w->wind_dir_deg * M_PI / 180.0f;
    float wspeed = w->wind_speed_ms;
    /* Rain falls downward; wind pushes it sideways. */
    float dirx = sinf(wrad) * (wspeed * 0.04f) + 0.0f;
    float diry = 1.0f;
    float dl = sqrtf(dirx*dirx + diry*diry);
    dirx /= dl; diry /= dl;

    BeginBlendMode(BLEND_ALPHA);
    for (i = 0; i < g_rain_count; ++i) {
        const NLRainDrop *d = &g_rain[i];
        if (!d->active) continue;
        if (d->x < vx0 || d->x > vx1 || d->y < vy0 || d->y > vy1) continue;
        Vector2 a = { d->x, d->y };
        Vector2 b = { d->x - dirx * d->len, d->y - diry * d->len };
        DrawLineEx(a, b, 1.0f, rgb8(170, 190, 210, 150));
    }
    EndBlendMode();

    /* Splash rings (derived from rain clock + ground). */
    {
        int ns = (int)(w->rain_mm_hr * 1.5f);
        if (ns > 60) ns = 60;
        uint32_t seed = (uint32_t)(g->real_seconds * 1000.0);
        for (i = 0; i < ns; ++i) {
            float x = vx0 + nl_randf(&seed) * (vx1 - vx0);
            float y = vy0 + nl_randf(&seed) * (vy1 - vy0);
            float ph = (g_rain_time * 2.0f + nl_randf(&seed)) - (float)((int)(g_rain_time*2.0f));
            if (ph < 0) ph += 1.0f;
            DrawRing((Vector2){x, y}, 0.5f + ph * 2.5f, 1.0f + ph * 3.0f,
                     0.0f, 360.0f, 8, rgb8(200, 220, 240, (uint8_t)(120 * (1.0f - ph))));
        }
    }
}

/* ------------------------------------------------------------------ */
/*  HUD / UI                                                          */
/* ------------------------------------------------------------------ */

static void nl_bar(float x, float y, float w, float h, float v, Color c, bool crit)
{
    DrawRectangleRounded((Rectangle){ x, y, w, h }, 0.3f, 6, rgb8(20,20,24,200));
    float fw = (w - 4.0f) * clampf_local(v/100.0f, 0.0f, 1.0f);
    if (fw > 0) {
        Color col = c;
        if (crit && (((int)(g_rain_time * 4.0f) & 1) == 0)) col = rgb8(255, 60, 60, 255);
        DrawRectangleRounded((Rectangle){ x + 2.0f, y + 2.0f, fw, h - 4.0f },
                             0.3f, 6, col);
    }
    DrawRectangleRoundedLines((Rectangle){ x, y, w, h }, 0.3f, 6, rgb8(90,90,100,200));
}

static void nl_text(const char *t, float x, float y, float size, Color c)
{
    if (t == NULL) return;
    DrawTextEx(g_font, t, (Vector2){ x, y }, size, 1.0f, c);
}

static void nl_textf(float x, float y, float size, Color c, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    nl_text(buf, x, y, size, c);
}

static void nl_draw_hud(const NLGame *g)
{
    const NLPlayer *p = &g->player;
    const NLWeather *w = &g->weather;
    char buf[256];
    int i;

    /* ---- Top bar ---- */
    DrawRectangle(0, 0, NL_SCREEN_W, 34, rgb8(10, 12, 16, 210));
    nl_clock_format(&g->clock, buf, sizeof(buf));
    nl_text(buf, 12, 8, 18, rgb8(235, 235, 235, 255));
    nl_textf(260, 8, 16, rgb8(180, 200, 255, 255),
             "Day %d", g->clock.day_index + 1);
    nl_textf(380, 8, 16, rgb8(200, 220, 200, 255),
             "%s", nl_season_name(w->season));
    nl_textf(560, 8, 16, rgb8(210, 210, 210, 255),
             "%s", nl_sky_name(w->sky));
    nl_textf(760, 8, 16, rgb8(255, 220, 150, 255),
             "%.1fC", w->temperature_c);
    nl_textf(900, 8, 16, rgb8(150, 200, 255, 255),
             "Rain %.1fmm/h", w->rain_mm_hr);
    nl_textf(1080, 8, 16, rgb8(180, 220, 255, 255),
             "Wind %.1f m/s", w->wind_speed_ms);

    /* ---- Need bars (left) ---- */
    {
        const char *names[8] = { "HP","NRG","HUN","THR","HYG","MOR","WRM","BLA" };
        float vals[8] = { p->health, p->energy, p->hunger, p->thirst,
                          p->hygiene, p->morale, p->warmth, p->bladder };
        Color cols[8] = { rgb8(220,60,60,255), rgb8(220,180,60,255),
                          rgb8(200,120,60,255), rgb8(60,160,220,255),
                          rgb8(120,200,180,255), rgb8(200,120,200,255),
                          rgb8(220,120,60,255), rgb8(180,180,200,255) };
        float x = 12, y = 46;
        for (i = 0; i < 8; ++i) {
            bool crit = vals[i] < 20.0f;
            nl_text(names[i], x, y, 14, rgb8(200,200,200,255));
            nl_bar(x + 34, y, 130, 16, vals[i], cols[i], crit);
            y += 20;
        }
    }

    /* ---- Money (bottom-left) ---- */
    {
        float y = NL_SCREEN_H - 70;
        DrawRectangle(10, y - 6, 250, 70, rgb8(10, 12, 16, 200));
        nl_textf(18, y, 15, rgb8(120, 255, 160, 255), "Cash: KSh %d", p->cash);
        nl_textf(18, y + 18, 15, rgb8(120, 200, 255, 255), "M-Pesa: KSh %d", p->mpesa);
        nl_textf(18, y + 36, 15, rgb8(255, 120, 120, 255), "Debt: KSh %d", p->debt);
    }

    /* ---- Active job panel (bottom-center) ---- */
    if (g->active_job >= 0 && g->active_job < g->job_count) {
        const NLJob *j = &g->jobs[g->active_job];
        float w = 360, x = (float)NL_SCREEN_W * 0.5f - w * 0.5f;
        float y = NL_SCREEN_H - 70;
        DrawRectangle(x - 8, y - 6, w + 16, 70, rgb8(10, 12, 16, 210));
        nl_textf(x, y, 15, rgb8(255, 230, 140, 255), "%s", j->name);
        nl_textf(x, y + 18, 13, rgb8(200, 200, 200, 255),
                 "Pay KSh %d-%d   %.1fh", j->pay_min, j->pay_max,
                 g->job_elapsed_hours);
        nl_bar(x, y + 36, w, 16, g->job_progress * 100.0f,
               rgb8(120, 220, 120, 255), false);
    }

    /* ---- Interaction prompt (center-bottom) ---- */
    if (g->interact.available && g->interact.kind != NL_INT_NONE) {
        float x = (float)NL_SCREEN_W * 0.5f;
        float tw = MeasureTextEx(g_font, g->interact.label, 18, 1.0f).x;
        float bx = x - tw * 0.5f - 14;
        float y = NL_SCREEN_H - 130;
        DrawRectangle(bx, y - 6, tw + 28, 30, rgb8(20, 40, 60, 230));
        DrawRectangleLinesEx((Rectangle){ bx, y - 6, tw + 28, 30 }, 1.0f,
                             rgb8(120, 200, 255, 255));
        nl_text(g->interact.label, bx + 14, y, 18, rgb8(230, 245, 255, 255));
    }

    /* ---- Message log (right side) ---- */
    {
        float x = NL_SCREEN_W - 360;
        float y = 46;
        Color kcol[5] = {
            rgb8(200, 200, 200, 255),  /* info */
            rgb8(120, 255, 120, 255),  /* good */
            rgb8(255, 220, 120, 255),  /* warn */
            rgb8(255, 110, 110, 255),  /* bad */
            rgb8(120, 220, 255, 255)   /* money */
        };
        for (i = 0; i < g->log.count; ++i) {
            int idx = (g->log.head - g->log.count + 1 + i + NL_MAX_LOG_LINES) % NL_MAX_LOG_LINES;
            const NLLogLine *L = &g->log.lines[idx];
            float a = clampf_local(1.0f - L->age / L->ttl, 0.0f, 1.0f);
            Color c = kcol[(int)L->kind];
            c.a = (uint8_t)(a * 255);
            nl_text(L->text, x, y, 14, c);
            y += 17;
            if (y > NL_SCREEN_H - 40) break;
        }
    }

    /* ---- Minimap (top-right) ---- */
    {
        float mw = 220, mh = 220;
        float mx = NL_SCREEN_W - mw - 12;
        float my = 40;
        float sx = mw / NL_WORLD_W;
        float sy = mh / NL_WORLD_H;
        DrawRectangle(mx, my, mw, mh, rgb8(8, 10, 14, 220));
        DrawRectangleLinesEx((Rectangle){ mx, my, mw, mh }, 1.0f,
                             rgb8(80, 90, 110, 255));
        /* district tints */
        {
            int k;
            static const NLDistrict dlist[6] = {
                NL_DIST_KIBERA, NL_DIST_CBD, NL_DIST_INDUSTRIAL,
                NL_DIST_MARKET, NL_DIST_ESTATE, NL_DIST_ROADSIDE
            };
            static const Color dcol[6] = {
                {90, 50, 30, 255}, {90, 90, 110, 255}, {110, 100, 80, 255},
                {120, 80, 40, 255}, {50, 80, 50, 255}, {60, 80, 60, 255}
            };
            static const float drx[6] = {0, 1700, 2700, 0, 1700, 2700};
            static const float dry[6] = {2200, 1700, 2200, 1700, 0, 1700};
            static const float drw[6] = {1700, 1000, 1300, 1700, 2300, 1300};
            static const float drh[6] = {1800, 1000, 1800, 500, 1700, 500};
            for (k = 0; k < 6; ++k) {
                (void)dlist[k];
                DrawRectangle((int)(mx + drx[k]*sx), (int)(my + dry[k]*sy),
                              (int)(drw[k]*sx), (int)(drh[k]*sy), dcol[k]);
            }
        }
        /* player */
        DrawCircle(mx + g->player.pos.x*sx, my + g->player.pos.y*sy, 3,
                   rgb8(0, 230, 255, 255));
        /* nearby NPCs */
        for (i = 0; i < g->npc_count; ++i) {
            const NLNpc *n = &g->npcs[i];
            if (!n->active) continue;
            if (nl_vec_dist(n->pos, g->player.pos) > 600.0f) continue;
            DrawCircle(mx + n->pos.x*sx, my + n->pos.y*sy, 1,
                       rgb8(220, 220, 220, 200));
        }
    }

    /* ---- Help & debug overlays ---- */
    if (g->show_help) {
        float w = 360, h = 300;
        float x = (NL_SCREEN_W - w) * 0.5f;
        float y = (NL_SCREEN_H - h) * 0.5f;
        DrawRectangle(x, y, w, h, rgb8(8, 10, 14, 235));
        DrawRectangleLinesEx((Rectangle){ x, y, w, h }, 1.0f, rgb8(120,180,255,255));
        nl_textf(x + 14, y + 10, 18, rgb8(255,255,255,255), "CONTROLS");
        nl_text("WASD  move", x + 14, y + 44, 14, rgb8(220,220,220,255));
        nl_text("Shift sprint", x + 14, y + 64, 14, rgb8(220,220,220,255));
        nl_text("E     interact", x + 14, y + 84, 14, rgb8(220,220,220,255));
        nl_text("Tab   map", x + 14, y + 104, 14, rgb8(220,220,220,255));
        nl_text("J     jobs", x + 14, y + 124, 14, rgb8(220,220,220,255));
        nl_text("F1    help", x + 14, y + 144, 14, rgb8(220,220,220,255));
        nl_text("F3    debug", x + 14, y + 164, 14, rgb8(220,220,220,255));
        nl_text("P     pause", x + 14, y + 184, 14, rgb8(220,220,220,255));
        nl_text("Esc   menu", x + 14, y + 204, 14, rgb8(220,220,220,255));
        nl_text("F3: toggle debug  F1: toggle help", x + 14, y + 234, 14, rgb8(160,200,255,255));
        nl_text("Press F1 to close", x + 14, y + 254, 14, rgb8(160,200,255,255));
    }

    if (g->show_debug) {
        float x = NL_SCREEN_W - 360, y = NL_SCREEN_H - 230;
        DrawRectangle(x, y, 348, 220, rgb8(0, 0, 0, 200));
        nl_textf(x + 8, y + 6, 13, rgb8(120, 255, 120, 255), "FPS: %d  frame %d",
                 GetFPS(), g->frame_counter);
        nl_textf(x + 8, y + 24, 13, rgb8(200, 255, 200, 255),
                 "NPC %d  Veh %d  Bld %d  Prop %d",
                 g->npc_count, g->vehicle_count, g->building_count, g->prop_count);
        nl_textf(x + 8, y + 42, 13, rgb8(200, 255, 200, 255),
                 "Puddle %d  Rain %d", g->puddle_count, g_rain_count);
        nl_textf(x + 8, y + 60, 13, rgb8(200, 255, 200, 255),
                 "amb %.2f,%.2f,%.2f int %.2f", w->ambient_r, w->ambient_g,
                 w->ambient_b, w->ambient_intensity);
        nl_textf(x + 8, y + 78, 13, rgb8(200, 255, 200, 255),
                 "ground_wet %.2f mud %.2f", w->ground_wetness, w->mud_factor);
        nl_textf(x + 8, y + 96, 13, rgb8(200, 255, 200, 255),
                 "lightning %.2f vis %.0f", w->lightning_flash, w->visibility_m);
        nl_textf(x + 8, y + 114, 13, rgb8(200, 255, 200, 255),
                 "wind %.1f @ %.0f deg", w->wind_speed_ms, w->wind_dir_deg);
        nl_textf(x + 8, y + 132, 13, rgb8(200, 255, 200, 255),
                 "temp %.1f hum %.0f%% cloud %.0f%%",
                 w->temperature_c, w->humidity*100, w->cloud_cover*100);
        nl_textf(x + 8, y + 150, 13, rgb8(200, 255, 200, 255),
                 "dist %s", nl_district_name(
                     nl_world_district_at(g, g->player.pos)));
        nl_textf(x + 8, y + 168, 13, rgb8(200, 255, 200, 255),
                 "player hp %.0f wet %.2f", p->health, p->wetness);
        nl_textf(x + 8, y + 186, 13, rgb8(200, 255, 200, 255),
                 "rep %.0f heat %.0f arrests %d", p->reputation,
                 p->police_heat, p->arrests);
    }
}

/* ------------------------------------------------------------------ */
/*  Full-screen overlays (mist, lightning)                            */
/* ------------------------------------------------------------------ */

static void nl_draw_weather_overlays(const NLGame *g)
{
    const NLWeather *w = &g->weather;

    /* Lightning: flash whole screen white. */
    if (w->lightning_flash > 0.001f) {
        DrawRectangle(0, 0, NL_SCREEN_W, NL_SCREEN_H,
                      rgb8(255, 255, 255, (uint8_t)(w->lightning_flash * 220)));
    }

    /* Mist / fog (NL_SKY_MIST and cool dry season). */
    if (w->sky == NL_SKY_MIST || w->season == NL_SEASON_COOL_DRY) {
        float fog = 0.18f;
        if (w->sky == NL_SKY_MIST) fog = 0.34f;
        DrawRectangle(0, 0, NL_SCREEN_W, NL_SCREEN_H,
                      rgb8(200, 210, 215, (uint8_t)(fog * 255)));
    }

    /* Heavy rain vignette darkening. */
    if (w->rain_mm_hr > 6.0f) {
        DrawRectangle(0, 0, NL_SCREEN_W, NL_SCREEN_H,
                      rgb8(20, 30, 45, (uint8_t)(clampf_local(w->rain_mm_hr/40.0f, 0, 1)*120)));
    }
}

/* ------------------------------------------------------------------ */
/*  Screen overlays (title/paused/sleeping/summary/gameover)          */
/* ------------------------------------------------------------------ */

static void nl_center_text(const char *t, float y, float size, Color c)
{
    float tw = MeasureTextEx(g_font, t, size, 1.0f).x;
    nl_text(t, (float)NL_SCREEN_W * 0.5f - tw * 0.5f, y, size, c);
}

static void nl_draw_screens(const NLGame *g)
{
    switch (g->screen) {
        case NL_SCREEN_TITLE: {
            DrawRectangle(0, 0, NL_SCREEN_W, NL_SCREEN_H, rgb8(8, 10, 16, 255));
            nl_center_text("NAIROBI LIFE", 220, 64, rgb8(255, 200, 90, 255));
            nl_center_text("A realistic Nairobi street-life survival game", 300, 22,
                           rgb8(220, 220, 220, 255));
            nl_center_text("Survive the streets. Hustle. Eat. Sleep. Repeat.", 340, 18,
                           rgb8(180, 200, 220, 255));
            nl_center_text("Press ENTER to begin", 460, 26, rgb8(120, 255, 160, 255));
            nl_center_text("WASD move | E interact | J jobs | Tab map | F1 help | Esc",
                           520, 16, rgb8(150, 170, 200, 255));
            break;
        }
        case NL_SCREEN_PAUSED: {
            DrawRectangle(0, 0, NL_SCREEN_W, NL_SCREEN_H, rgb8(0, 0, 0, 150));
            nl_center_text("PAUSED", 380, 56, rgb8(255, 255, 255, 255));
            nl_center_text("Press P to resume", 450, 22, rgb8(200, 200, 200, 255));
            break;
        }
        case NL_SCREEN_SLEEPING: {
            DrawRectangle(0, 0, NL_SCREEN_W, NL_SCREEN_H, rgb8(4, 6, 16, 235));
            nl_center_text("Sleeping...", 400, 48, rgb8(180, 200, 255, 255));
            nl_center_text("Press SPACE to wake", 460, 20, rgb8(150, 170, 220, 255));
            break;
        }
        case NL_SCREEN_DAY_SUMMARY: {
            DrawRectangle(0, 0, NL_SCREEN_W, NL_SCREEN_H, rgb8(8, 10, 16, 225));
            nl_center_text("DAY SUMMARY", 140, 48, rgb8(255, 210, 100, 255));
            nl_textf((float)NL_SCREEN_W * 0.5f - 200, 240, 24,
                     rgb8(120, 255, 160, 255), "Earnings:  KSh %d", g->day_earnings);
            nl_textf((float)NL_SCREEN_W * 0.5f - 200, 290, 24,
                     rgb8(255, 120, 120, 255), "Spending:  KSh %d", g->day_spending);
            nl_textf((float)NL_SCREEN_W * 0.5f - 200, 340, 24,
                     rgb8(255, 235, 140, 255), "Net:       KSh %d",
                     g->day_earnings - g->day_spending);
            nl_textf((float)NL_SCREEN_W * 0.5f - 200, 390, 20,
                     rgb8(200, 220, 255, 255), "Worked:    %.1f h",
                     g->day_worked_hours);
            nl_center_text("Press ENTER for a new day", 480, 22,
                           rgb8(120, 255, 160, 255));
            break;
        }
        case NL_SCREEN_GAMEOVER: {
            DrawRectangle(0, 0, NL_SCREEN_W, NL_SCREEN_H, rgb8(20, 4, 4, 235));
            nl_center_text("GAME OVER", 320, 64, rgb8(255, 90, 90, 255));
            nl_textf((float)NL_SCREEN_W * 0.5f - 200, 420, 22,
                     rgb8(220, 220, 220, 255), "You survived %d days.",
                     g->player.days_survived);
            nl_center_text("Press ENTER to restart", 480, 24,
                           rgb8(255, 200, 120, 255));
            break;
        }
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

void nl_render_init(void)
{
    if (g_inited) return;
    /* Light buffers are SCREEN sized, not world sized. A 4000x4000 pair of
     * RGBA targets would burn ~128 MB of VRAM and force the GPU to shade the
     * entire 4 km world every frame, which would defeat the view culling. */
    g_lit   = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    g_light = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    g_font = GetFontDefault();
    g_rain_count = 0;
    memset(g_rain, 0, sizeof(g_rain));
    g_inited = true;
}

void nl_render_shutdown(void)
{
    if (!g_inited) return;
    UnloadRenderTexture(g_lit);
    UnloadRenderTexture(g_light);
    g_inited = false;
}

/* Rain particle update. Spawns proportional to rain intensity, drifts with
 * wind, respawns on expiry or when it hits the ground. */
void nl_render_rain_update(NLGame *g, float dt)
{
    const NLWeather *w = &g->weather;
    float rate = w->rain_mm_hr * 12.0f;     /* drops/sec at full intensity */
    int to_spawn;
    int i;

    g_rain_time += dt;

    /* Determine target active count from intensity. */
    int target = (int)(w->rain_mm_hr * 220.0f);
    if (target > NL_MAX_RAINDROPS) target = NL_MAX_RAINDROPS;

    /* Spawn accumulator for fractional rates. */
    g_spawn_accum += rate * dt;
    to_spawn = (int)g_spawn_accum;
    g_spawn_accum -= (float)to_spawn;

    /* Ensure at least the target number of active drops exist. */
    if (g_rain_count < target) to_spawn += (target - g_rain_count);

    /* Wind vector (rain falls down, pushed sideways by wind). */
    float wrad = w->wind_dir_deg * M_PI / 180.0f;
    float wx = sinf(wrad) * w->wind_speed_ms * 6.0f;
    float wy = 380.0f + w->wind_speed_ms * 4.0f;

    /* Respawn expired / inactive drops. */
    for (i = 0; i < NL_MAX_RAINDROPS && to_spawn > 0; ++i) {
        if (g_rain[i].active) continue;
        g_rain[i].x = g->camera.x + ((float)NL_SCREEN_W / g->camera_zoom)
                      * (nl_randf(&g->rng_state) - 0.5f) * 1.6f;
        g_rain[i].y = g->camera.y + ((float)NL_SCREEN_H / g->camera_zoom)
                      * (nl_randf(&g->rng_state) - 0.5f) * 1.6f
                      - ((float)NL_SCREEN_H / g->camera_zoom) * 0.6f;
        g_rain[i].vx = wx;
        g_rain[i].vy = wy;
        g_rain[i].len = 6.0f + w->rain_mm_hr * 0.4f;
        g_rain[i].life = 2.0f;
        g_rain[i].active = true;
        if (g_rain_count <= i) g_rain_count = i + 1;
        to_spawn--;
    }

    /* Integrate active drops (world space). */
    for (i = 0; i < g_rain_count; ++i) {
        if (!g_rain[i].active) continue;
        g_rain[i].x += g_rain[i].vx * dt;
        g_rain[i].y += g_rain[i].vy * dt;
        g_rain[i].life -= dt;
        /* Respawn when it falls out of the visible area. */
        float halfW = ((float)NL_SCREEN_W * 0.5f) / g->camera_zoom;
        float halfH = ((float)NL_SCREEN_H * 0.5f) / g->camera_zoom;
        if (g_rain[i].life <= 0.0f ||
            g_rain[i].y > g->camera.y + halfH + 20.0f ||
            g_rain[i].x < g->camera.x - halfW - 40.0f ||
            g_rain[i].x > g->camera.x + halfW + 40.0f) {
            g_rain[i].active = false;
        }
    }
    /* Compact count. */
    while (g_rain_count > 0 && !g_rain[g_rain_count - 1].active) g_rain_count--;
}

void nl_render_frame(NLGame *g)
{
    Camera2D cam;
    float vx0, vy0, vx1, vy1;
    int sw, sh;

    if (g == NULL) return;

    /* Keep the light buffers matched to the framebuffer size. */
    sw = GetScreenWidth();
    sh = GetScreenHeight();
    if (g_inited && (g_lit.texture.width != sw || g_lit.texture.height != sh)) {
        UnloadRenderTexture(g_lit);
        UnloadRenderTexture(g_light);
        g_lit   = LoadRenderTexture(sw, sh);
        g_light = LoadRenderTexture(sw, sh);
    }

    BeginDrawing();
    ClearBackground(rgb8(0, 0, 0, 255));

    /* Compute visible world rect for culling. */
    nl_view_rect(g, 40.0f, &vx0, &vy0, &vx1, &vy1);
    cam = nl_make_camera(g);

    /* ---- Pass 1: lit scene into the screen-sized g_lit ---- */
    BeginTextureMode(g_lit);
    ClearBackground(nl_surface_color(NL_SURF_DIRT, g->weather.ground_wetness));
    BeginMode2D(cam);
    nl_draw_ground(g, vx0, vy0, vx1, vy1);
    nl_draw_roads(g, vx0, vy0, vx1, vy1);
    nl_draw_puddles(g, vx0, vy0, vx1, vy1);
    nl_draw_buildings(g, vx0, vy0, vx1, vy1);
    nl_draw_props(g, vx0, vy0, vx1, vy1, 0.0f);
    nl_draw_vehicles(g, vx0, vy0, vx1, vy1);
    nl_draw_npcs(g, vx0, vy0, vx1, vy1);
    nl_draw_player(g, vx0, vy0, vx1, vy1);
    EndMode2D();
    EndTextureMode();

    /* ---- Pass 2: light accumulation into the screen-sized g_light ---- */
    BeginTextureMode(g_light);
    ClearBackground(rgb8(0, 0, 0, 255));
    BeginMode2D(cam);
    BeginBlendMode(BLEND_ADDITIVE);
    nl_draw_light(g, vx0, vy0, vx1, vy1);
    EndBlendMode();
    EndMode2D();
    EndTextureMode();

    /* ---- Pass 3: compose lit * light. Both targets are already in screen
     * space, so this must NOT be inside BeginMode2D or it would be
     * transformed a second time. Render textures are y-flipped. ---- */
    {
        Rectangle src = { 0.0f, 0.0f, (float)sw, -(float)sh };
        Vector2   org = { 0.0f, 0.0f };

        DrawTextureRec(g_lit.texture, src, org, WHITE);

        BeginBlendMode(BLEND_MULTIPLIED);
        DrawTextureRec(g_light.texture, src, org, WHITE);
        EndBlendMode();
    }

    /* Rain sits above the lit world, in world space for depth. */
    BeginMode2D(cam);
    nl_draw_rain(g, vx0, vy0, vx1, vy1);
    EndMode2D();

    /* ---- Screen-space UI ---- */
    nl_draw_hud(g);
    nl_draw_weather_overlays(g);
    nl_draw_screens(g);

    EndDrawing();
}
