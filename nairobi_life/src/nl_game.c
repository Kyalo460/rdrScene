/* nl_game.c - Nairobi Life: game state orchestration, input, and simulation tick.
 *
 * Owns the top-level game loop logic: advances the clock, drives the weather,
 * world, NPCs, vehicles and economy, resolves player movement against the
 * world, and finds the nearest interactable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "raylib.h"
#include "nl_core.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Player physical constants. Realistic human locomotion: a person walks at
 * roughly 1.4 m/s and can jog at about 3.5 m/s. Carrying stock slows you. */
#define NL_WALK_SPEED    1.45f
#define NL_SPRINT_SPEED  3.60f
#define NL_PLAYER_RADIUS 0.45f

/* ---------------------------------------------------------------------- */

static void nl_player_init(NLPlayer *p)
{
    memset(p, 0, sizeof(*p));

    /* Start in Kibera, which the world generator places in the south-west. */
    p->pos.x = 850.0f;
    p->pos.y = 3100.0f;
    p->facing = 0.0f;

    p->health  = 85.0f;
    p->energy  = 70.0f;
    p->hunger  = 55.0f;
    p->thirst  = 60.0f;
    p->hygiene = 65.0f;
    p->morale  = 55.0f;
    p->warmth  = 70.0f;
    p->bladder = 20.0f;

    /* You wake up with almost nothing: a few coins and rent already owed.
     * A Kibera single room runs ~KSh 2,000-3,000/month (Mathare budget study),
     * so the daily rent burden is small but relentless. */
    p->cash  = 120;
    p->mpesa = 0;
    p->debt  = 600;

    p->reputation = 10.0f;
    p->police_heat = 0.0f;

    p->illness = NL_ILL_NONE;
    p->illness_severity = 0.0f;
    p->wetness = 0.0f;
    p->injury = 0.0f;

    p->stock_water = 0;
    p->stock_sweets = 0;
    p->stock_scrap_kg = 0;

    p->has_shelter_tonight = false;
    p->days_survived = 0;
    p->sleep_debt = 2.0f;
    p->last_meal_hour = 0.0f;
}

/* ---------------------------------------------------------------------- */

void nl_game_init(NLGame *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));

    g->rng_state = seed ? seed : 0x1BADB002u;
    g->screen = NL_SCREEN_TITLE;
    g->time_scale = NL_TIME_SCALE_DEFAULT;
    g->camera_zoom = 12.0f;
    g->active_job = -1;
    g->show_debug = false;
    g->show_help = false;

    nl_log_init(&g->log);

    /* Start in April: the peak of the long rains, so the player meets
     * Nairobi's most punishing weather immediately. */
    nl_clock_init(&g->clock, 2026, 4, 6, 6.0f);

    nl_weather_init(&g->weather, &g->clock, g->rng_state);
    nl_world_generate(g, seed);
    nl_npc_spawn_all(g);
    nl_vehicle_spawn_all(g);

    nl_player_init(&g->player);
    nl_econ_init(g);
    nl_jobs_generate(g);

    g->day_start_cash = g->player.cash;
    g->camera = g->player.pos;

    nl_log_push(&g->log, NL_MSG_INFO, "Nairobi, %s %d. You wake up in Kibera.",
                nl_month_name(g->clock.month), g->clock.year);
    nl_log_push(&g->log, NL_MSG_WARN, "Rent owed: KSh %d. Find work.", g->player.debt);
}

/* ---------------------------------------------------------------------- */

