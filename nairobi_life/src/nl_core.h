/* nl_core.h - Nairobi Life: shared types and module contracts.
 *
 * A realistic survival life-sim set in Nairobi, Kenya.
 * All simulation values are grounded in real-world data:
 *   - Climate: Dagoretti Corner / JKIA station climate normals (1991-2020)
 *   - Sunrise/sunset: latitude -1.286389, longitude 36.817223 (near equator)
 *   - Economy: Kenyan Shilling (KSh) street prices
 *
 * Units used throughout:
 *   temperature  : degrees Celsius
 *   rainfall     : millimetres per hour
 *   wind         : metres per second
 *   money        : Kenyan Shillings (KSh), integer
 *   distance     : metres (world space); 1 world unit == 1 metre
 *   mass         : kilograms
 *   time         : simulation seconds unless noted
 */
#ifndef NL_CORE_H
#define NL_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ======================================================================== */
/*  Build configuration                                                     */
/* ======================================================================== */

#define NL_SCREEN_W          1600
#define NL_SCREEN_H          900
#define NL_TARGET_FPS        144      /* high-end laptop target */

/* World is 4 km x 4 km of Nairobi, 1 unit = 1 metre. */
#define NL_WORLD_W           4000.0f
#define NL_WORLD_H           4000.0f

#define NL_MAX_NPCS          512
#define NL_MAX_BUILDINGS     600
#define NL_MAX_PROPS         2400
#define NL_MAX_VEHICLES      160
#define NL_MAX_JOBS          64
#define NL_MAX_PUDDLES       512
#define NL_MAX_RAINDROPS     6000
#define NL_MAX_LOG_LINES     64
#define NL_LOG_LINE_LEN      128

/* Simulation clock: how many in-game seconds pass per real second.
 * 60.0 => one in-game minute per real second => 24 min per in-game day. */
#define NL_TIME_SCALE_DEFAULT 60.0f

/* ======================================================================== */
/*  Math helpers (avoid depending on raylib in simulation modules)          */
/* ======================================================================== */

typedef struct { float x, y; } NLVec2;

/* ======================================================================== */
/*  Calendar / clock                                                        */
/* ======================================================================== */

typedef struct {
    int   year;        /* e.g. 2026 */
    int   month;       /* 1..12 */
    int   day;         /* 1..31 */
    int   weekday;     /* 0=Sunday .. 6=Saturday */
    float hour;        /* 0.0 .. 24.0 fractional, EAT (UTC+3) */
    int   day_index;   /* days elapsed since game start */
    float sunrise;     /* fractional hour, local */
    float sunset;      /* fractional hour, local */
    bool  is_daylight;
} NLClock;

/* ======================================================================== */
/*  Weather                                                                 */
/* ======================================================================== */

/* Nairobi's real bimodal rainfall regime. */
typedef enum {
    NL_SEASON_SHORT_DRY = 0,   /* Jan - Feb : hot, dry, dusty         */
    NL_SEASON_LONG_RAINS,      /* Mar - May : heaviest rainfall       */
    NL_SEASON_COOL_DRY,        /* Jun - Sep : cold, overcast, drizzle */
    NL_SEASON_SHORT_RAINS,     /* Oct - Dec : afternoon thunderstorms */
    NL_SEASON_COUNT
} NLSeason;

typedef enum {
    NL_SKY_CLEAR = 0,
    NL_SKY_PARTLY_CLOUDY,
    NL_SKY_OVERCAST,
    NL_SKY_DRIZZLE,
    NL_SKY_RAIN,
    NL_SKY_HEAVY_RAIN,
    NL_SKY_THUNDERSTORM,
    NL_SKY_MIST,
    NL_SKY_COUNT
} NLSkyState;

typedef struct {
    NLSeason   season;
    NLSkyState sky;

    float temperature_c;      /* dry-bulb air temperature            */
    float apparent_c;         /* feels-like, wind chill + humidity   */
    float humidity;           /* 0..1 relative humidity              */
    float cloud_cover;        /* 0..1                                */
    float rain_mm_hr;         /* instantaneous rainfall intensity    */
    float rain_accum_mm;      /* accumulated today                   */
    float wind_speed_ms;      /* metres/second                       */
    float wind_dir_deg;       /* meteorological, 0 = from north      */
    float visibility_m;       /* metres                              */
    float pressure_hpa;       /* station pressure at 1795 m altitude */
    float aqi_pm25;           /* PM2.5 ug/m3 - Nairobi air quality   */

    float ground_wetness;     /* 0..1, how soaked the ground is      */
    float mud_factor;         /* 0..1, movement penalty on dirt      */
    float lightning_flash;    /* 0..1, decays after a strike         */
    float thunder_delay;      /* seconds until thunder audio cue     */

    /* Derived light for rendering. */
    float ambient_r, ambient_g, ambient_b;
    float ambient_intensity;  /* 0..1 */
} NLWeather;

