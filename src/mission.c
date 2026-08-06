#include "mission.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static MissionSystem g_mission_system = {0};
static int g_next_mission_id = 1;

static const char* g_survive_titles[] = {
    "Hold the Line", "Last Stand", "Siege Survival", "Wave Defense"
};
static const char* g_deliver_titles[] = {
    "Courier Run", "Package Drop", "Smuggle Route", "Express Delivery"
};
static const char* g_assassinate_titles[] = {
    "High Value Target", "Silent Takedown", "Elimination Order", "Contract Kill"
};
static const char* g_territory_titles[] = {
    "Turf War", "Clear the Block", "Gang Sweep", "Territory Claim"
};
static const char* g_escape_titles[] = {
    "Breakout", "Hot Pursuit", "Escape Route", "Flee the Scene"
};
static const char* g_boss_titles[] = {
    "Kingpin Showdown", "Warlord Hunt", "Boss Battle", "Final Confrontation"
};

static const char* g_survive_descs[] = {
    "Survive waves of enemies for the time limit.",
    "Hold your position against increasing enemy waves.",
    "Defend the safehouse from siege.",
    "Endure the onslaught until extraction."
};
static const char* g_deliver_descs[] = {
    "Pick up the package at point A and deliver to point B before time runs out.",
    "Transport contraband across rival territory.",
    "Complete the drop without being intercepted.",
    "Deliver the goods to the contact on time."
};
static const char* g_assassinate_descs[] = {
    "Locate and eliminate the high-value target.",
    "Take out the boss silently without raising alarm.",
    "Execute the elimination order on the marked target.",
    "Complete the contract on the designated target."
};
static const char* g_territory_descs[] = {
    "Clear the area of rival gang members and hold it for 60 seconds.",
    "Wipe out enemy presence and secure the territory.",
    "Eliminate all hostiles in the zone and maintain control.",
    "Push out the rival gang and defend the block."
};
static const char* g_escape_descs[] = {
    "Reach the exit point while being hunted by enemies.",
    "Escape the ambush and make it to the safe zone.",
    "Flee the area before the hunters catch you.",
    "Navigate to the extraction point under pursuit."
};
static const char* g_boss_descs[] = {
    "Defeat the unique named boss with multiple combat phases.",
    "Take down the warlord through all his battle phases.",
    "Survive the boss fight and claim victory.",
    "Confront and eliminate the kingpin in a multi-phase battle."
};

static float CalculateDifficultyScale(void) {
    return 1.0f;
}

static int GetRandomInt(int min, int max) {
    return min + (rand() % (max - min + 1));
}

static void GenerateSurviveMission(Mission* m, float diff_scale) {
    int idx = GetRandomInt(0, 3);
    strncpy(m->title, g_survive_titles[idx], MAX_MISSION_TITLE - 1);
    strncpy(m->description, g_survive_descs[idx], MAX_MISSION_DESC - 1);
    m->time_limit = 120.0f * diff_scale;
    m->timer = m->time_limit;
    m->required_count = GetRandomInt(15, 30) * diff_scale;
    m->current_count = 0;
    m->cash_reward = 500 + (int)(diff_scale * 300);
    m->rep_reward = 50 + (int)(diff_scale * 30);
    m->target_pos.x = (float)GetRandomInt(-500, 500);
    m->target_pos.y = (float)GetRandomInt(-500, 500);
}

static void GenerateDeliverMission(Mission* m, float diff_scale) {
    int idx = GetRandomInt(0, 3);
    strncpy(m->title, g_deliver_titles[idx], MAX_MISSION_TITLE - 1);
    strncpy(m->description, g_deliver_descs[idx], MAX_MISSION_DESC - 1);
    m->time_limit = 180.0f * diff_scale;
    m->timer = m->time_limit;
    m->required_count = 2;
    m->current_count = 0;
    m->cash_reward = 800 + (int)(diff_scale * 400);
    m->rep_reward = 75 + (int)(diff_scale * 40);
    m->target_pos.x = (float)GetRandomInt(-500, 500);
    m->target_pos.y = (float)GetRandomInt(-500, 500);
}

static void GenerateAssassinateMission(Mission* m, float diff_scale) {
    int idx = GetRandomInt(0, 3);
    strncpy(m->title, g_assassinate_titles[idx], MAX_MISSION_TITLE - 1);
    strncpy(m->description, g_assassinate_descs[idx], MAX_MISSION_DESC - 1);
    m->time_limit = 300.0f * diff_scale;
    m->timer = m->time_limit;
    m->required_count = 1;
    m->current_count = 0;
    m->cash_reward = 1500 + (int)(diff_scale * 800);
    m->rep_reward = 150 + (int)(diff_scale * 80);
    m->target_pos.x = (float)GetRandomInt(-500, 500);
    m->target_pos.y = (float)GetRandomInt(-500, 500);
    m->target_entity_id = GetRandomInt(1000, 9999);
}

static void GenerateTerritoryMission(Mission* m, float diff_scale) {
    int idx = GetRandomInt(0, 3);
    strncpy(m->title, g_territory_titles[idx], MAX_MISSION_TITLE - 1);
    strncpy(m->description, g_territory_descs[idx], MAX_MISSION_DESC - 1);
    m->time_limit = 60.0f * diff_scale;
    m->timer = 0.0f;
    m->required_count = GetRandomInt(10, 20) * diff_scale;
    m->current_count = 0;
    m->cash_reward = 1000 + (int)(diff_scale * 500);
    m->rep_reward = 100 + (int)(diff_scale * 50);
    m->target_pos.x = (float)GetRandomInt(-500, 500);
    m->target_pos.y = (float)GetRandomInt(-500, 500);
}