static void nl_update_interaction(NLGame *g)
{
    NLInteraction *it = &g->interact;
    it->kind = NL_INT_NONE;
    it->available = false;
    it->target_index = -1;
    it->label[0] = '\0';

    NLPlayer *p = &g->player;

    /* Nearest job site wins if we are standing in it and are not already
     * working. Job radii are small so this is unambiguous. */
    if (g->active_job < 0) {
        float best = 1e9f;
        int   best_i = -1;
        for (int i = 0; i < g->job_count; i++) {
            const NLJob *j = &g->jobs[i];
            if (!j->available) continue;
            float d = nl_vec_dist(p->pos, j->pos);
            if (d < j->radius && d < best) { best = d; best_i = i; }
        }
        if (best_i >= 0) {
            it->kind = NL_INT_JOB;
            it->target_index = best_i;
            it->available = true;
            snprintf(it->label, sizeof(it->label), "[E] Work: %s (KSh %d-%d)",
                     g->jobs[best_i].name,
                     g->jobs[best_i].pay_min, g->jobs[best_i].pay_max);
            return;
        }
    }

    /* Otherwise look for a nearby useful building. */
    float best = 1e9f;
    int   best_i = -1;
    for (int i = 0; i < g->building_count; i++) {
        const NLBuilding *b = &g->buildings[i];
        if (!b->enterable) continue;
        if (b->kind != NL_BLD_KIOSK && b->kind != NL_BLD_SHOP &&
            b->kind != NL_BLD_CLINIC && b->kind != NL_BLD_WATER_POINT &&
            b->kind != NL_BLD_SHACK)
            continue;

        NLVec2 c;
        c.x = b->pos.x + b->size.x * 0.5f;
        c.y = b->pos.y + b->size.y * 0.5f;
        float reach = 0.5f * (b->size.x + b->size.y) * 0.5f + 2.5f;
        float d = nl_vec_dist(p->pos, c);
        if (d < reach && d < best) { best = d; best_i = i; }
    }

    if (best_i < 0) return;

    const NLBuilding *b = &g->buildings[best_i];
    it->target_index = best_i;
    it->available = true;

    switch (b->kind) {
    case NL_BLD_KIOSK:
        it->kind = NL_INT_BUY_FOOD;
        snprintf(it->label, sizeof(it->label),
                 "[E] Kibanda: ugali & beans KSh %d", g->prices.ugali_beans_meal);
        break;
    case NL_BLD_SHOP:
        it->kind = NL_INT_BUY_STOCK;
        snprintf(it->label, sizeof(it->label),
                 "[E] Duka: buy water stock KSh %d each", g->prices.water_bottle_buy);
        break;
    case NL_BLD_WATER_POINT:
        it->kind = NL_INT_BUY_WATER;
        snprintf(it->label, sizeof(it->label),
                 "[E] Water point: 20L jerrycan KSh %d", g->prices.water_jerrycan);
        break;
    case NL_BLD_CLINIC:
        it->kind = NL_INT_CLINIC;
        snprintf(it->label, sizeof(it->label),
                 "[E] Clinic: consultation KSh %d", g->prices.clinic_visit);
        break;
    case NL_BLD_SHACK:
        it->kind = NL_INT_SLEEP;
        snprintf(it->label, sizeof(it->label), "[E] Sleep here until morning");
        break;
    default:
        it->available = false;
        break;
    }
}

/* ---------------------------------------------------------------------- */

static void nl_do_interaction(NLGame *g)
{
    NLInteraction *it = &g->interact;
    if (!it->available) return;

    NLPlayer *p = &g->player;

    switch (it->kind) {
    case NL_INT_JOB:
        nl_job_start(g, it->target_index);
        break;

    case NL_INT_BUY_FOOD:
        nl_player_eat(g, 0);
        break;

    case NL_INT_BUY_STOCK: {
        /* Buy as many bottles as affordable, capped so the player cannot
         * carry an unrealistic load. A hawker's crate is about 24 bottles. */
        int want = 24 - p->stock_water;
        if (want <= 0) {
            nl_log_push(&g->log, NL_MSG_WARN, "You cannot carry more stock.");
            break;
        }
        int afford = p->cash / (g->prices.water_bottle_buy > 0
                                ? g->prices.water_bottle_buy : 1);
        int n = (afford < want) ? afford : want;
        if (n <= 0) {
            nl_log_push(&g->log, NL_MSG_BAD, "Not enough cash for stock.");
            break;
        }
        nl_player_spend(g, n * g->prices.water_bottle_buy, "water stock");
        p->stock_water += n;
        nl_log_push(&g->log, NL_MSG_GOOD, "Bought %d bottles to hawk.", n);
        break;
    }

    case NL_INT_BUY_WATER:
        if (p->cash < g->prices.water_jerrycan) {
            nl_log_push(&g->log, NL_MSG_BAD, "No cash for water.");
            break;
        }
        nl_player_spend(g, g->prices.water_jerrycan, "water");
        p->thirst = nl_clampf(p->thirst + 55.0f, 0.0f, 100.0f);
        p->hygiene = nl_clampf(p->hygiene + 10.0f, 0.0f, 100.0f);
        nl_log_push(&g->log, NL_MSG_GOOD, "Filled a jerrycan. Thirst eased.");
        break;

    case NL_INT_CLINIC:
        if (p->illness == NL_ILL_NONE && p->injury < 0.05f) {
            nl_log_push(&g->log, NL_MSG_INFO, "The nurse says you are fine.");
            break;
        }
        if (p->cash < g->prices.clinic_visit) {
            nl_log_push(&g->log, NL_MSG_BAD,
                        "Treatment costs KSh %d. You cannot pay.",
                        g->prices.clinic_visit);
            break;
        }
        nl_player_spend(g, g->prices.clinic_visit, "clinic");
        nl_log_push(&g->log, NL_MSG_GOOD, "Treated for %s.",
                    nl_illness_name(p->illness));
        p->illness = NL_ILL_NONE;
        p->illness_severity = 0.0f;
        p->injury = 0.0f;
        p->health = nl_clampf(p->health + 25.0f, 0.0f, 100.0f);
        break;

    case NL_INT_SLEEP:
        g->screen = NL_SCREEN_SLEEPING;
        break;

    default:
        break;
    }
}