/* ======================================================================== */
/*  Player                                                                  */
/* ======================================================================== */

typedef enum {
    NL_ILL_NONE = 0,
    NL_ILL_COLD,          /* from being rained on / cold nights */
    NL_ILL_FOOD_POISON,   /* cheap street food gone bad         */
    NL_ILL_MALARIA,       /* mosquitoes near stagnant water     */
    NL_ILL_CHOLERA,       /* dirty water in the slum            */
    NL_ILL_COUNT
} NLIllness;

typedef struct {
    NLVec2 pos;
    NLVec2 vel;
    float  facing;          /* radians */

    /* Vital needs, all 0..100 where 100 is fully satisfied. */
    float health;
    float energy;           /* stamina for working / running */
    float hunger;           /* 100 = full  */
    float thirst;           /* 100 = hydrated */
    float hygiene;          /* affects job access, harassment */
    float morale;           /* affects earning rate, risk taking */
    float warmth;           /* body temperature comfort */
    float bladder;

    /* Money in Kenyan Shillings. */
    int   cash;             /* physical notes/coins on hand   */
    int   mpesa;            /* mobile money balance           */
    int   debt;             /* owed to landlord / shylock     */

    /* Social / legal standing. */
    float reputation;       /* 0..100 street cred */
    float police_heat;      /* 0..100 chance of harassment */
    int   arrests;
    int   days_survived;

    /* Health conditions. */
    NLIllness illness;
    float illness_severity; /* 0..1 */
    float wetness;          /* 0..1 how soaked the player is */
    float injury;           /* 0..1 physical injury level */

    /* Carried goods for hawking. */
    int   stock_water;      /* bottles of water to resell */
    int   stock_sweets;     /* mints/sweets to resell     */
    int   stock_scrap_kg;   /* collected scrap metal      */

    bool  has_shelter_tonight;
    bool  is_sprinting;
    bool  is_working;
    bool  under_roof;

    float sleep_debt;       /* hours of missed sleep */
    float last_meal_hour;
} NLPlayer;

/* ======================================================================== */
/*  World geometry                                                          */
/* ======================================================================== */

typedef enum {
    NL_DIST_KIBERA = 0,     /* informal settlement, iron sheet housing */
    NL_DIST_CBD,            /* central business district, high-rise    */
    NL_DIST_INDUSTRIAL,     /* Industrial Area, warehouses             */
    NL_DIST_ESTATE,         /* middle-income estate housing            */
    NL_DIST_MARKET,         /* open air market                         */
    NL_DIST_ROADSIDE,       /* highway verge                           */
    NL_DIST_COUNT
} NLDistrict;

typedef enum {
    NL_BLD_SHACK = 0,       /* mabati (iron sheet) shack */
    NL_BLD_TOWER,           /* CBD office tower          */
    NL_BLD_SHOP,            /* duka / small shop         */
    NL_BLD_KIOSK,           /* kibanda food stall        */
    NL_BLD_WAREHOUSE,
    NL_BLD_APARTMENT,
    NL_BLD_CHURCH,
    NL_BLD_POLICE_POST,
    NL_BLD_CLINIC,
    NL_BLD_WATER_POINT,
    NL_BLD_COUNT
} NLBuildingKind;

typedef struct {
    NLVec2         pos;      /* top-left corner, metres */
    NLVec2         size;
    NLBuildingKind kind;
    NLDistrict     district;
    uint8_t        r, g, b;
    int            floors;
    bool           enterable;
    bool           lit_at_night;
    float          roof_pitch;  /* affects rain runoff visuals */
} NLBuilding;

typedef enum {
    NL_PROP_TRASH = 0,
    NL_PROP_TYRE,
    NL_PROP_CRATE,
    NL_PROP_BARREL,
    NL_PROP_STREETLIGHT,
    NL_PROP_POLE,
    NL_PROP_TREE,
    NL_PROP_SIGNBOARD,
    NL_PROP_DRAIN,
    NL_PROP_LAUNDRY_LINE,
    NL_PROP_COUNT
} NLPropKind;

