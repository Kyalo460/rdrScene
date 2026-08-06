#ifndef ENEMY_H
#define ENEMY_H

#include "common.h"

void EnemyInit(Game* game);
void EnemyUpdate(Game* game, float dt);
void EnemyAI(Game* game, Entity *e, float dt);
void EnemySpawnWave(Game* game, int wave);
Entity* EntityCreateEnemy(Game* game, Faction faction, float x, float y);
void EntityMoveToward(Entity *e, float targetX, float targetY, float speed, float dt);
void EntityAttack(Entity *attacker, Entity *target);
int EntityFindNearestTarget(Entity *e, Faction targetFaction, float maxRange);

#endif