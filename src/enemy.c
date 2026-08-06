#include "nairobi_streets.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

static void CivilianAI(Entity* e, Entity* player, float dist, float dt);
static void GangAI(Game* game, Entity* e, Entity* player, float dist, float dt);
static void PoliceAI(Game* game, Entity* e, Entity* player, float dist, float dt);

void EnemyInit(Game* game) {
    game->entity_count = 0;
    game->worldHeat = 0;
    srand((unsigned int)time(NULL));
}

void EnemyUpdate(Game* game, float dt) {
    Entity* player = game->player;
    if (!player) return;

    for (int i = 0; i < game->entity_count; i++) {
        Entity* e = &game->entities[i];
        if (e->active && e->type != ENTITY_PLAYER && e->type != ENTITY_BULLET) {
            EnemyAI(game, e, dt);
        }
    }
}

void EnemyAI(Game* game, Entity* e, float dt) {
    Entity* player = game->player;
    if (!player) return;

    float dist = EntityDistance(e, player);

    switch (e->faction) {
        case FACTION_CIVILIAN:
            CivilianAI(e, player, dist, dt);
            break;
        case FACTION_GANG_MUNGIKI:
        case FACTION_GANG_TALIBAN:
        case FACTION_GANG_JERUSALEM:
            GangAI(game, e, player, dist, dt);
            break;
        case FACTION_POLICE:
            PoliceAI(game, e, player, dist, dt);
            break;
    }
}

void CivilianAI(Entity* e, Entity* player, float dist, float dt) {
    if (dist < 100.0f && e->state_timer > 2.0f) {
        float angle = atan2f(e->y - player->y, e->x - player->x);
        EntityMoveToward(e, e->x + cosf(angle) * 200, e->y + sinf(angle) * 200, 100.0f, dt);
        e->state_timer = 0.0f;
    } else if (e->state_timer > 5.0f) {
        if (e->patrol_count == 0) {
            for (int i = 0; i < 4; i++) {
                e->patrol_points[i][0] = e->x + (rand() % 200 - 100);
                e->patrol_points[i][1] = e->y + (rand() % 200 - 100);
            }
            e->patrol_count = 4;
            e->patrol_index = 0;
        }
        if (e->patrol_count > 0) {
            float tx = e->patrol_points[e->patrol_index][0];
            float ty = e->patrol_points[e->patrol_index][1];
            float d = sqrtf((tx - e->x) * (tx - e->x) + (ty - e->y) * (ty - e->y));
            if (d < 20.0f) {
                e->patrol_index = (e->patrol_index + 1) % e->patrol_count;
            } else {
                EntityMoveToward(e, tx, ty, 50.0f, dt);
            }
        }
        e->state_timer = 0.0f;
    }
}

void GangAI(Game* game, Entity* e, Entity* player, float dist, float dt) {
    if (dist < 400.0f) {
        int target_id = EntityFindNearestTarget(game, e, FACTION_PLAYER, 400.0f);
        if (target_id >= 0) {
            Entity* target = &game->entities[target_id];
            e->current_target_id = target_id;

            if (dist < 80.0f && e->weapon != WEAPON_NONE) {
                if (e->weapon == WEAPON_KNIFE || e->weapon == WEAPON_FISTS) {
                    EntityAttack(e, target);
                } else if (e->ai_timer > 0.5f) {
                    float angle = atan2f(target->y - e->y, target->x - e->x);
                    Entity* bullet = EntityCreate(game, ENTITY_BULLET, e->x, e->y, angle);
                    if (bullet) {
                        bullet->vel_x = cosf(angle) * 400.0f;
                        bullet->vel_y = sinf(angle) * 400.0f;
                        bullet->damage = 10;
                        bullet->owner = e;
                        bullet->lifetime = 2.0f;
                        bullet->width = 8;
                        bullet->height = 8;
                    }
                    e->ai_timer = 0.0f;
                }
            } else {
                EntityMoveToward(e, target->x, target->y, 120.0f, dt);
            }
        }
    } else {
        if (e->patrol_count == 0) {
            for (int i = 0; i < 4; i++) {
                e->patrol_points[i][0] = e->x + (rand() % 400 - 200);
                e->patrol_points[i][1] = e->y + (rand() % 400 - 200);
            }
            e->patrol_count = 4;
            e->patrol_index = 0;
        }
        if (e->patrol_count > 0) {
            float tx = e->patrol_points[e->patrol_index][0];
            float ty = e->patrol_points[e->patrol_index][1];
            float d = sqrtf((tx - e->x) * (tx - e->x) + (ty - e->y) * (ty - e->y));
            if (d < 30.0f) {
                e->patrol_index = (e->patrol_index + 1) % e->patrol_count;
            } else {
                EntityMoveToward(e, tx, ty, 60.0f, dt);
            }
        }
    }
}