typedef struct {
    NLVec2     pos;
    NLPropKind kind;
    float      scale;
    float      rot;
    uint8_t    r, g, b;
} NLProp;

typedef enum {
    NL_SURF_TARMAC = 0,     /* paved road, drains well            */
    NL_SURF_DIRT,           /* murram - becomes mud when wet      */
    NL_SURF_CONCRETE,
    NL_SURF_GRASS,
    NL_SURF_WATER,          /* open drain / stream                */
    NL_SURF_COUNT
} NLSurface;

typedef struct {
    NLVec2 a, b;
    float  width;
    bool   paved;
    int    traffic_density;   /* 0..10 */
} NLRoad;

typedef struct {
    NLVec2 pos;
    float  radius;
    float  depth;      /* 0..1, grows with rain, evaporates after */
} NLPuddle;

/* ======================================================================== */
/*  NPCs                                                                    */
/* ======================================================================== */

typedef enum {
    NL_NPC_PEDESTRIAN = 0,  /* ordinary Nairobian going to work */
    NL_NPC_HAWKER,          /* competing street vendor          */
    NL_NPC_SHOPKEEPER,
    NL_NPC_MATATU_TOUT,     /* makanga calling for passengers   */
    NL_NPC_POLICE,          /* regular police                   */
    NL_NPC_ASKARI,          /* county askari, confiscates goods */
    NL_NPC_THUG,            /* mugger                           */
    NL_NPC_STREET_KID,
    NL_NPC_OFFICE_WORKER,   /* good customer                    */
    NL_NPC_BODA_RIDER,
    NL_NPC_COUNT
} NLNpcKind;

typedef enum {
    NL_AI_IDLE = 0,
    NL_AI_WALKING,
    NL_AI_WORKING,
    NL_AI_TALKING,
    NL_AI_BUYING,
    NL_AI_CHASING,
    NL_AI_FLEEING,
    NL_AI_SHELTERING,   /* took cover from rain */
    NL_AI_HARASSING,
    NL_AI_COUNT
} NLAiState;

typedef struct {
    NLVec2    pos;
    NLVec2    vel;
    NLVec2    target;
    NLNpcKind kind;
    NLAiState state;

    float  speed;
    float  state_timer;
    float  patience;
    float  wealth_level;    /* 0..1, how likely to buy from you */
    float  aggression;      /* 0..1 */
    float  facing;
    float  bob_phase;       /* walk animation phase */

    int    health;
    bool   active;
    bool   has_umbrella;
    bool   knows_player;
    uint8_t skin_r, skin_g, skin_b;
    uint8_t cloth_r, cloth_g, cloth_b;
    char   name[24];
} NLNpc;

/* ======================================================================== */
/*  Vehicles                                                                */
/* ======================================================================== */

typedef enum {
    NL_VEH_MATATU = 0,      /* 14-seater nissan / 33-seater */
    NL_VEH_BODA,            /* motorcycle taxi              */
    NL_VEH_CAR,
    NL_VEH_LORRY,
    NL_VEH_HANDCART,        /* mkokoteni                    */
    NL_VEH_COUNT
} NLVehicleKind;

typedef struct {
    NLVec2        pos;
    NLVec2        vel;
    float         angle;
    float         speed;
    NLVehicleKind kind;
    int           road_index;
    float         road_t;      /* 0..1 progress along road */
    int           direction;   /* +1 or -1 */
    bool          active;
    bool          headlights;
    bool          honking;
    float         honk_timer;
    uint8_t       r, g, b;
} NLVehicle;

/* ======================================================================== */
/*  Economy & jobs                                                          */
/* ======================================================================== */

typedef enum {
    NL_JOB_NONE = 0,
    NL_JOB_HAWK_WATER,      /* sell bottled water in traffic     */
    NL_JOB_HAWK_SWEETS,
    NL_JOB_COLLECT_SCRAP,   /* scrap metal / plastic collection  */
    NL_JOB_MKOKOTENI,       /* pushing a handcart for hire       */
    NL_JOB_CAR_WASH,
    NL_JOB_PARKING_GUIDE,   /* directing parking for tips        */
    NL_JOB_LOADING,         /* casual loading at Industrial Area */
    NL_JOB_CONSTRUCTION,    /* mjengo casual labour              */
    NL_JOB_BEG,
    NL_JOB_COUNT
} NLJobKind;

