#include "nairobi_streets.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

Game g_game = {0};

static void InitTextures(Game* game) {
    Image img = GenImageColor(32, 32, RED);
    game->textures[0] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(32, 32, BLUE);
    game->textures[1] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(32, 32, GREEN);
    game->textures[2] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(32, 32, YELLOW);
    game->textures[3] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(32, 32, ORANGE);
    game->textures[4] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(16, 16, WHITE);
    game->textures[5] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(32, 48, BROWN);
    game->textures[6] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(32, 48, DARKGREEN);
    game->textures[7] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(32, 48, DARKBLUE);
    game->textures[8] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(32, 48, MAROON);
    game->textures[9] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(16, 16, GOLD);
    game->textures[10] = LoadTextureFromImage(img);
    UnloadImage(img);

    img = GenImageColor(16, 16, LIGHTGRAY);
    game->textures[11] = LoadTextureFromImage(img);
    UnloadImage(img);

    game->texture_count = 12;
}

void GameInit(Game* game) {
    srand((unsigned int)time(NULL));

    game->screen_width = SCREEN_WIDTH;
    game->screen_height = SCREEN_HEIGHT;
    game->world_width = WORLD_WIDTH;
    game->world_height = WORLD_HEIGHT;
    game->camera_x = 0;
    game->camera_y = 0;
    game->entity_count = 0;
    game->player = NULL;
    game->cash = 500;
    game->respect = 0;
    game->heat_level = 0;
    game->current_weapon = WEAPON_PISTOL;
    game->ammo = 30;
    game->max_ammo = 30;
    game->kills = 0;
    game->time_survived = 0;
    game->game_over = false;
    game->paused = false;
    game->current_mission = -1;
    game->show_weapon_wheel = false;
    game->show_mission_log = false;
    game->game_time = 0;
    game->wave = 1;
    game->enemies_killed_this_wave = 0;

    for (int i = 0; i < 512; i++) {
        game->input.keys[i] = false;
        game->input.keys_pressed[i] = false;
        game->input.keys_released[i] = false;
    }
    for (int i = 0; i < 3; i++) {
        game->input.mouse_buttons[i] = false;
        game->input.mouse_buttons_pressed[i] = false;
        game->input.mouse_buttons_released[i] = false;
    }
    game->input.mouse_x = 0;
    game->input.mouse_y = 0;
    game->input.mouse_wheel = 0;

    InitTextures(game);

    WorldInit((uint32_t)time(NULL));
    MissionInit();
    AudioInit();
    UIInit();
    DialogueInit();

    GameSpawnPlayer(game);

    AudioPlayMusic(0);
}

void GameSpawnPlayer(Game* game) {
    Vector2 spawn = WorldGetSpawnPoint(FACTION_PLAYER);
    Entity* player = EntityCreate(game, ENTITY_PLAYER, spawn.x, spawn.y, 0.0f);
    if (player) {
        player->width = 32;
        player->height = 48;
        player->health = 100;
        player->max_health = 100;
        player->stamina = 100.0f;
        player->max_stamina = 100.0f;
        player->armor = 0;
        player->max_armor = 100;
        player->speed = 200.0f;
        player->weapon = WEAPON_PISTOL;
        player->weapon_cooldown = 0.0f;
        player->dodge_cooldown = 0.0f;
        player->is_sprinting = false;
        player->is_dodging = false;
        player->active = true;
        player->type = ENTITY_PLAYER;
        player->faction = FACTION_PLAYER;
        game->player = player;
        game->camera_x = player->x - game->screen_width / 2;
        game->camera_y = player->y - game->screen_height / 2;
    }
}

