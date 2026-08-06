#ifndef ENEMY_H
#define ENEMY_H

#include "common.h"

void EnemyInit(void);
void EnemyUpdate(float dt);
void EnemyAI(Entity *e, float dt);
void EnemySpawnWave(int wave);
Entity* EntityCreateEnemy(Faction faction, float x, float y);
void EntityMoveToward(Entity *e, float targetX, float targetY, float speed, float dt);
void EntityAttack(Entity *attacker, Entity *target);
int EntityFindNearestTarget(Entity *e, Faction targetFaction, float maxRange);

#endif