/* ---------------------------------------------------------------------- */

void nl_game_input(NLGame *g, float real_dt)
{
    (void)real_dt;

    if (IsKeyPressed(KEY_F1)) g->show_help  = !g->show_help;
    if (IsKeyPressed(KEY_F3)) g->show_debug = !g->show_debug;

    switch (g->screen) {
    case NL_SCREEN_TITLE:
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))
            g->screen = NL_SCREEN_PLAYING;
        return;

    case NL_SCREEN_PAUSED:
        if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE))
            g->screen = NL_SCREEN_PLAYING;
        return;

    case NL_SCREEN_DAY_SUMMARY:
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))
            g->screen = NL_SCREEN_PLAYING;
        return;

    case NL_SCREEN_GAMEOVER:
        if (IsKeyPressed(KEY_ENTER)) {
            uint32_t seed = g->rng_state ^ 0x9E3779B9u;
            nl_game_init(g, seed);
            g->screen = NL_SCREEN_PLAYING;
        }
        return;

    case NL_SCREEN_SLEEPING:
        return;

    case NL_SCREEN_PLAYING:
        break;
    }

    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) {
        g->screen = NL_SCREEN_PAUSED;
        return;
    }

    NLPlayer *p = &g->player;

    /* Movement. */
    NLVec2 dir = { 0.0f, 0.0f };
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    dir.y -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  dir.y += 1.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  dir.x -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) dir.x += 1.0f;

    /* Sprinting needs energy; you cannot sprint on an empty tank. */
    p->is_sprinting = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                      && p->energy > 5.0f;

    if (dir.x != 0.0f || dir.y != 0.0f) {
        dir = nl_vec_norm(dir);
        p->facing = atan2f(dir.y, dir.x);
    }
    p->vel = dir;

    if (IsKeyPressed(KEY_E)) nl_do_interaction(g);

    /* Cancel the current job. */
    if (IsKeyPressed(KEY_Q) && g->active_job >= 0) nl_job_cancel(g, false);

    /* Zoom. */
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        g->camera_zoom = nl_clampf(g->camera_zoom + wheel * 1.5f, 5.0f, 34.0f);
    }

    /* Time scale control, so a player can wait out the rain. */
    if (IsKeyPressed(KEY_PERIOD))
        g->time_scale = nl_clampf(g->time_scale * 2.0f, 15.0f, 960.0f);
    if (IsKeyPressed(KEY_COMMA))
        g->time_scale = nl_clampf(g->time_scale * 0.5f, 15.0f, 960.0f);
}

/* ---------------------------------------------------------------------- */