void GameHandleInput(Game* game) {
    for (int i = 0; i < 512; i++) {
        game->input.keys_pressed[i] = false;
        game->input.keys_released[i] = false;
    }
    for (int i = 0; i < 3; i++) {
        game->input.mouse_buttons_pressed[i] = false;
        game->input.mouse_buttons_released[i] = false;
    }
    game->input.mouse_wheel = 0;

    game->input.mouse_x = GetMouseX();
    game->input.mouse_y = GetMouseY();
    game->input.mouse_wheel = GetMouseWheelMove();

    for (int i = 0; i < 512; i++) {
        bool down = IsKeyDown(i);
        if (down && !game->input.keys[i]) {
            game->input.keys_pressed[i] = true;
        }
        if (!down && game->input.keys[i]) {
            game->input.keys_released[i] = true;
        }
        game->input.keys[i] = down;
    }

    for (int i = 0; i < 3; i++) {
        bool down = IsMouseButtonDown(i);
        if (down && !game->input.mouse_buttons[i]) {
            game->input.mouse_buttons_pressed[i] = true;
        }
        if (!down && game->input.mouse_buttons[i]) {
            game->input.mouse_buttons_released[i] = true;
        }
        game->input.mouse_buttons[i] = down;
    }

    if (game->input.keys_pressed[KEY_ESCAPE]) {
        game->paused = !game->paused;
    }

    if (game->input.keys_pressed[KEY_TAB]) {
        game->show_mission_log = !game->show_mission_log;
    }

    if (game->input.keys_pressed[KEY_Q]) {
        game->show_weapon_wheel = true;
    }
    if (game->input.keys_released[KEY_Q]) {
        game->show_weapon_wheel = false;
    }
}

void GameUpdate(Game* game) {
    game->dt = GetFrameTime();
    game->game_time += game->dt;
    game->time_survived = (int)game->game_time;

    GameHandleInput(game);

    if (game->paused || game->game_over) return;

    AudioUpdate();

    if (game->player && game->player->active) {
        CameraFollowPlayer(game);
    }

    for (int i = 0; i < game->entity_count; i++) {
        Entity* e = &game->entities[i];
        if (e->active) {
            EntityUpdate(game, e, game->dt);
        }
    }

    for (int i = game->entity_count - 1; i >= 0; i--) {
        Entity* e = &game->entities[i];
        if (!e->active) {
            game->entities[i] = game->entities[game->entity_count - 1];
            game->entity_count--;
        }
    }

    WorldUpdate(game->dt);
    MissionUpdate(game->dt);
    EnemyUpdate(game, game->dt);

    if (game->player) {
        PlayerHUD hud = {
            .health = game->player->health,
            .maxHealth = game->player->max_health,
            .stamina = game->player->stamina,
            .maxStamina = game->player->max_stamina,
            .armor = game->player->armor,
            .maxArmor = game->player->max_armor,
            .cash = game->cash,
            .respect = game->respect,
            .heatLevel = game->heat_level,
            .currentWeapon = game->current_weapon,
            .ammo = game->ammo,
            .maxAmmo = game->max_ammo,
            .position = {game->player->x, game->player->y}
        };
    }
}

void CameraFollowPlayer(Game* game) {
    if (!game->player) return;

    game->camera_x = game->player->x - game->screen_width / 2;
    game->camera_y = game->player->y - game->screen_height / 2;

    if (game->camera_x < 0) game->camera_x = 0;
    if (game->camera_y < 0) game->camera_y = 0;
    if (game->camera_x > game->world_width - game->screen_width) {
        game->camera_x = game->world_width - game->screen_width;
    }
    if (game->camera_y > game->world_height - game->screen_height) {
        game->camera_y = game->world_height - game->screen_height;
    }
}

