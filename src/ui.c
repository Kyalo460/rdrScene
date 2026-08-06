#include "ui.h"
#include "raymath.h"
#include <string.h>
#include <stdio.h>

UIAssets uiAssets = {0};

static void DrawBar(float x, float y, float width, float height, float value, float maxValue, Color bgColor, Color fillColor, Color borderColor) {
    DrawRectangle(x, y, width, height, bgColor);
    if (maxValue > 0) {
        float fillWidth = (value / maxValue) * width;
        DrawRectangle(x, y, fillWidth, height, fillColor);
    }
    DrawRectangleLinesEx((Rectangle){x, y, width, height}, 2, borderColor);
}

static void DrawTextShadowed(const char *text, int x, int y, int fontSize, Color color, Color shadowColor) {
    DrawText(text, x + 1, y + 1, fontSize, shadowColor);
    DrawText(text, x, y, fontSize, color);
}

static void DrawTextureCentered(Texture2D texture, float x, float y, float scale, Color tint) {
    DrawTextureEx(texture, (Vector2){x - (texture.width * scale) / 2, y - (texture.height * scale) / 2}, 0, scale, tint);
}

void UIInit(void) {
    uiAssets.font = GetFontDefault();
    uiAssets.fontBold = GetFontDefault();

    uiAssets.nairobiGold = (Color){255, 215, 0, 255};
    uiAssets.nairobiDark = (Color){15, 15, 20, 255};
    uiAssets.nairobiRed = (Color){220, 20, 60, 255};
    uiAssets.nairobiBlue = (Color){30, 144, 255, 255};
    uiAssets.nairobiYellow = (Color){255, 223, 0, 255};

    Image panelImg = GenImageColor(300, 200, uiAssets.nairobiDark);
    ImageDrawRectangle(&panelImg, 0, 0, 300, 200, (Color){20, 20, 30, 230});
    ImageDrawRectangleLines(&panelImg, 0, 0, 300, 200, 2, uiAssets.nairobiGold);
    uiAssets.hudPanel = LoadTextureFromImage(panelImg);
    UnloadImage(panelImg);

    Image maskImg = GenImageColor(200, 200, BLANK);
    ImageDrawCircle(&maskImg, 100, 100, 100, WHITE);
    uiAssets.minimapMask = LoadTextureFromImage(maskImg);
    UnloadImage(maskImg);

    for (int i = 0; i < 7; i++) {
        Image iconImg = GenImageColor(32, 32, BLANK);
        Color colors[7] = {uiAssets.nairobiGold, uiAssets.nairobiRed, uiAssets.nairobiBlue, 
                          uiAssets.nairobiYellow, ORANGE, PURPLE, GREEN};
        ImageDrawRectangle(&iconImg, 4, 4, 24, 24, colors[i]);
        ImageDrawRectangleLines(&iconImg, 4, 4, 24, 24, 1, WHITE);
        uiAssets.weaponIcons[i] = LoadTextureFromImage(iconImg);
        UnloadImage(iconImg);
    }

    Image heartImg = GenImageColor(16, 16, RED);
    uiAssets.heartIcon = LoadTextureFromImage(heartImg);
    UnloadImage(heartImg);

    Image staminaImg = GenImageColor(16, 16, YELLOW);
    uiAssets.staminaIcon = LoadTextureFromImage(staminaImg);
    UnloadImage(staminaImg);

    Image armorImg = GenImageColor(16, 16, BLUE);
    uiAssets.armorIcon = LoadTextureFromImage(armorImg);
    UnloadImage(armorImg);

    Image cashImg = GenImageColor(16, 16, uiAssets.nairobiGold);
    uiAssets.cashIcon = LoadTextureFromImage(cashImg);
    UnloadImage(cashImg);

    Image respectImg = GenImageColor(16, 16, PURPLE);
    uiAssets.respectIcon = LoadTextureFromImage(respectImg);
    UnloadImage(respectImg);

    for (int i = 0; i < 5; i++) {
        Image heatImg = GenImageColor(16, 16, BLANK);
        for (int s = 0; s <= i; s++) {
            ImageDrawTriangle(&heatImg, 
                (Vector2){8 + s * 3, 2}, 
                (Vector2){5 + s * 3, 14}, 
                (Vector2){11 + s * 3, 14}, 
                uiAssets.nairobiGold);
        }
        uiAssets.heatIcons[i] = LoadTextureFromImage(heatImg);
        UnloadImage(heatImg);
    }
}

