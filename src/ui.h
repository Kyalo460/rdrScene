#ifndef UI_H
#define UI_H

#include "common.h"

void UIInit(void);
void UIClose(void);
void UIDraw(void);
void UIDrawHUD(PlayerHUD *hud);
void UIDrawMission(MissionData *mission);
void UIDrawWeaponWheel(WeaponWheel *wheel);
void UIDrawWasted(WastedScreen *wasted);
void UIDrawDialogue(DialogueBox *dialogue);
void UIUpdateDialogue(DialogueBox *dialogue, float dt);
void UIStartDialogue(DialogueBox *dialogue, const char *name, const char *portraitPath, const char *text);
void UISetWasted(WastedScreen *wasted, bool show, int kills, int timeSurvived, int cashEarned);
void UISetWeaponWheel(WeaponWheel *wheel, bool visible, Vector2 center);
void UIUpdateWeaponWheel(WeaponWheel *wheel, Vector2 mousePos);
void UIDrawMinimap(PlayerHUD *hud, Vector2 *enemies, int enemyCount, Vector2 *missions, int missionCount, Vector2 *police, int policeCount);

#endif