void GameDraw(Game* game) {
    BeginDrawing();
    ClearBackground((Color){13, 17, 23, 255});

    BeginMode2D((Camera2D){
        .offset = {0, 0},
        .target = {game->camera_x + game->screen_width / 2, game->camera_y + game->screen_height / 2},
        .rotation = 0,
        .zoom = 1.0f
    });

    WorldDraw();

    for (int i = 0; i < game->entity_count; i++) {
        Entity* e = &game->entities[i];
        if (e->active) {
            EntityDraw(game, e);
        }
    }

    EndMode2D();

    if (game->player) {
        PlayerHUD hud = {
            .health = game->player->health,
            .maxHealth = game->player->max_health,
            .stamina = game->player->stamina,
            .maxStamina = game->player->max_stamina,
            .armor = game->player->armor,
            .maxArmor = game->player->max_armor,
            .cash = game->cash,
            .respect = game->respect,
            .heatLevel = game->heat_level,
            .currentWeapon = game->current_weapon,
            .ammo = game->ammo,
            .maxAmmo = game->max_ammo,
            .position = {game->player->x, game->player->y}
        };
        UIDrawHUD(&hud);
    }

    Mission* active = MissionGetActive();
    if (active) {
        MissionData md = {0};
        strncpy(md.title, active->title, sizeof(md.title) - 1);
        strncpy(md.description, active->description, sizeof(md.description) - 1);
        md.timer = active->timer;
        md.rewardCash = active->cash_reward;
        md.rewardRespect = active->rep_reward;
        md.active = true;
        UIDrawMission(&md);
    }

    if (game->show_weapon_wheel) {
        WeaponWheel wheel = {
            .weaponCount = 7,
            .weaponTypes = {WEAPON_FISTS, WEAPON_KNIFE, WEAPON_PISTOL, WEAPON_SHOTGUN, WEAPON_AK47, WEAPON_SNIPER, WEAPON_RPG},
            .selectedWeapon = game->current_weapon,
            .visible = true,
            .center = {game->screen_width / 2.0f, game->screen_height / 2.0f},
            .radius = 180
        };
        UIUpdateWeaponWheel(&wheel, (Vector2){game->input.mouse_x, game->input.mouse_y});
        if (wheel.selectedWeapon >= 0 && wheel.selectedWeapon < 7) {
            game->current_weapon = wheel.weaponTypes[wheel.selectedWeapon];
        }
        UIDrawWeaponWheel(&wheel);
    }

    if (game->game_over) {
        WastedScreen wasted = {0};
        UISetWasted(&wasted, true, game->kills, game->time_survived, game->cash);
        UIDrawWasted(&wasted);
    }

    if (game->paused) {
        DrawRectangle(0, 0, game->screen_width, game->screen_height, (Color){0, 0, 0, 180});
        const char* pauseText = "PAUSED - PRESS ESC TO RESUME";
        int textW = MeasureText(pauseText, 40);
        DrawText(pauseText, (game->screen_width - textW) / 2, game->screen_height / 2 - 20, 40, YELLOW);
    }

    EndDrawing();
}

void GameClose(Game* game) {
    for (int i = 0; i < game->texture_count; i++) {
        UnloadTexture(game->textures[i]);
    }
    UIClose();
    AudioClose();
    DialogueClose();
    CloseAudioDevice();
    CloseWindow();
}

Entity* EntityCreate(Game* game, EntityType type, float x, float y, float angle) {
    if (game->entity_count >= MAX_ENTITIES) return NULL;

    Entity* e = &game->entities[game->entity_count++];
    memset(e, 0, sizeof(Entity));
    e->id = game->entity_count;
    e->type = type;
    e->x = x;
    e->y = y;
    e->angle = angle;
    e->active = true;
    return e;
}

void EntityDestroy(Game* game, Entity* e) {
    if (e) e->active = false;
}

void EntityUpdate(Game* game, Entity* e, float dt) {
    (void)game;
    (void)e;
    (void)dt;
}