void UIClose(void) {
    UnloadFont(uiAssets.font);
    UnloadFont(uiAssets.fontBold);
    UnloadTexture(uiAssets.hudPanel);
    UnloadTexture(uiAssets.minimapMask);
    for (int i = 0; i < 7; i++) UnloadTexture(uiAssets.weaponIcons[i]);
    UnloadTexture(uiAssets.heartIcon);
    UnloadTexture(uiAssets.staminaIcon);
    UnloadTexture(uiAssets.armorIcon);
    UnloadTexture(uiAssets.cashIcon);
    UnloadTexture(uiAssets.respectIcon);
    for (int i = 0; i < 5; i++) UnloadTexture(uiAssets.heatIcons[i]);
}

void UIDraw(void) {
}

void UIDrawHUD(PlayerHUD *hud) {
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

    DrawRectangle(10, 10, 280, 100, (Color){15, 15, 20, 200});
    DrawRectangleLinesEx((Rectangle){10, 10, 280, 100}, 2, uiAssets.nairobiGold);

    DrawTexture(uiAssets.heartIcon, 20, 20, WHITE);
    DrawBar(45, 22, 230, 18, hud->health, hud->maxHealth, 
            (Color){60, 0, 0, 200}, uiAssets.nairobiRed, uiAssets.nairobiGold);
    char healthText[32];
    sprintf(healthText, "%.0f/%.0f", hud->health, hud->maxHealth);
    DrawTextShadowed(healthText, 45, 42, 16, WHITE, BLACK);

    DrawTexture(uiAssets.staminaIcon, 20, 50, WHITE);
    DrawBar(45, 52, 230, 18, hud->stamina, hud->maxStamina,
            (Color){60, 60, 0, 200}, uiAssets.nairobiYellow, uiAssets.nairobiGold);
    char staminaText[32];
    sprintf(staminaText, "%.0f/%.0f", hud->stamina, hud->maxStamina);
    DrawTextShadowed(staminaText, 45, 72, 16, WHITE, BLACK);

    DrawTexture(uiAssets.armorIcon, 20, 80, WHITE);
    DrawBar(45, 82, 230, 18, hud->armor, hud->maxArmor,
            (Color){0, 0, 60, 200}, uiAssets.nairobiBlue, uiAssets.nairobiGold);
    char armorText[32];
    sprintf(armorText, "%.0f/%.0f", hud->armor, hud->maxArmor);
    DrawTextShadowed(armorText, 45, 102, 16, WHITE, BLACK);

    DrawRectangle(screenW - 300, 10, 290, 140, (Color){15, 15, 20, 200});
    DrawRectangleLinesEx((Rectangle){screenW - 300, 10, 290, 140}, 2, uiAssets.nairobiGold);

    DrawTexture(uiAssets.cashIcon, screenW - 290, 20, WHITE);
    char cashText[32];
    sprintf(cashText, "KSh %d", hud->cash);
    DrawTextShadowed(cashText, screenW - 265, 20, 20, uiAssets.nairobiGold, BLACK);

    DrawTexture(uiAssets.respectIcon, screenW - 290, 50, WHITE);
    char respectText[32];
    sprintf(respectText, "Respect %d", hud->respect);
    DrawTextShadowed(respectText, screenW - 265, 50, 20, PURPLE, BLACK);

    if (hud->heatLevel > 0 && hud->heatLevel <= 5) {
        DrawTexture(uiAssets.heatIcons[hud->heatLevel - 1], screenW - 290, 80, WHITE);
    }
    char heatText[32];
    sprintf(heatText, "Heat %d/5", hud->heatLevel);
    DrawTextShadowed(heatText, screenW - 265, 80, 20, uiAssets.nairobiRed, BLACK);

    if (hud->currentWeapon >= 0 && hud->currentWeapon < 7) {
        DrawTextureEx(uiAssets.weaponIcons[hud->currentWeapon], 
                     (Vector2){screenW - 290, 110}, 0, 2, WHITE);
    }
    char ammoText[32];
    sprintf(ammoText, "%d / %d", hud->ammo, hud->maxAmmo);
    DrawTextShadowed(ammoText, screenW - 220, 120, 20, WHITE, BLACK);

    UIDrawMinimap(hud, NULL, 0, NULL, 0, NULL, 0);
}

