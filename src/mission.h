#ifndef MISSION_H
#define MISSION_H

#include "common.h"

void MissionInit(void);
void MissionUpdate(float dt);
int MissionStart(MissionType type);
void MissionComplete(int id);
void MissionFail(int id);
Mission* MissionGet(int id);
Mission* MissionGetActive(void);
bool MissionIsActive(int id);
int MissionGetCount(void);

#endif