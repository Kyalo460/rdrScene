#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "raylib.h"
#include "raymath.h"

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define WORLD_WIDTH 4000
#define WORLD_HEIGHT 4000
#define MAX_ENTITIES 1024
#define MAX_BULLETS 512
#define MAX_PARTICLES 1024
#define PI 3.14159265358979323846f
#define MAX_BUILDINGS 500
#define MAX_PROPS 2000
#define MAX_NAV_NODES 2000
#define MAX_MISSIONS 32
#define MAX_MISSION_TITLE 64
#define MAX_MISSION_DESC 256
#define MAX_SOUNDS 10
#define MAX_MUSIC 5
#define ROAD_GRID_SIZE 400
#define MAX_TEMPLATES_PER_SITUATION 16
#define MAX_SLANG_ENTRIES 32
#define MAX_DIALOGUE_LENGTH 512

typedef enum {
    FACTION_PLAYER = 0,
    FACTION_CIVILIAN,
    FACTION_GANG_MUNGIKI,
    FACTION_GANG_TALIBAN,
    FACTION_GANG_JERUSALEM,
    FACTION_POLICE,
    FACTION_COUNT
} Faction;

typedef enum {
    ENTITY_NONE = 0,
    ENTITY_PLAYER,
    ENTITY_CIVILIAN,
    ENTITY_GANG_MUNGIKI,
    ENTITY_GANG_TALIBAN,
    ENTITY_GANG_JERUSALEM,
    ENTITY_POLICE,
    ENTITY_BOSS,
    ENTITY_BULLET,
    ENTITY_MELEE,
    ENTITY_MOLOTOV,
    ENTITY_EXPLOSION,
    ENTITY_PICKUP
} EntityType;

typedef enum {
    WEAPON_NONE = -1,
    WEAPON_FISTS = 0,
    WEAPON_KNIFE,
    WEAPON_PISTOL,
    WEAPON_SHOTGUN,
    WEAPON_AK47,
    WEAPON_SNIPER,
    WEAPON_RPG,
    WEAPON_GRENADE,
    WEAPON_MOLOTOV,
    WEAPON_COUNT
} WeaponType;

typedef enum {
    PICKUP_HEALTH,
    PICKUP_AMMO,
    PICKUP_ARMOR,
    PICKUP_CASH,
    PICKUP_WEAPON
} PickupType;

typedef enum {
    BUILDING_SLUM = 0,
    BUILDING_ESTATE,
    BUILDING_CBD
} BuildingType;

typedef enum {
    PROP_MATATU = 0,
    PROP_KIOSK,
    PROP_BARRIER,
    PROP_STREETLIGHT,
    PROP_TRASH
} PropType;

typedef enum {
    MISSION_NONE = 0,
    MISSION_SURVIVE,
    MISSION_DELIVER,
    MISSION_ASSASSINATE,
    MISSION_TERRITORY,
    MISSION_ESCAPE,
    MISSION_BOSS
} MissionType;

typedef enum {
    MISSION_STATUS_INACTIVE = 0,
    MISSION_STATUS_ACTIVE,
    MISSION_STATUS_COMPLETED,
    MISSION_STATUS_FAILED
} MissionStatus;

typedef enum {
    DIALOGUE_IDLE = 0,
    DIALOGUE_SPOT_PLAYER,
    DIALOGUE_ENGAGE,
    DIALOGUE_TAUNT,
    DIALOGUE_FLEE,
    DIALOGUE_DYING,
    DIALOGUE_KILL_PLAYER,
    DIALOGUE_CALL_BACKUP,
    DIALOGUE_WIN_FIGHT,
    DIALOGUE_LOSE_FIGHT,
    DIALOGUE_GREETING,
    DIALOGUE_THREATEN,
    DIALOGUE_EXTORT,
    DIALOGUE_SITUATION_COUNT
} DialogueSituation;

typedef enum {
    RELATION_NEUTRAL = 0,
    RELATION_HOSTILE,
    RELATION_FEARFUL,
    RELATION_RESPECTFUL,
    RELATION_COUNT
} DialogueRelation;

