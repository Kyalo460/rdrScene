#include "nairobi_streets.h"
#include <math.h>
#include <stdlib.h>

#define PLAYER_SPEED 200.0f
#define PLAYER_SPRINT_SPEED 350.0f
#define STAMINA_DRAIN_RATE 30.0f
#define STAMINA_REGEN_RATE 15.0f
#define MAX_STAMINA 100.0f
#define DODGE_DISTANCE 150.0f
#define DODGE_COOLDOWN 0.5f

static const float weapon_cooldowns[WEAPON_COUNT] = {
    0.5f,  // FISTS
    0.4f,  // KNIFE
    0.3f,  // PISTOL
    0.6f,  // SHOTGUN
    0.08f, // AK47
    1.0f,  // SNIPER
    2.0f,  // RPG
    1.5f,  // GRENADE
    2.0f   // MOLOTOV
};

static const int weapon_damages[WEAPON_COUNT] = {
    15,  // FISTS
    25,  // KNIFE
    12,  // PISTOL
    10,  // SHOTGUN (per pellet)
    18,  // AK47
    50,  // SNIPER
    100, // RPG
    40,  // GRENADE
    30   // MOLOTOV (initial)
};

static const bool weapon_auto[WEAPON_COUNT] = {
    false, // FISTS
    false, // KNIFE
    false, // PISTOL
    false, // SHOTGUN
    true,  // AK47
    false, // SNIPER
    false, // RPG
    false, // GRENADE
    false  // MOLOTOV
};

void PlayerInit(Game* game) {
    Vector2 spawn = WorldGetSpawnPoint(FACTION_PLAYER);
    Entity* player = EntityCreate(game, ENTITY_PLAYER, spawn.x, spawn.y);
    if (player) {
        player->width = 32;
        player->height = 48;
        player->health = 100;
        player->max_health = 100;
        player->stamina = MAX_STAMINA;
        player->max_stamina = MAX_STAMINA;
        player->armor = 0;
        player->max_armor = 100;
        player->speed = PLAYER_SPEED;
        player->angle = 0.0f;
        player->active = true;
        player->type = ENTITY_PLAYER;
        player->weapon = WEAPON_PISTOL;
        player->weapon_cooldown = 0.0f;
        player->dodge_cooldown = 0.0f;
        player->is_sprinting = false;
        player->is_dodging = false;
        player->dodge_timer = 0.0f;
        player->dodge_dir_x = 0.0f;
        player->dodge_dir_y = 0.0f;
        game->player = player;
        game->camera_x = player->x - game->screen_width / 2;
        game->camera_y = player->y - game->screen_height / 2;
    }
}

