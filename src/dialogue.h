#ifndef DIALOGUE_H
#define DIALOGUE_H

#include "common.h"

void DialogueInit(void);
void DialogueClose(void);
char* DialogueGenerate(Entity *speaker, Entity *target, DialogueContext ctx);
const char* DialogueGetFactionName(Faction faction);
const char* DialogueGetWeaponName(WeaponType weapon);
const char* DialogueGetLocationName(float x, float y);
const char* DialogueGetRandomSlang(void);
TimeOfDay DialogueGetTimeOfDay(float hour);
DialogueRelation DialogueGetRelation(Entity *speaker, Entity *target, int playerRespect, int playerHeat);

#endif