typedef struct {
    NLJobKind  kind;
    NLVec2     pos;
    float      radius;
    int        pay_min;        /* KSh */
    int        pay_max;
    float      energy_cost;    /* per in-game hour */
    float      duration_hours;
    float      rain_penalty;   /* 0..1 earnings multiplier loss in rain */
    float      heat_risk;      /* police attention gained */
    bool       available;
    bool       requires_stock;
    char       name[40];
    char       desc[96];
} NLJob;

/* Prices in KSh, realistic Nairobi street prices. */
typedef struct {
    int water_bottle_buy;    /* wholesale 500ml */
    int water_bottle_sell;
    int sweets_pack_buy;
    int sweets_sell_each;
    int scrap_per_kg;
    int ugali_beans_meal;    /* kibandaski meal */
    int chapati;
    int mandazi;
    int tea;
    int matatu_fare;         /* varies with rain & rush hour */
    int boda_fare;
    int shack_rent_daily;
    int hostel_bed;
    int public_toilet;
    int clinic_visit;
    int malaria_treatment;
    int phone_charge;
    int water_jerrycan;      /* 20L from a water point */
} NLPrices;

/* ======================================================================== */
/*  Events / notifications                                                  */
/* ======================================================================== */

typedef enum {
    NL_MSG_INFO = 0,
    NL_MSG_GOOD,
    NL_MSG_WARN,
    NL_MSG_BAD,
    NL_MSG_MONEY
} NLMsgKind;

typedef struct {
    char      text[NL_LOG_LINE_LEN];
    NLMsgKind kind;
    float     age;       /* seconds since posted */
    float     ttl;
} NLLogLine;

typedef struct {
    NLLogLine lines[NL_MAX_LOG_LINES];
    int       head;
    int       count;
} NLLog;

/* ======================================================================== */
/*  Interaction prompt                                                      */
/* ======================================================================== */

typedef enum {
    NL_INT_NONE = 0,
    NL_INT_JOB,
    NL_INT_BUY_FOOD,
    NL_INT_BUY_WATER,
    NL_INT_BUY_STOCK,
    NL_INT_SLEEP,
    NL_INT_CLINIC,
    NL_INT_TOILET,
    NL_INT_SHELTER,
    NL_INT_TALK
} NLInteractKind;

typedef struct {
    NLInteractKind kind;
    int            target_index;
    char           label[96];
    bool           available;
} NLInteraction;

/* ======================================================================== */
/*  Game state                                                              */
/* ======================================================================== */

typedef enum {
    NL_SCREEN_TITLE = 0,
    NL_SCREEN_PLAYING,
    NL_SCREEN_PAUSED,
    NL_SCREEN_SLEEPING,
    NL_SCREEN_DAY_SUMMARY,
    NL_SCREEN_GAMEOVER
} NLScreen;

typedef struct {
    NLScreen   screen;
    NLClock    clock;
    NLWeather  weather;
    NLPlayer   player;
    NLPrices   prices;
    NLLog      log;
    NLInteraction interact;

    NLBuilding buildings[NL_MAX_BUILDINGS];
    int        building_count;
    NLProp     props[NL_MAX_PROPS];
    int        prop_count;
    NLRoad     roads[128];
    int        road_count;
    NLPuddle   puddles[NL_MAX_PUDDLES];
    int        puddle_count;
    NLNpc      npcs[NL_MAX_NPCS];
    int        npc_count;
    NLVehicle  vehicles[NL_MAX_VEHICLES];
    int        vehicle_count;
    NLJob      jobs[NL_MAX_JOBS];
    int        job_count;

    int        active_job;        /* index into jobs, -1 if none */
    float      job_progress;      /* 0..1 */
    float      job_elapsed_hours;

    NLVec2     camera;
    float      camera_zoom;
    float      time_scale;
    uint32_t   rng_state;

    /* Day statistics for the end-of-day summary. */
    int        day_earnings;
    int        day_spending;
    int        day_start_cash;
    float      day_worked_hours;

    bool       show_debug;
    bool       show_help;
    double     real_seconds;
    int        frame_counter;
} NLGame;

/* ======================================================================== */
/*  Module API                                                              */
/* ======================================================================== */

