#include "nairobi_streets.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char* g_slang[] = {
    "sawa", "poa", "fiti", "mzuri", "chap chap", "hamna shida",
    "lakini", "bado", "sasa", "leo", "kesho", "jana",
    "msee", "dada", "kaka", "buda", "shosho", "bibi",
    "chakula", "bia", "pombe", "safari", "gari", "boda",
    "mkubwa", "mdogo", "hashara", "faida", "hali", "mambo"
};

static const char* g_faction_names[] = {
    "Civilian", "Mungiki", "Taliban", "Jerusalem", "Police"
};

static const char* g_weapon_names[] = {
    "fists", "knife", "pistol", "shotgun", "AK-47", "sniper", "RPG", "grenade", "molotov"
};

static char g_dialogue_buffer[1024];

static const char* g_spot_templates[] = {
    "%s: Oi! %s! Unafanya nini hapa?",
    "%s: Sasa %s, uko na nini?",
    "%s: Wewe %s, simama hapo!",
    "%s: Eh %s, unaenda wapi?"
};

static const char* g_engage_templates[] = {
    "%s: Leo utapata! %s!",
    "%s: Nitakumaliza %s!",
    "%s: Hii ni eneo langu %s!",
    "%s: Utaona %s, usinione tena!"
};

static const char* g_taunt_templates[] = {
    "%s: Ha ha ha! Unakaa mgonjwa %s!",
    "%s: Ni kama huendi shule %s!",
    "%s: Mungu akikupe fursa %s!",
    "%s: Umeshindwa kabisa %s!"
};

static const char* g_flee_templates[] = {
    "%s: Acha! Nitakimbia!",
    "%s: Mungu nisaidie!",
    "%s: Sina nguvu tena!",
    "%s: Utanipata kesho!"
};

static const char* g_dying_templates[] = {
    "%s: Aaaah! Nimeuawa na %s...",
    "%s: Usiniache hapa %s...",
    "%s: Familia yangu... %s...",
    "%s: Kifo... si mwisho %s..."
};

static const char* g_kill_templates[] = {
    "%s: Moja imeenda! %s!",
    "%s: Nilikuwa najua nitakushinda %s!",
    "%s: Hivi ndivyo hivi %s!",
    "%s: Mwingine! %s!"
};

static const char* g_backup_templates[] = {
    "%s: Watu wangu! Njoo haraka!",
    "%s: Msaada! %s ananikimbiza!",
    "%s: Timu! Tushikane pamoja!",
    "%s: Backup! Sasa hivi!"
};

static const char* g_win_templates[] = {
    "%s: Nilishinda! %s!",
    "%s: Hii ni ya kawaida %s!",
    "%s: Mambo yamekwisha %s!",
    "%s: Nimetoka mzinga %s!"
};

static const char* g_greeting_templates[] = {
    "%s: Mambo %s! Uko poa?",
    "%s: Sasa %s, habari yako?",
    "%s: Vipi %s, mambo vipi?",
    "%s: Jambo %s, uko sawa?"
};

static const char* g_threaten_templates[] = {
    "%s: Usinipigie kelele %s!",
    "%s: Nitakufanya kumbuka %s!",
    "%s: Hii ni chongo %s!",
    "%s: Usijaribu %s!"
};

static const char* g_extort_templates[] = {
    "%s: Toa kitu kidogo %s...",
    "%s: Hapa tulipa kodi %s!",
    "%s: Pesa au mate %s!",
    "%s: Nipe 500 bob %s!"
};

void DialogueInit(void) {
    srand((unsigned int)time(NULL));
}

void DialogueClose(void) {
}