static void GenerateEscapeMission(Mission* m, float diff_scale) {
    int idx = GetRandomInt(0, 3);
    strncpy(m->title, g_escape_titles[idx], MAX_MISSION_TITLE - 1);
    strncpy(m->description, g_escape_descs[idx], MAX_MISSION_DESC - 1);
    m->time_limit = 240.0f * diff_scale;
    m->timer = m->time_limit;
    m->required_count = 1;
    m->current_count = 0;
    m->cash_reward = 1200 + (int)(diff_scale * 600);
    m->rep_reward = 120 + (int)(diff_scale * 60);
    m->target_pos.x = (float)GetRandomInt(-500, 500);
    m->target_pos.y = (float)GetRandomInt(-500, 500);
}

static void GenerateBossMission(Mission* m, float diff_scale) {
    int idx = GetRandomInt(0, 3);
    strncpy(m->title, g_boss_titles[idx], MAX_MISSION_TITLE - 1);
    strncpy(m->description, g_boss_descs[idx], MAX_MISSION_DESC - 1);
    m->time_limit = 600.0f * diff_scale;
    m->timer = m->time_limit;
    m->required_count = 3;
    m->current_count = 0;
    m->cash_reward = 3000 + (int)(diff_scale * 2000);
    m->rep_reward = 300 + (int)(diff_scale * 200);
    m->target_pos.x = (float)GetRandomInt(-500, 500);
    m->target_pos.y = (float)GetRandomInt(-500, 500);
    m->target_entity_id = GetRandomInt(10000, 19999);
}

void MissionInit(void) {
    memset(&g_mission_system, 0, sizeof(MissionSystem));
    g_next_mission_id = 1;
    srand((unsigned int)time(NULL));
}

void MissionUpdate(float dt) {
    if (g_mission_system.active_mission_id <= 0) return;

    Mission* m = MissionGet(g_mission_system.active_mission_id);
    if (!m || m->status != MISSION_STATUS_ACTIVE) return;

    g_mission_system.global_timer += dt;

    switch (m->type) {
        case MISSION_SURVIVE:
        case MISSION_DELIVER:
        case MISSION_ASSASSINATE:
        case MISSION_ESCAPE:
        case MISSION_BOSS:
            m->timer -= dt;
            if (m->timer <= 0.0f) {
                MissionFail(m->id);
            }
            break;
        case MISSION_TERRITORY:
            if (m->current_count >= m->required_count) {
                m->timer += dt;
                if (m->timer >= 60.0f) {
                    MissionComplete(m->id);
                }
            } else {
                m->timer = 0.0f;
            }
            break;
        default:
            break;
    }
}

int MissionStart(MissionType type) {
    if (g_mission_system.mission_count >= MAX_MISSIONS) return -1;
    if (type == MISSION_NONE) return -1;

    float diff_scale = CalculateDifficultyScale();

    Mission* m = &g_mission_system.missions[g_mission_system.mission_count];
    memset(m, 0, sizeof(Mission));
    m->id = g_next_mission_id++;
    m->type = type;
    m->status = MISSION_STATUS_ACTIVE;
    m->tier = 1;
    m->difficulty_scale = diff_scale;

    switch (type) {
        case MISSION_SURVIVE:
            GenerateSurviveMission(m, diff_scale);
            break;
        case MISSION_DELIVER:
            GenerateDeliverMission(m, diff_scale);
            break;
        case MISSION_ASSASSINATE:
            GenerateAssassinateMission(m, diff_scale);
            break;
        case MISSION_TERRITORY:
            GenerateTerritoryMission(m, diff_scale);
            break;
        case MISSION_ESCAPE:
            GenerateEscapeMission(m, diff_scale);
            break;
        case MISSION_BOSS:
            GenerateBossMission(m, diff_scale);
            break;
        default:
            return -1;
    }

    g_mission_system.active_mission_id = m->id;
    g_mission_system.mission_count++;

    return m->id;
}

void MissionComplete(int id) {
    Mission* m = MissionGet(id);
    if (!m || m->status != MISSION_STATUS_ACTIVE) return;

    m->status = MISSION_STATUS_COMPLETED;
    g_mission_system.active_mission_id = -1;
}

void MissionFail(int id) {
    Mission* m = MissionGet(id);
    if (!m || m->status != MISSION_STATUS_ACTIVE) return;

    m->status = MISSION_STATUS_FAILED;
    g_mission_system.active_mission_id = -1;
}

Mission* MissionGet(int id) {
    for (int i = 0; i < g_mission_system.mission_count; i++) {
        if (g_mission_system.missions[i].id == id) {
            return &g_mission_system.missions[i];
        }
    }
    return NULL;
}

Mission* MissionGetActive(void) {
    return MissionGet(g_mission_system.active_mission_id);
}

bool MissionIsActive(int id) {
    Mission* m = MissionGet(id);
    return m && m->status == MISSION_STATUS_ACTIVE;
}

int MissionGetCount(void) {
    return g_mission_system.mission_count;
}