void PlayerUpdate(Game* game, float dt) {
    Entity* player = game->player;
    if (!player || !player->active) return;

    if (player->weapon_cooldown > 0.0f) {
        player->weapon_cooldown -= dt;
    }
    if (player->dodge_cooldown > 0.0f) {
        player->dodge_cooldown -= dt;
    }

    if (player->is_dodging) {
        player->dodge_timer -= dt;
        player->x += player->dodge_dir_x * DODGE_DISTANCE * dt * 5.0f;
        player->y += player->dodge_dir_y * DODGE_DISTANCE * dt * 5.0f;

        if (player->dodge_timer <= 0.0f) {
            player->is_dodging = false;
        }
    }

    if (player->is_sprinting && player->stamina > 0.0f && !player->is_dodging) {
        player->stamina -= STAMINA_DRAIN_RATE * dt;
        if (player->stamina < 0.0f) player->stamina = 0.0f;
    } else if (!player->is_sprinting && player->stamina < MAX_STAMINA) {
        player->stamina += STAMINA_REGEN_RATE * dt;
        if (player->stamina > MAX_STAMINA) player->stamina = MAX_STAMINA;
    }

    float current_speed = (player->is_sprinting && player->stamina > 0.0f && !player->is_dodging)
        ? PLAYER_SPRINT_SPEED : PLAYER_SPEED;

    if (!player->is_dodging) {
        float move_x = 0.0f, move_y = 0.0f;
        if (game->input.keys[KEY_W]) move_y -= 1.0f;
        if (game->input.keys[KEY_S]) move_y += 1.0f;
        if (game->input.keys[KEY_A]) move_x -= 1.0f;
        if (game->input.keys[KEY_D]) move_x += 1.0f;

        float len = sqrtf(move_x * move_x + move_y * move_y);
        if (len > 0.0f) {
            move_x /= len;
            move_y /= len;

            float new_x = player->x + move_x * current_speed * dt;
            float new_y = player->y + move_y * current_speed * dt;

            Vector2 check_pos = {new_x, new_y};
            if (WorldIsWalkable(check_pos)) {
                player->x = new_x;
                player->y = new_y;
            } else {
                check_pos.x = player->x + move_x * current_speed * dt;
                check_pos.y = player->y;
                if (WorldIsWalkable(check_pos)) {
                    player->x = check_pos.x;
                } else {
                    check_pos.x = player->x;
                    check_pos.y = player->y + move_y * current_speed * dt;
                    if (WorldIsWalkable(check_pos)) {
                        player->y = check_pos.y;
                    }
                }
            }
        }
    }

    int mx = game->input.mouse_x + (int)game->camera_x;
    int my = game->input.mouse_y + (int)game->camera_y;
    float dx = mx - player->x;
    float dy = my - player->y;
    player->angle = atan2f(dy, dx);

    bool mouse_down = game->input.mouse_buttons[MOUSE_BUTTON_LEFT];
    if ((weapon_auto[player->weapon] && mouse_down) ||
        (!weapon_auto[player->weapon] && game->input.mouse_buttons_pressed[MOUSE_BUTTON_LEFT])) {
        if (player->weapon_cooldown <= 0.0f && game->ammo > 0) {
            PlayerShoot(game);
            player->weapon_cooldown = weapon_cooldowns[player->weapon];
            if (player->weapon != WEAPON_FISTS && player->weapon != WEAPON_KNIFE) {
                game->ammo--;
            }
        }
    }

    if (game->input.keys[KEY_LEFT_SHIFT] && player->stamina > 0.0f && !player->is_dodging) {
        player->is_sprinting = true;
    } else {
        player->is_sprinting = false;
    }

    if (game->input.keys_pressed[KEY_SPACE] && player->dodge_cooldown <= 0.0f && !player->is_dodging) {
        float dodge_x = 0.0f, dodge_y = 0.0f;
        if (game->input.keys[KEY_W]) dodge_y -= 1.0f;
        if (game->input.keys[KEY_S]) dodge_y += 1.0f;
        if (game->input.keys[KEY_A]) dodge_x -= 1.0f;
        if (game->input.keys[KEY_D]) dodge_x += 1.0f;

        float len = sqrtf(dodge_x * dodge_x + dodge_y * dodge_y);
        if (len == 0.0f) {
            dodge_x = cosf(player->angle);
            dodge_y = sinf(player->angle);
            len = 1.0f;
        }
        dodge_x /= len;
        dodge_y /= len;

        player->is_dodging = true;
        player->dodge_timer = 0.2f;
        player->dodge_dir_x = dodge_x;
        player->dodge_dir_y = dodge_y;
        player->dodge_cooldown = DODGE_COOLDOWN;
    }

    for (int i = 0; i < 9; i++) {
        if (game->input.keys_pressed[KEY_ONE + i]) {
            player->weapon = i;
            game->current_weapon = i;
        }
    }

    if (game->input.keys_pressed[KEY_R]) {
        game->ammo = game->max_ammo;
    }

    if (player->x < 0) player->x = 0;
    if (player->y < 0) player->y = 0;
    if (player->x > game->world_width - player->width) player->x = game->world_width - player->width;
    if (player->y > game->world_height - player->height) player->y = game->world_height - player->height;
}