char* DialogueGenerate(Entity* speaker, Entity* target, DialogueContext ctx) {
    const char* speaker_name = DialogueGetFactionName(ctx.faction);
    const char* target_name = target ? "wewe" : "msee";
    const char* slang = DialogueGetRandomSlang();
    const char* location = DialogueGetLocationName(speaker->x, speaker->y);
    const char* weapon = DialogueGetWeaponName(ctx.playerWeapon);

    const char** templates = NULL;
    int template_count = 0;

    switch (ctx.situation) {
        case DIALOGUE_SPOT_PLAYER:
            templates = g_spot_templates;
            template_count = 4;
            break;
        case DIALOGUE_ENGAGE:
            templates = g_engage_templates;
            template_count = 4;
            break;
        case DIALOGUE_TAUNT:
            templates = g_taunt_templates;
            template_count = 4;
            break;
        case DIALOGUE_FLEE:
            templates = g_flee_templates;
            template_count = 4;
            break;
        case DIALOGUE_DYING:
            templates = g_dying_templates;
            template_count = 4;
            break;
        case DIALOGUE_KILL_PLAYER:
            templates = g_kill_templates;
            template_count = 4;
            break;
        case DIALOGUE_CALL_BACKUP:
            templates = g_backup_templates;
            template_count = 4;
            break;
        case DIALOGUE_WIN_FIGHT:
            templates = g_win_templates;
            template_count = 4;
            break;
        case DIALOGUE_GREETING:
            templates = g_greeting_templates;
            template_count = 4;
            break;
        case DIALOGUE_THREATEN:
            templates = g_threaten_templates;
            template_count = 4;
            break;
        case DIALOGUE_EXTORT:
            templates = g_extort_templates;
            template_count = 4;
            break;
        default:
            return "...";
    }

    int idx = rand() % template_count;
    const char* tmpl = templates[idx];

    snprintf(g_dialogue_buffer, sizeof(g_dialogue_buffer), tmpl,
             speaker_name, target_name, slang, location, weapon);

    return g_dialogue_buffer;
}

const char* DialogueGetFactionName(Faction faction) {
    if (faction >= 0 && faction < FACTION_COUNT) {
        return g_faction_names[faction];
    }
    return "Unknown";
}

const char* DialogueGetWeaponName(WeaponType weapon) {
    if (weapon >= 0 && weapon < WEAPON_COUNT) {
        return g_weapon_names[weapon];
    }
    return "none";
}

const char* DialogueGetLocationName(float x, float y) {
    int grid_x = (int)(x / ROAD_GRID_SIZE);
    int grid_y = (int)(y / ROAD_GRID_SIZE);
    int center_grid = (WORLD_WIDTH / ROAD_GRID_SIZE) / 2;

    if (grid_x >= center_grid - 1 && grid_x <= center_grid + 1 &&
        grid_y >= center_grid - 1 && grid_y <= center_grid + 1) {
        return "CBD";
    } else if (grid_x < center_grid - 3 || grid_x > center_grid + 3 ||
               grid_y < center_grid - 3 || grid_y > center_grid + 3) {
        return "slums";
    } else {
        return "estate";
    }
}

const char* DialogueGetRandomSlang(void) {
    return g_slang[rand() % (sizeof(g_slang) / sizeof(g_slang[0]))];
}

TimeOfDay DialogueGetTimeOfDay(float hour) {
    if (hour >= 5 && hour < 7) return TIME_DAWN;
    if (hour >= 7 && hour < 17) return TIME_DAY;
    if (hour >= 17 && hour < 19) return TIME_DUSK;
    return TIME_NIGHT;
}

DialogueRelation DialogueGetRelation(Entity* speaker, Entity* target, int playerRespect, int playerHeat) {
    if (speaker->faction == FACTION_POLICE) {
        if (playerHeat > 50) return RELATION_HOSTILE;
        if (playerRespect > 100) return RELATION_RESPECTFUL;
        return RELATION_NEUTRAL;
    }

    if (speaker->faction == FACTION_CIVILIAN) {
        if (playerHeat > 30) return RELATION_FEARFUL;
        if (playerRespect > 50) return RELATION_RESPECTFUL;
        return RELATION_NEUTRAL;
    }

    if (speaker->faction >= FACTION_MUNGIKI && speaker->faction <= FACTION_JERUSALEM) {
        if (target && target->faction == speaker->faction) return RELATION_RESPECTFUL;
        if (playerHeat > 80) return RELATION_FEARFUL;
        if (playerRespect > 200) return RELATION_RESPECTFUL;
        return RELATION_HOSTILE;
    }

    return RELATION_NEUTRAL;
}