void EntityDraw(Game* game, Entity* e) {
    if (!e || !e->active) return;

    int screen_x = (int)(e->x - game->camera_x);
    int screen_y = (int)(e->y - game->camera_y);

    if (screen_x < -50 || screen_x > game->screen_width + 50 ||
        screen_y < -50 || screen_y > game->screen_height + 50) {
        return;
    }

    Color color = WHITE;
    int tex_idx = 0;

    switch (e->type) {
        case ENTITY_PLAYER: color = GREEN; tex_idx = 0; break;
        case ENTITY_CIVILIAN: color = BLUE; tex_idx = 1; break;
        case ENTITY_GANG_MUNGIKI: color = RED; tex_idx = 6; break;
        case ENTITY_GANG_TALIBAN: color = ORANGE; tex_idx = 7; break;
        case ENTITY_GANG_JERUSALEM: color = PURPLE; tex_idx = 8; break;
        case ENTITY_POLICE: color = BLUE; tex_idx = 9; break;
        case ENTITY_BOSS: color = MAROON; tex_idx = 6; break;
        case ENTITY_BULLET: color = YELLOW; tex_idx = 5; break;
        case ENTITY_PICKUP: color = GOLD; tex_idx = 10; break;
        default: color = WHITE; tex_idx = 0; break;
    }

    if (tex_idx < game->texture_count && game->textures[tex_idx].id > 0) {
        Rectangle src = {0, 0, (float)game->textures[tex_idx].width, (float)game->textures[tex_idx].height};
        Rectangle dst = {screen_x, screen_y, e->width, e->height};
        Vector2 origin = {e->width / 2, e->height / 2};
        DrawTexturePro(game->textures[tex_idx], src, dst, origin, e->angle * RAD2DEG, WHITE);
    } else {
        DrawRectanglePro((Rectangle){screen_x, screen_y, e->width, e->height}, (Vector2){e->width / 2, e->height / 2}, e->angle * RAD2DEG, color);
    }

    if (e->health < e->max_health && e->max_health > 0) {
        float bar_w = 30;
        float bar_h = 4;
        float bar_x = screen_x - bar_w / 2;
        float bar_y = screen_y - e->height / 2 - 10;
        DrawRectangle(bar_x, bar_y, bar_w, bar_h, RED);
        DrawRectangle(bar_x, bar_y, bar_w * e->health / e->max_health, bar_h, GREEN);
    }
}

bool EntityCheckCollision(Entity* a, Entity* b) {
    if (!a->active || !b->active) return false;
    return (a->x < b->x + b->width &&
            a->x + a->width > b->x &&
            a->y < b->y + b->height &&
            a->y + a->height > b->y);
}

void EntityTakeDamage(Game* game, Entity* target, int damage, Entity* attacker) {
    (void)game;
    if (!target || !target->active) return;
    target->health -= damage;
    if (target->health <= 0) {
        target->active = false;
        if (target->type != ENTITY_PLAYER && attacker && attacker->type == ENTITY_PLAYER) {
            game->kills++;
            game->cash += 10;
            game->respect += 5;
        }
        if (target->type == ENTITY_PLAYER) {
            game->game_over = true;
        }
    }
}

Entity* EntityFindNearest(Game* game, Entity* from, EntityType target_type, float max_range) {
    Entity* nearest = NULL;
    float nearest_dist = max_range;

    for (int i = 0; i < game->entity_count; i++) {
        Entity* e = &game->entities[i];
        if (e->active && e != from && e->type == target_type) {
            float dist = EntityDistance(from, e);
            if (dist < nearest_dist) {
                nearest_dist = dist;
                nearest = e;
            }
        }
    }
    return nearest;
}

void WorldWrapEntity(Entity* e) {
    if (e->x < 0) e->x = WORLD_WIDTH;
    if (e->x > WORLD_WIDTH) e->x = 0;
    if (e->y < 0) e->y = WORLD_HEIGHT;
    if (e->y > WORLD_HEIGHT) e->y = 0;
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Nairobi Streets - Wild West Survival");
    SetTargetFPS(60);

    GameInit(&g_game);

    while (!WindowShouldClose()) {
        GameUpdate(&g_game);
        GameDraw(&g_game);
    }

    GameClose(&g_game);
    return 0;
}