void UIDrawMinimap(PlayerHUD *hud, Vector2 *enemies, int enemyCount, Vector2 *missions, int missionCount, Vector2 *police, int policeCount) {
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    int mapSize = 200;
    int mapX = screenW - mapSize - 10;
    int mapY = screenH - mapSize - 10;

    DrawRectangle(mapX, mapY, mapSize, mapSize, (Color){10, 10, 15, 220});
    DrawRectangleLinesEx((Rectangle){mapX, mapY, mapSize, mapSize}, 2, uiAssets.nairobiGold);

    float scale = mapSize / 2000.0f;
    int centerX = mapX + mapSize / 2;
    int centerY = mapY + mapSize / 2;

    for (int i = 0; i < enemyCount; i++) {
        int ex = centerX + (int)((enemies[i].x - hud->position.x) * scale);
        int ey = centerY + (int)((enemies[i].y - hud->position.y) * scale);
        if (ex >= mapX && ex <= mapX + mapSize && ey >= mapY && ey <= mapY + mapSize) {
            DrawCircle(ex, ey, 4, uiAssets.nairobiRed);
        }
    }

    for (int i = 0; i < missionCount; i++) {
        int mx = centerX + (int)((missions[i].x - hud->position.x) * scale);
        int my = centerY + (int)((missions[i].y - hud->position.y) * scale);
        if (mx >= mapX && mx <= mapX + mapSize && my >= mapY && my <= mapY + mapSize) {
            DrawCircle(mx, my, 5, uiAssets.nairobiYellow);
        }
    }

    for (int i = 0; i < policeCount; i++) {
        int px = centerX + (int)((police[i].x - hud->position.x) * scale);
        int py = centerY + (int)((police[i].y - hud->position.y) * scale);
        if (px >= mapX && px <= mapX + mapSize && py >= mapY && py <= mapY + mapSize) {
            DrawCircle(px, py, 4, uiAssets.nairobiBlue);
        }
    }

    DrawCircle(centerX, centerY, 6, WHITE);
    DrawCircleLines(centerX, centerY, 6, uiAssets.nairobiGold);
}

void UIDrawMission(MissionData *mission) {
    if (!mission->active) return;

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    int panelW = 600;
    int panelH = 500;
    int panelX = (screenW - panelW) / 2;
    int panelY = (screenH - panelH) / 2;

    DrawRectangle(panelX, panelY, panelW, panelH, (Color){15, 15, 20, 240});
    DrawRectangleLinesEx((Rectangle){panelX, panelY, panelW, panelH}, 3, uiAssets.nairobiGold);

    DrawTextShadowed(mission->title, panelX + 20, panelY + 20, 28, uiAssets.nairobiGold, BLACK);
    
    DrawRectangle(panelX + 20, panelY + 65, panelW - 40, 2, uiAssets.nairobiGold);

    DrawTextShadowed("DESCRIPTION", panelX + 20, panelY + 80, 18, uiAssets.nairobiYellow, BLACK);
    DrawTextShadowed(mission->description, panelX + 20, panelY + 110, 16, WHITE, BLACK);

    DrawRectangle(panelX + 20, panelY + 150, panelW - 40, 2, uiAssets.nairobiGold);

    DrawTextShadowed("OBJECTIVES", panelX + 20, panelY + 165, 18, uiAssets.nairobiYellow, BLACK);
    
    for (int i = 0; i < mission->objectiveCount; i++) {
        int objY = panelY + 200 + i * 35;
        Color checkColor = mission->objectiveComplete[i] ? GREEN : uiAssets.nairobiGold;
        DrawRectangleLinesEx((Rectangle){panelX + 30, objY, 20, 20}, 2, checkColor);
        if (mission->objectiveComplete[i]) {
            DrawTextShadowed("✓", panelX + 34, objY, 18, GREEN, BLACK);
        }
        DrawTextShadowed(mission->objectives[i], panelX + 60, objY, 16, 
                        mission->objectiveComplete[i] ? GRAY : WHITE, BLACK);
    }

    DrawRectangle(panelX + 20, panelY + 430, panelW - 40, 2, uiAssets.nairobiGold);

    int minutes = (int)mission->timer / 60;
    int seconds = (int)mission->timer % 60;
    char timerText[32];
    sprintf(timerText, "TIME: %02d:%02d", minutes, seconds);
    DrawTextShadowed(timerText, panelX + 20, panelY + 440, 20, 
                    mission->timer < 60 ? uiAssets.nairobiRed : uiAssets.nairobiYellow, BLACK);

    char rewardsText[64];
    sprintf(rewardsText, "REWARDS: KSh %d  |  Respect %d", mission->rewardCash, mission->rewardRespect);
    DrawTextShadowed(rewardsText, panelX + 20, panelY + 470, 18, uiAssets.nairobiGold, BLACK);
}