void PoliceAI(Game* game, Entity* e, Entity* player, float dist, float dt) {
    int heat = game->worldHeat;

    if (heat > 50 || dist < 300.0f) {
        int target_id = EntityFindNearestTarget(game, e, FACTION_GANG_MUNGIKI, 500.0f);
        if (target_id < 0) target_id = EntityFindNearestTarget(game, e, FACTION_GANG_TALIBAN, 500.0f);
        if (target_id < 0) target_id = EntityFindNearestTarget(game, e, FACTION_GANG_JERUSALEM, 500.0f);
        if (target_id < 0 && player->heat > 0) target_id = EntityFindNearestTarget(game, e, FACTION_PLAYER, 500.0f);

        if (target_id >= 0) {
            Entity* target = &game->entities[target_id];
            e->current_target_id = target_id;
            float tdist = EntityDistance(e, target);

            if (tdist < 100.0f && e->ai_timer > 0.8f) {
                float angle = atan2f(target->y - e->y, target->x - e->x);
                Entity* bullet = EntityCreate(game, ENTITY_BULLET, e->x, e->y, angle);
                if (bullet) {
                    bullet->vel_x = cosf(angle) * 500.0f;
                    bullet->vel_y = sinf(angle) * 500.0f;
                    bullet->damage = 15;
                    bullet->owner = e;
                    bullet->lifetime = 2.0f;
                    bullet->width = 8;
                    bullet->height = 8;
                }
                e->ai_timer = 0.0f;
            } else {
                EntityMoveToward(e, target->x, target->y, 150.0f, dt);
            }
        }
    } else {
        if (e->patrol_count == 0) {
            for (int i = 0; i < 4; i++) {
                e->patrol_points[i][0] = 500 + (rand() % 2000);
                e->patrol_points[i][1] = 500 + (rand() % 2000);
            }
            e->patrol_count = 4;
            e->patrol_index = 0;
        }
        if (e->patrol_count > 0) {
            float tx = e->patrol_points[e->patrol_index][0];
            float ty = e->patrol_points[e->patrol_index][1];
            float d = sqrtf((tx - e->x) * (tx - e->x) + (ty - e->y) * (ty - e->y));
            if (d < 50.0f) {
                e->patrol_index = (e->patrol_index + 1) % e->patrol_count;
            } else {
                EntityMoveToward(e, tx, ty, 80.0f, dt);
            }
        }
    }
}

void EnemySpawnWave(Game* game, int wave) {
    int count = 3 + wave * 2;
    if (count > 20) count = 20;

    for (int i = 0; i < count; i++) {
        float angle = (float)rand() / RAND_MAX * 2.0f * PI;
        float dist = 500.0f + (float)rand() / RAND_MAX * 500.0f;
        float x = WORLD_WIDTH / 2 + cosf(angle) * dist;
        float y = WORLD_HEIGHT / 2 + sinf(angle) * dist;

        if (x < 100) x = 100;
        if (x > WORLD_WIDTH - 100) x = WORLD_WIDTH - 100;
        if (y < 100) y = 100;
        if (y > WORLD_HEIGHT - 100) y = WORLD_HEIGHT - 100;

        Faction faction;
        int r = rand() % 100;
        if (r < 40) faction = FACTION_CIVILIAN;
        else if (r < 60) faction = FACTION_GANG_MUNGIKI;
        else if (r < 80) faction = FACTION_GANG_TALIBAN;
        else faction = FACTION_GANG_JERUSALEM;

        Entity* e = EntityCreate(game, ENTITY_CIVILIAN, x, y, 0.0f);
        if (e) {
            e->faction = faction;
            e->health = 50 + wave * 10;
            e->max_health = e->health;
            e->weapon = (rand() % 3) + 1;
            e->speed = 100.0f + (float)(rand() % 50);
        }
    }

    if (wave % 5 == 0) {
        float x = WORLD_WIDTH / 2 + (rand() % 400 - 200);
        float y = WORLD_HEIGHT / 2 + (rand() % 400 - 200);
        Entity* boss = EntityCreate(game, ENTITY_BOSS, x, y, 0.0f);
        if (boss) {
            boss->faction = FACTION_GANG_MUNGIKI;
            boss->health = 500 + wave * 50;
            boss->max_health = boss->health;
            boss->weapon = WEAPON_AK47;
            boss->speed = 150.0f;
            boss->width = 48;
            boss->height = 64;
        }
    }
}

Entity* EntityCreateEnemy(Game* game, Faction faction, float x, float y) {
    EntityType type = ENTITY_CIVILIAN;
    switch (faction) {
        case FACTION_GANG_MUNGIKI:
        case FACTION_GANG_TALIBAN:
        case FACTION_GANG_JERUSALEM:
            type = ENTITY_GANG_MUNGIKI;
            break;
        case FACTION_POLICE:
            type = ENTITY_POLICE;
            break;
        default:
            type = ENTITY_CIVILIAN;
    }
    Entity* e = EntityCreate(game, type, x, y, 0.0f);
    if (e) {
        e->faction = faction;
    }
    return e;
}

void EntityMoveToward(Entity* e, float targetX, float targetY, float speed, float dt) {
    float dx = targetX - e->x;
    float dy = targetY - e->y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > 0.1f) {
        e->x += (dx / dist) * speed * dt;
        e->y += (dy / dist) * speed * dt;
        e->angle = atan2f(dy, dx);
    }
}

void EntityAttack(Entity* attacker, Entity* target) {
    if (!attacker || !target) return;
    float dist = EntityDistance(attacker, target);
    if (dist < 60.0f) {
        target->health -= 10;
        if (target->health <= 0) {
            target->active = false;
        }
    }
}

int EntityFindNearestTarget(Game* game, Entity* e, Faction targetFaction, float maxRange) {
    float nearestDist = maxRange;
    int nearestId = -1;

    for (int i = 0; i < game->entity_count; i++) {
        Entity* target = &game->entities[i];
        if (target->active && target->faction == targetFaction && target != e) {
            float dist = EntityDistance(e, target);
            if (dist < nearestDist) {
                nearestDist = dist;
                nearestId = i;
            }
        }
    }
    return nearestId;
}

float EntityDistance(Entity* a, Entity* b) {
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    return sqrtf(dx * dx + dy * dy);
}