void PlayerShoot(Game* game) {
    Entity* player = game->player;
    if (!player) return;

    float px = player->x + player->width / 2.0f;
    float py = player->y + player->height / 2.0f;
    float dir_x = cosf(player->angle);
    float dir_y = sinf(player->angle);

    AudioPlaySound(1, 1.0f);

    switch (player->weapon) {
        case WEAPON_FISTS:
        case WEAPON_KNIFE: {
            Entity* melee = EntityCreate(game, ENTITY_MELEE, px, py, player->angle);
            if (melee) {
                melee->damage = weapon_damages[player->weapon];
                melee->owner = player;
                melee->lifetime = 0.2f;
                melee->vel_x = dir_x * 50.0f;
                melee->vel_y = dir_y * 50.0f;
                melee->width = 40;
                melee->height = 40;
            }
            break;
        }
        case WEAPON_PISTOL: {
            EntityCreateBullet(game, px, py, dir_x, dir_y, weapon_damages[WEAPON_PISTOL], 600.0f, player);
            break;
        }
        case WEAPON_SHOTGUN: {
            for (int i = 0; i < 8; i++) {
                float spread = ((float)rand() / RAND_MAX - 0.5f) * 0.4f;
                float bx = cosf(player->angle + spread);
                float by = sinf(player->angle + spread);
                EntityCreateBullet(game, px, py, bx, by, weapon_damages[WEAPON_SHOTGUN], 500.0f, player);
            }
            break;
        }
        case WEAPON_AK47: {
            float spread = ((float)rand() / RAND_MAX - 0.5f) * 0.08f;
            float bx = cosf(player->angle + spread);
            float by = sinf(player->angle + spread);
            EntityCreateBullet(game, px, py, bx, by, weapon_damages[WEAPON_AK47], 700.0f, player);
            break;
        }
        case WEAPON_SNIPER: {
            EntityCreateBullet(game, px, py, dir_x, dir_y, weapon_damages[WEAPON_SNIPER], 1000.0f, player);
            break;
        }
        case WEAPON_RPG: {
            Entity* rocket = EntityCreate(game, ENTITY_BULLET, px, py, player->angle);
            if (rocket) {
                rocket->vel_x = dir_x * 300.0f;
                rocket->vel_y = dir_y * 300.0f;
                rocket->damage = weapon_damages[WEAPON_RPG];
                rocket->owner = player;
                rocket->lifetime = 5.0f;
                rocket->width = 16;
                rocket->height = 16;
            }
            break;
        }
        case WEAPON_GRENADE: {
            Entity* grenade = EntityCreate(game, ENTITY_BULLET, px, py, player->angle);
            if (grenade) {
                grenade->vel_x = dir_x * 250.0f;
                grenade->vel_y = dir_y * 250.0f - 100.0f;
                grenade->damage = weapon_damages[WEAPON_GRENADE];
                grenade->owner = player;
                grenade->lifetime = 3.0f;
                grenade->width = 12;
                grenade->height = 12;
            }
            break;
        }
        case WEAPON_MOLOTOV: {
            Entity* molotov = EntityCreate(game, ENTITY_MOLOTOV, px, py, player->angle);
            if (molotov) {
                molotov->vel_x = dir_x * 300.0f;
                molotov->vel_y = dir_y * 300.0f - 100.0f;
                molotov->damage = weapon_damages[WEAPON_MOLOTOV];
                molotov->owner = player;
                molotov->lifetime = 3.0f;
                molotov->width = 16;
                molotov->height = 16;
            }
            break;
        }
    }
}

Entity* EntityCreateBullet(Game* game, float x, float y, float dir_x, float dir_y, int damage, float speed, Entity* owner) {
    Entity* bullet = EntityCreate(game, ENTITY_BULLET, x, y);
    if (bullet) {
        bullet->vel_x = dir_x * speed;
        bullet->vel_y = dir_y * speed;
        bullet->damage = damage;
        bullet->owner = owner;
        bullet->lifetime = 2.0f;
        bullet->width = 8;
        bullet->height = 8;
        bullet->angle = atan2f(dir_y, dir_x);
    }
    return bullet;
}