void UIDrawWeaponWheel(WeaponWheel *wheel) {
    if (!wheel->visible) return;

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    wheel->center = (Vector2){screenW / 2.0f, screenH / 2.0f};
    wheel->radius = 180;

    DrawRectangle(0, 0, screenW, screenH, (Color){0, 0, 0, 180});

    for (int i = 0; i < wheel->weaponCount; i++) {
        float angle = (i * 360.0f / wheel->weaponCount - 90) * DEG2RAD;
        float x = wheel->center.x + cosf(angle) * wheel->radius;
        float y = wheel->center.y + sinf(angle) * wheel->radius;

        Color slotColor = (i == wheel->selectedWeapon) ? uiAssets.nairobiGold : (Color){40, 40, 50, 200};
        Color borderColor = (i == wheel->selectedWeapon) ? WHITE : uiAssets.nairobiGold;

        DrawCircle(x, y, 55, slotColor);
        DrawCircleLines(x, y, 55, borderColor);

        if (wheel->weaponTypes[i] >= 0 && wheel->weaponTypes[i] < 7) {
            DrawTextureEx(uiAssets.weaponIcons[wheel->weaponTypes[i]], 
                         (Vector2){x - 32, y - 32}, 0, 2, WHITE);
        }

        char weaponName[16];
        const char *names[7] = {"Fists", "Pistol", "Shotgun", "AK-47", "Sniper", "RPG", "Grenade"};
        if (wheel->weaponTypes[i] >= 0 && wheel->weaponTypes[i] < 7) {
            strcpy(weaponName, names[wheel->weaponTypes[i]]);
            DrawTextShadowed(weaponName, x - MeasureText(weaponName, 14) / 2, y + 45, 14, WHITE, BLACK);
        }
    }

    DrawTextShadowed("HOLD Q - MOVE MOUSE TO SELECT - RELEASE Q", 
                    screenW / 2 - 200, screenH - 50, 18, uiAssets.nairobiGold, BLACK);
}

void UIUpdateWeaponWheel(WeaponWheel *wheel, Vector2 mousePos) {
    if (!wheel->visible) return;

    Vector2 dir = Vector2Subtract(mousePos, wheel->center);
    float distance = Vector2Length(dir);
    
    if (distance < wheel->radius * 0.3f) {
        wheel->selectedWeapon = -1;
        return;
    }

    float angle = atan2f(dir.y, dir.x) * RAD2DEG + 90;
    if (angle < 0) angle += 360;
    
    int sector = (int)(angle / (360.0f / wheel->weaponCount));
    if (sector >= 0 && sector < wheel->weaponCount) {
        wheel->selectedWeapon = sector;
    }
}

void UIDrawWasted(WastedScreen *wasted) {
    if (!wasted->show) return;

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

    wasted->vignetteAlpha += GetFrameTime() * 80;
    if (wasted->vignetteAlpha > 200) wasted->vignetteAlpha = 200;

    for (int i = 0; i < screenH; i += 2) {
        float alpha = (wasted->vignetteAlpha / 200.0f) * (1.0f - fabsf(i - screenH / 2) / (screenH / 2.0f));
        DrawRectangle(0, i, screenW, 2, (Color){150, 0, 0, (unsigned char)(alpha * 255)});
    }

    const char *wastedText = "WASTED";
    int textW = MeasureText(wastedText, 120);
    int textX = (screenW - textW) / 2;
    int textY = screenH / 2 - 150;

    for (int i = 0; i < 5; i++) {
        DrawText(wastedText, textX + i, textY + i, 120, (Color){80, 0, 0, 255});
    }
    DrawText(wastedText, textX, textY, 120, uiAssets.nairobiRed);

    char statsText[256];
    sprintf(statsText, "KILLS: %d    TIME SURVIVED: %ds    CASH EARNED: KSh %d", 
            wasted->kills, wasted->timeSurvived, wasted->cashEarned);
    int statsW = MeasureText(statsText, 24);
    DrawTextShadowed(statsText, (screenW - statsW) / 2, textY + 150, 24, uiAssets.nairobiGold, BLACK);

    const char *continueText = "PRESS SPACE TO CONTINUE";
    int contW = MeasureText(continueText, 28);
    float pulse = (sinf(GetTime() * 4) + 1) * 0.5f;
    Color pulseColor = (Color){
        (unsigned char)(255 * pulse), 
        (unsigned char)(215 * pulse), 
        0, 
        255
    };
    DrawTextShadowed(continueText, (screenW - contW) / 2, textY + 220, 28, pulseColor, BLACK);
}