typedef enum {
    TIME_DAY = 0,
    TIME_DUSK,
    TIME_NIGHT,
    TIME_DAWN,
    TIME_COUNT
} TimeOfDay;

typedef struct {
    float x, y;
} Vec2;

typedef struct Entity {
    int id;
    EntityType type;
    Faction faction;
    float x, y;
    float width, height;
    float angle;
    float speed;
    int health;
    int max_health;
    float stamina;
    float max_stamina;
    int armor;
    int max_armor;
    WeaponType weapon;
    float weapon_cooldown;
    float dodge_cooldown;
    bool is_sprinting;
    bool is_dodging;
    float dodge_timer;
    float dodge_dir_x, dodge_dir_y;
    int damage;
    struct Entity* owner;
    float lifetime;
    float vel_x, vel_y;
    int sprite_index;
    bool active;
    int heat;
    int reputation;
    int territory_id;
    float ai_timer;
    float state_timer;
    int current_target_id;
    float patrol_points[4][2];
    int patrol_count;
    int patrol_index;
    PickupType pickup_type;
    int pickup_value;
} Entity;

typedef struct {
    bool keys[512];
    bool keys_pressed[512];
    bool keys_released[512];
    bool mouse_buttons[3];
    bool mouse_buttons_pressed[3];
    bool mouse_buttons_released[3];
    int mouse_x, mouse_y;
    int mouse_wheel;
} Input;

typedef struct {
    Entity entities[MAX_ENTITIES];
    int entity_count;
    Entity* player;
    float camera_x, camera_y;
    float world_width, world_height;
    int screen_width, screen_height;
    Input input;
    float dt;
    int cash;
    int respect;
    int heat_level;
    int current_weapon;
    int ammo;
    int max_ammo;
    int kills;
    int time_survived;
    bool game_over;
    bool paused;
    int current_mission;
    bool show_weapon_wheel;
    bool show_mission_log;
    float game_time;
    int wave;
    int enemies_killed_this_wave;
    int worldHeat;
    Texture2D textures[16];
    int texture_count;
} Game;

typedef struct {
    Vector2 pos;
    Vector2 size;
    BuildingType type;
    uint32_t color;
} Building;

typedef struct {
    Vector2 pos;
    PropType type;
    uint32_t color;
} Prop;

typedef struct {
    Vector2 pos;
    bool walkable;
    int connections[4];
    int connection_count;
} NavNode;

typedef struct {
    int x, y;
    bool horizontal;
} RoadSegment;

typedef struct {
    int id;
    MissionType type;
    MissionStatus status;
    char title[MAX_MISSION_TITLE];
    char description[MAX_MISSION_DESC];
    Vec2 target_pos;
    int target_entity_id;
    int required_count;
    int current_count;
    float time_limit;
    float timer;
    int cash_reward;
    int rep_reward;
    int tier;
    float difficulty_scale;
} Mission;

typedef struct {
    Mission missions[MAX_MISSIONS];
    int active_mission_id;
    int mission_count;
    float global_timer;
} MissionSystem;

typedef struct {
    DialogueSituation situation;
    Faction faction;
    int playerHeat;
    int playerRespect;
    WeaponType playerWeapon;
    int speakerHealth;
    int speakerMaxHealth;
    float distance;
    TimeOfDay timeOfDay;
    DialogueRelation relation;
} DialogueContext;

typedef struct {
    float health;
    float maxHealth;
    float stamina;
    float maxStamina;
    float armor;
    float maxArmor;
    int cash;
    int respect;
    int heatLevel;
    int currentWeapon;
    int ammo;
    int maxAmmo;
    Vector2 position;
} PlayerHUD;

typedef struct {
    char title[64];
    char description[256];
    char objectives[8][128];
    int objectiveCount;
    bool objectiveComplete[8];
    float timer;
    int rewardCash;
    int rewardRespect;
    bool active;
} MissionData;

typedef struct {
    int weaponCount;
    int weaponTypes[7];
    int selectedWeapon;
    bool visible;
    Vector2 center;
    float radius;
} WeaponWheel;

typedef struct {
    bool show;
    float vignetteAlpha;
    int kills;
    int timeSurvived;
    int cashEarned;
} WastedScreen;

