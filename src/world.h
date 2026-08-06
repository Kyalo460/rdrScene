#ifndef WORLD_H
#define WORLD_H

#include "common.h"

void WorldInit(uint32_t seed);
void WorldUpdate(float dt);
void WorldDraw(void);
Vector2 WorldGetSpawnPoint(Faction faction);
bool WorldIsWalkable(Vector2 pos);

#endif