void UISetWasted(WastedScreen *wasted, bool show, int kills, int timeSurvived, int cashEarned) {
    wasted->show = show;
    wasted->vignetteAlpha = 0;
    wasted->kills = kills;
    wasted->timeSurvived = timeSurvived;
    wasted->cashEarned = cashEarned;
}

void UIStartDialogue(DialogueBox *dialogue, const char *name, const char *portraitPath, const char *text) {
    strncpy(dialogue->characterName, name, sizeof(dialogue->characterName) - 1);
    strncpy(dialogue->portraitPath, portraitPath, sizeof(dialogue->portraitPath) - 1);
    strncpy(dialogue->dialogue, text, sizeof(dialogue->dialogue) - 1);
    dialogue->dialogueIndex = 0;
    dialogue->typewriterTimer = 0;
    dialogue->typewriterSpeed = 0.02f;
    dialogue->show = true;
    
    if (dialogue->portrait.id == 0 && strlen(portraitPath) > 0) {
        dialogue->portrait = LoadTexture(portraitPath);
    }
}

void UIUpdateDialogue(DialogueBox *dialogue, float dt) {
    if (!dialogue->show) return;

    dialogue->typewriterTimer += dt;
    int targetIndex = (int)(dialogue->typewriterTimer / dialogue->typewriterSpeed);
    if (targetIndex > (int)strlen(dialogue->dialogue)) {
        targetIndex = strlen(dialogue->dialogue);
    }
    dialogue->dialogueIndex = targetIndex;

    if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (dialogue->dialogueIndex >= (int)strlen(dialogue->dialogue)) {
            dialogue->show = false;
        } else {
            dialogue->dialogueIndex = strlen(dialogue->dialogue);
            dialogue->typewriterTimer = strlen(dialogue->dialogue) * dialogue->typewriterSpeed;
        }
    }
}

void UIDrawDialogue(DialogueBox *dialogue) {
    if (!dialogue->show) return;

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    int boxH = 180;
    int boxY = screenH - boxH - 20;
    int boxX = 20;
    int boxW = screenW - 40;

    DrawRectangle(boxX, boxY, boxW, boxH, (Color){15, 15, 20, 230});
    DrawRectangleLinesEx((Rectangle){boxX, boxY, boxW, boxH}, 2, uiAssets.nairobiGold);

    if (dialogue->portrait.id > 0) {
        DrawTextureEx(dialogue->portrait, (Vector2){boxX + 20, boxY + 20}, 0, 3, WHITE);
        DrawRectangleLinesEx((Rectangle){boxX + 20, boxY + 20, 96, 96}, 2, uiAssets.nairobiGold);
    }

    DrawTextShadowed(dialogue->characterName, boxX + 140, boxY + 20, 24, uiAssets.nairobiGold, BLACK);

    char visibleText[513];
    strncpy(visibleText, dialogue->dialogue, dialogue->dialogueIndex);
    visibleText[dialogue->dialogueIndex] = '\0';
    
    DrawTextShadowed(visibleText, boxX + 140, boxY + 55, 20, WHITE, BLACK);

    if (dialogue->dialogueIndex >= (int)strlen(dialogue->dialogue)) {
        const char *continueHint = "► PRESS SPACE TO CONTINUE";
        float pulse = (sinf(GetTime() * 4) + 1) * 0.5f;
        Color hintColor = (Color){
            (unsigned char)(200 * pulse + 55), 
            (unsigned char)(160 * pulse + 55), 
            0, 
            255
        };
        DrawTextShadowed(continueHint, boxX + boxW - MeasureText(continueHint, 18) - 20, boxY + boxH - 35, 18, hintColor, BLACK);
    }
}

void UISetWeaponWheel(WeaponWheel *wheel, bool visible, Vector2 center) {
    wheel->visible = visible;
    wheel->center = center;
    wheel->selectedWeapon = -1;
    if (visible) {
        wheel->weaponCount = 7;
        for (int i = 0; i < 7; i++) wheel->weaponTypes[i] = i;
    }
}