static void nl_move_player(NLGame *g, float real_dt)
{
    NLPlayer *p = &g->player;

    float base = p->is_sprinting ? NL_SPRINT_SPEED : NL_WALK_SPEED;

    /* Surface and weather change how fast you can actually move. Wet murram
     * in Kibera is genuinely difficult ground. */
    NLSurface surf = nl_world_surface_at(g, p->pos);
    base *= nl_weather_move_multiplier(&g->weather, surf);

    /* Carrying scrap is heavy; illness, injury and exhaustion all slow you. */
    base *= 1.0f - nl_clampf((float)p->stock_scrap_kg / 120.0f, 0.0f, 0.35f);
    base *= 1.0f - nl_clampf(p->injury * 0.4f, 0.0f, 0.4f);
    base *= 1.0f - nl_clampf(p->illness_severity * 0.25f, 0.0f, 0.25f);
    if (p->energy < 20.0f) base *= 0.65f;

    NLVec2 step;
    step.x = p->vel.x * base * real_dt;
    step.y = p->vel.y * base * real_dt;

    /* Axis-separated movement so we slide along walls instead of sticking. */
    NLVec2 tryx = { p->pos.x + step.x, p->pos.y };
    if (!nl_world_blocked(g, tryx, NL_PLAYER_RADIUS)) p->pos.x = tryx.x;

    NLVec2 tryy = { p->pos.x, p->pos.y + step.y };
    if (!nl_world_blocked(g, tryy, NL_PLAYER_RADIUS)) p->pos.y = tryy.y;

    p->pos.x = nl_clampf(p->pos.x, 1.0f, NL_WORLD_W - 1.0f);
    p->pos.y = nl_clampf(p->pos.y, 1.0f, NL_WORLD_H - 1.0f);

    p->under_roof = nl_world_under_roof(g, p->pos);
}

/* ---------------------------------------------------------------------- */

static void nl_check_gameover(NLGame *g)
{
    if (g->player.health <= 0.0f) {
        g->player.health = 0.0f;
        g->screen = NL_SCREEN_GAMEOVER;
    }
}

/* ---------------------------------------------------------------------- */

void nl_game_update(NLGame *g, float real_dt)
{
    g->real_seconds += (double)real_dt;
    g->frame_counter++;

    /* Rain particles keep animating on menus so the title screen is alive. */
    nl_render_rain_update(g, real_dt);
    nl_log_update(&g->log, real_dt);

    if (g->screen == NL_SCREEN_TITLE ||
        g->screen == NL_SCREEN_PAUSED ||
        g->screen == NL_SCREEN_DAY_SUMMARY ||
        g->screen == NL_SCREEN_GAMEOVER)
        return;

    /* Sleeping fast-forwards to 06:00 the next morning. */
    if (g->screen == NL_SCREEN_SLEEPING) {
        float hour = g->clock.hour;
        float until_morning = (hour < 6.0f) ? (6.0f - hour)
                                            : (24.0f - hour + 6.0f);
        nl_player_sleep(g, until_morning, g->player.under_roof ||
                                          g->player.has_shelter_tonight);

        int prev_day = g->clock.day_index;
        nl_clock_advance_hours(&g->clock, until_morning);
        nl_weather_update(&g->weather, &g->clock,
                          until_morning * 3600.0f, &g->rng_state);

        if (g->clock.day_index != prev_day) nl_player_new_day(g);

        nl_jobs_generate(g);
        g->screen = NL_SCREEN_DAY_SUMMARY;
        nl_check_gameover(g);
        return;
    }

    /* --- Normal play tick --- */

    float sim_seconds = real_dt * g->time_scale;

    int prev_day = g->clock.day_index;
    nl_clock_update(&g->clock, sim_seconds);
    if (g->clock.day_index != prev_day) {
        nl_player_new_day(g);
        nl_jobs_generate(g);
    }

    nl_weather_update(&g->weather, &g->clock, sim_seconds, &g->rng_state);
    nl_world_update(g, real_dt, sim_seconds);

    nl_move_player(g, real_dt);

    nl_npc_update(g, real_dt, sim_seconds);
    nl_vehicle_update(g, real_dt, sim_seconds);

    nl_player_needs_update(g, sim_seconds);
    nl_econ_update(g, real_dt, sim_seconds);

    nl_update_interaction(g);

    /* Camera eases toward the player. */
    float k = 1.0f - expf(-8.0f * real_dt);
    g->camera.x += (g->player.pos.x - g->camera.x) * k;
    g->camera.y += (g->player.pos.y - g->camera.y) * k;

    nl_check_gameover(g);
}