typedef struct {
    bool show;
    char characterName[32];
    char portraitPath[64];
    char dialogue[512];
    int dialogueIndex;
    float typewriterTimer;
    float typewriterSpeed;
    Texture2D portrait;
} DialogueBox;

typedef struct {
    Font font;
    Font fontBold;
    Texture2D hudPanel;
    Texture2D minimapMask;
    Texture2D weaponIcons[7];
    Texture2D missionIcons[4];
    Texture2D heartIcon;
    Texture2D staminaIcon;
    Texture2D armorIcon;
    Texture2D cashIcon;
    Texture2D respectIcon;
    Texture2D heatIcons[5];
    Color nairobiGold;
    Color nairobiDark;
    Color nairobiRed;
    Color nairobiBlue;
    Color nairobiYellow;
} UIAssets;

extern Game g_game;
extern Building g_buildings[MAX_BUILDINGS];
extern int g_building_count;
extern Prop g_props[MAX_PROPS];
extern int g_prop_count;
extern NavNode g_nav_nodes[MAX_NAV_NODES];
extern int g_nav_count;
extern RoadSegment g_roads[200];
extern int g_road_count;
extern UIAssets uiAssets;

void GameInit(Game* game);
void GameUpdate(Game* game);
void GameDraw(Game* game);
void GameClose(Game* game);
void GameHandleInput(Game* game);
void GameSpawnPlayer(Game* game);
Entity* EntityCreate(Game* game, EntityType type, float x, float y, float angle);
void EntityDestroy(Game* game, Entity* e);
void EntityUpdate(Game* game, Entity* e, float dt);
void EntityDraw(Game* game, Entity* e);
bool EntityCheckCollision(Entity* a, Entity* b);
float EntityDistance(Entity* a, Entity* b);
void EntityTakeDamage(Game* game, Entity* target, int damage, Entity* attacker);
Entity* EntityFindNearest(Game* game, Entity* from, EntityType target_type, float max_range);
void WorldWrapEntity(Entity* e);
void CameraFollowPlayer(Game* game);

void PlayerInit(Game* game);
void PlayerUpdate(Game* game, float dt);
void PlayerShoot(Game* game);
Entity* EntityCreateBullet(Game* game, float x, float y, float dir_x, float dir_y, int damage, float speed, Entity* owner);

void WorldInit(uint32_t seed);
void WorldUpdate(float dt);
void WorldDraw(void);
Vector2 WorldGetSpawnPoint(Faction faction);
bool WorldIsWalkable(Vector2 pos);

void EnemyInit(Game* game);
void EnemyUpdate(Game* game, float dt);
void EnemyAI(Game* game, Entity *e, float dt);
void EnemySpawnWave(Game* game, int wave);
Entity* EntityCreateEnemy(Game* game, Faction faction, float x, float y);
void EntityMoveToward(Entity *e, float targetX, float targetY, float speed, float dt);
void EntityAttack(Entity *attacker, Entity *target);
int EntityFindNearestTarget(Game* game, Entity* e, Faction targetFaction, float maxRange);

void MissionInit(void);
void MissionUpdate(float dt);
int MissionStart(MissionType type);
void MissionComplete(int id);
void MissionFail(int id);
Mission* MissionGet(int id);
Mission* MissionGetActive(void);
bool MissionIsActive(int id);
int MissionGetCount(void);

void AudioInit(void);
void AudioPlaySound(int id, float pitch);
void AudioPlayMusic(int id);
void AudioUpdate(void);
void AudioStopMusic(void);
void AudioClose(void);

void DialogueInit(void);
void DialogueClose(void);
char* DialogueGenerate(Entity *speaker, Entity *target, DialogueContext ctx);
const char* DialogueGetFactionName(Faction faction);
const char* DialogueGetWeaponName(WeaponType weapon);
const char* DialogueGetLocationName(float x, float y);
const char* DialogueGetRandomSlang(void);
TimeOfDay DialogueGetTimeOfDay(float hour);
DialogueRelation DialogueGetRelation(Entity *speaker, Entity *target, int playerRespect, int playerHeat);

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