/* --- nl_util.c ---------------------------------------------------------- */
uint32_t nl_rand(uint32_t *state);
float    nl_randf(uint32_t *state);                 /* 0..1 */
float    nl_randf_range(uint32_t *state, float a, float b);
int      nl_rand_range(uint32_t *state, int a, int b); /* inclusive */
float    nl_clampf(float v, float lo, float hi);
float    nl_lerpf(float a, float b, float t);
float    nl_smoothstep(float e0, float e1, float x);
float    nl_vec_dist(NLVec2 a, NLVec2 b);
NLVec2   nl_vec_norm(NLVec2 v);
float    nl_noise1(float x, uint32_t seed);         /* value noise */
float    nl_fbm1(float x, uint32_t seed, int octaves);

void     nl_log_init(NLLog *log);
void     nl_log_push(NLLog *log, NLMsgKind kind, const char *fmt, ...);
void     nl_log_update(NLLog *log, float dt);

/* --- nl_time.c ---------------------------------------------------------- */
void  nl_clock_init(NLClock *c, int year, int month, int day, float hour);
void  nl_clock_update(NLClock *c, float sim_seconds);
void  nl_clock_advance_hours(NLClock *c, float hours);
const char *nl_month_name(int month);
const char *nl_weekday_name(int weekday);
void  nl_clock_format(const NLClock *c, char *out, size_t n);
bool  nl_clock_is_rush_hour(const NLClock *c);
bool  nl_clock_is_weekend(const NLClock *c);

/* --- nl_weather.c ------------------------------------------------------- */
void  nl_weather_init(NLWeather *w, const NLClock *c, uint32_t seed);
void  nl_weather_update(NLWeather *w, const NLClock *c, float sim_seconds,
                        uint32_t *rng);
NLSeason nl_season_for_month(int month);
const char *nl_season_name(NLSeason s);
const char *nl_sky_name(NLSkyState s);
float nl_weather_move_multiplier(const NLWeather *w, NLSurface surf);
float nl_weather_earning_multiplier(const NLWeather *w, NLJobKind job);

/* --- nl_world.c --------------------------------------------------------- */
void      nl_world_generate(NLGame *g, uint32_t seed);
NLSurface nl_world_surface_at(const NLGame *g, NLVec2 p);
NLDistrict nl_world_district_at(const NLGame *g, NLVec2 p);
bool      nl_world_blocked(const NLGame *g, NLVec2 p, float radius);
bool      nl_world_under_roof(const NLGame *g, NLVec2 p);
void      nl_world_update(NLGame *g, float dt, float sim_seconds);
const char *nl_district_name(NLDistrict d);
const char *nl_building_name(NLBuildingKind k);

/* --- nl_npc.c ----------------------------------------------------------- */
void nl_npc_spawn_all(NLGame *g);
void nl_npc_update(NLGame *g, float dt, float sim_seconds);
const char *nl_npc_kind_name(NLNpcKind k);
int  nl_npc_nearest(const NLGame *g, NLVec2 p, float max_dist);

/* --- nl_vehicle.c (inside nl_npc.c) ------------------------------------- */
void nl_vehicle_spawn_all(NLGame *g);
void nl_vehicle_update(NLGame *g, float dt, float sim_seconds);

/* --- nl_econ.c ---------------------------------------------------------- */
void nl_econ_init(NLGame *g);
void nl_econ_update(NLGame *g, float dt, float sim_seconds);
void nl_jobs_generate(NLGame *g);
bool nl_job_start(NLGame *g, int job_index);
void nl_job_cancel(NLGame *g, bool quiet);
int  nl_job_payout(NLGame *g, const NLJob *job);
void nl_player_needs_update(NLGame *g, float sim_seconds);
void nl_player_spend(NLGame *g, int amount, const char *what);
void nl_player_earn(NLGame *g, int amount, const char *what);
bool nl_player_eat(NLGame *g, int meal_index);
void nl_player_sleep(NLGame *g, float hours, bool sheltered);
void nl_player_new_day(NLGame *g);
const char *nl_job_kind_name(NLJobKind k);
const char *nl_illness_name(NLIllness i);

/* --- nl_render.c -------------------------------------------------------- */
void nl_render_init(void);
void nl_render_shutdown(void);
void nl_render_frame(NLGame *g);
void nl_render_rain_update(NLGame *g, float dt);

/* --- nl_game.c ---------------------------------------------------------- */
void nl_game_init(NLGame *g, uint32_t seed);
void nl_game_update(NLGame *g, float real_dt);
void nl_game_input(NLGame *g, float real_dt);

#endif /* NL_CORE_H */
