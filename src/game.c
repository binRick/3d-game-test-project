// IRON FIST 3D - raylib native FPS
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// Resource layout differs between platforms:
//   macOS  — resources live at <app>/Contents/Resources/ (one dir up from MacOS/)
//   Windows — all assets are baked into the exe as a Win32 RCDATA resource
//             ("bundle"), extracted to %TEMP%/IronFist3D/ at startup
//   Linux — resources sit next to the exe in flat sprites/ and sounds/
#if defined(__APPLE__)
  #define RES_PREFIX "../Resources/"
#else
  #define RES_PREFIX ""
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
// Forward-declare the bits of Win32 we need. Avoid <windows.h> because its
// macros shadow raylib symbols (DrawText, CloseWindow, Rectangle, ...).
typedef void *HMODULE;
typedef void *HRSRC;
typedef void *HGLOBAL;
typedef unsigned long DWORD;
__declspec(dllimport) HMODULE __stdcall GetModuleHandleA(const char *);
__declspec(dllimport) HRSRC   __stdcall FindResourceA(HMODULE, const char *, const char *);
__declspec(dllimport) HGLOBAL __stdcall LoadResource(HMODULE, HRSRC);
__declspec(dllimport) void *  __stdcall LockResource(HGLOBAL);
__declspec(dllimport) DWORD   __stdcall SizeofResource(HMODULE, HRSRC);
__declspec(dllimport) DWORD   __stdcall GetTempPathA(DWORD, char *);
__declspec(dllimport) int     __stdcall CreateDirectoryA(const char *, void *);
#define RT_RCDATA ((const char *)10)

// Filled in by ExtractBundle() at startup if the exe carries a "bundle"
// RCDATA resource. Otherwise AppDir() falls back to raylib's exe-dir lookup
// so an unbundled dev build can still find loose sprites/ and sounds/.
static char g_appDir[1024] = "";

static void ExtractBundle(void) {
    HMODULE h = GetModuleHandleA(NULL);
    HRSRC hres = FindResourceA(h, "bundle", RT_RCDATA);
    if (!hres) return;
    HGLOBAL hg = LoadResource(h, hres);
    DWORD size = SizeofResource(h, hres);
    unsigned char *data = (unsigned char *)LockResource(hg);
    if (!data || size < 8 || memcmp(data, "IFB1", 4) != 0) return;

    char temp[512];
    GetTempPathA(sizeof(temp), temp);
    snprintf(g_appDir, sizeof(g_appDir), "%sIronFist3D/", temp);
    for (char *p = g_appDir; *p; p++) if (*p == '\\') *p = '/';
    CreateDirectoryA(g_appDir, NULL);

    size_t pos = 4;
    unsigned num;
    memcpy(&num, data + pos, 4); pos += 4;
    for (unsigned i = 0; i < num && pos + 6 <= size; i++) {
        unsigned short pl;
        memcpy(&pl, data + pos, 2); pos += 2;
        if (pos + pl + 4 > size) break;
        char path[512];
        if (pl >= sizeof(path)) break;
        memcpy(path, data + pos, pl); path[pl] = 0; pos += pl;
        unsigned sz;
        memcpy(&sz, data + pos, 4); pos += 4;
        if (pos + sz > size) break;

        char out[1024];
        snprintf(out, sizeof(out), "%s%s", g_appDir, path);
        size_t base = strlen(g_appDir);
        for (char *p = out + base; *p; p++) {
            if (*p == '/') { *p = 0; CreateDirectoryA(out, NULL); *p = '/'; }
        }
        FILE *f = fopen(out, "wb");
        if (f) { fwrite(data + pos, 1, sz, f); fclose(f); }
        pos += sz;
    }
}
#endif

static const char *AppDir(void) {
#ifdef _WIN32
    if (g_appDir[0]) return g_appDir;
#endif
    return GetApplicationDirectory();
}

// ── CONSTANTS ────────────────────────────────────────────────────────────────
#define SW          1280
#define SH          720
#define FPS         144
#define CELL        4.0f
#define WALL_H      5.1f
#define ROWS        20
#define COLS        30
#define MAX_ENEMIES 96
#define MAX_BULLETS 256
#define MAX_PARTS   768
#define MAX_PICKS   48
#define EYE_H       1.65f
#define PRAD        0.32f
#define SPEED       7.5f
#define SENS        0.0016f
#define GRAV       -22.0f
#define JUMP        8.5f
#define STEP_H      0.55f  // max auto-climb height (stairs)
#define MAX_PITCH   1.44f

// ── MAP ──────────────────────────────────────────────────────────────────────
static const int MAP[ROWS][COLS] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,1,0,1,1,0,0,1,0,0,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0,0,1},
    {1,0,0,1,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,0,0,0,1,0,0,1},
    {1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,1,0,1,0,0,0,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,0,0,1,0,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,0,1,0,0,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,0,0,0,1,0,1,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,0,0,0,1,0,0,1},
    {1,0,0,1,1,0,1,1,0,0,1,0,0,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

// ── EMBEDDED SHADERS ─────────────────────────────────────────────────────────
// Desktop: GLSL 330 core. Web (Emscripten / WebGL 2): GLSL ES 300, which is
// source-compatible with 330 apart from the version line and a required
// float precision declaration in the fragment shader.
#if defined(PLATFORM_WEB)
  #define GLSL_VERSION   "#version 300 es\n"
  #define GLSL_PRECISION "precision mediump float;\n"
#else
  #define GLSL_VERSION   "#version 330 core\n"
  #define GLSL_PRECISION ""
#endif
static const char *VS = GLSL_VERSION
"in vec3 vertexPosition;\n"
"in vec2 vertexTexCoord;\n"
"in vec3 vertexNormal;\n"
"in vec4 vertexColor;\n"
"uniform mat4 mvp;\n"
"uniform mat4 matModel;\n"
"uniform mat4 matNormal;\n"
"out vec3 fragPos;\n"
"out vec2 fragUV;\n"
"out vec3 fragNorm;\n"
"void main(){\n"
"  fragPos  = vec3(matModel*vec4(vertexPosition,1.0));\n"
"  fragUV   = vertexTexCoord;\n"
"  fragNorm = normalize(vec3(matNormal*vec4(vertexNormal,1.0)));\n"
"  gl_Position = mvp*vec4(vertexPosition,1.0);\n"
"}\n";

static const char *FS = GLSL_VERSION GLSL_PRECISION
"in vec3 fragPos;\n"
"in vec2 fragUV;\n"
"in vec3 fragNorm;\n"
"uniform sampler2D texture0;\n"
"uniform vec4 colDiffuse;\n"
"uniform vec3 viewPos;\n"
"uniform vec4 ambient;\n"
"struct Light{ int enabled; vec3 pos; vec3 color; float radius; };\n"
"#define NL 8\n"
"uniform Light lights[NL];\n"
"out vec4 outColor;\n"
"void main(){\n"
"  vec4 tex = texture(texture0, fragUV)*colDiffuse;\n"
"  vec3 n   = normalize(fragNorm);\n"
"  vec3 lit = ambient.rgb;\n"
"  for(int i=0;i<NL;i++){\n"
"    if(lights[i].enabled==0) continue;\n"
"    vec3  lv = lights[i].pos - fragPos;\n"
"    float d  = length(lv);\n"
"    if(d>lights[i].radius) continue;\n"
"    float att = 1.0 - d/lights[i].radius;\n"
"    att = att*att;\n"
"    float diff = max(dot(n, normalize(lv)), 0.0);\n"
"    lit += lights[i].color * diff * att * 2.5;\n"
"  }\n"
"  vec3 col = tex.rgb * clamp(lit, 0.0, 1.0);\n"
"  // fog\n"
"  float fd = length(viewPos - fragPos);\n"
"  float ff = clamp(1.0 - (fd-22.0)/58.0, 0.12, 1.0);\n"
"  outColor = vec4(mix(vec3(0.10,0.08,0.14), col, ff), 1.0);\n"
"}\n";

// ── LIGHTING ─────────────────────────────────────────────────────────────────
#define NUM_LIGHTS 8
typedef struct { Vector3 pos; Vector3 color; float radius; int enabled; } LightDef;
static Shader    g_shader;
static LightDef  g_lights[NUM_LIGHTS];

// shader uniform locations
static int u_viewPos, u_ambient;
static int u_lEnabled[NUM_LIGHTS], u_lPos[NUM_LIGHTS];
static int u_lColor[NUM_LIGHTS], u_lRadius[NUM_LIGHTS];

static void ShaderSetLight(int i) {
    SetShaderValue(g_shader, u_lEnabled[i], &g_lights[i].enabled, SHADER_UNIFORM_INT);
    SetShaderValue(g_shader, u_lPos[i],     &g_lights[i].pos,     SHADER_UNIFORM_VEC3);
    SetShaderValue(g_shader, u_lColor[i],   &g_lights[i].color,   SHADER_UNIFORM_VEC3);
    SetShaderValue(g_shader, u_lRadius[i],  &g_lights[i].radius,  SHADER_UNIFORM_FLOAT);
}

static void InitShader(void) {
    g_shader = LoadShaderFromMemory(VS, FS);
    g_shader.locs[SHADER_LOC_MATRIX_MODEL]  = GetShaderLocation(g_shader, "matModel");
    g_shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(g_shader, "matNormal");
    u_viewPos = GetShaderLocation(g_shader, "viewPos");
    u_ambient = GetShaderLocation(g_shader, "ambient");
    char buf[64];
    for (int i = 0; i < NUM_LIGHTS; i++) {
        snprintf(buf, 64, "lights[%d].enabled", i); u_lEnabled[i] = GetShaderLocation(g_shader, buf);
        snprintf(buf, 64, "lights[%d].pos",     i); u_lPos[i]     = GetShaderLocation(g_shader, buf);
        snprintf(buf, 64, "lights[%d].color",   i); u_lColor[i]   = GetShaderLocation(g_shader, buf);
        snprintf(buf, 64, "lights[%d].radius",  i); u_lRadius[i]  = GetShaderLocation(g_shader, buf);
    }

    // Fixed scene lights – industrial/hell palette
    // Mounted just below the ceiling so the visible fixture sits flush
    LightDef scene[6] = {
        {{ 5*CELL, WALL_H-0.15f,  2*CELL}, {1.0f, 0.65f, 0.2f},  40.f, 1},
        {{14*CELL, WALL_H-0.15f,  9*CELL}, {0.2f, 0.5f,  1.0f},  50.f, 1},
        {{24*CELL, WALL_H-0.15f,  5*CELL}, {1.0f, 0.15f, 0.1f},  40.f, 1},
        {{ 5*CELL, WALL_H-0.15f, 16*CELL}, {0.15f,1.0f,  0.3f},  40.f, 1},
        {{24*CELL, WALL_H-0.15f, 16*CELL}, {0.9f, 0.3f,  1.0f},  40.f, 1},
        {{14*CELL, WALL_H-0.15f, 17*CELL}, {1.0f, 0.8f,  0.3f},  40.f, 1},
    };
    for (int i = 0; i < 6; i++) g_lights[i] = scene[i];
    // slots 6-7 reserved (muzzle flash, player damage pulse)
    g_lights[6] = (LightDef){{0,0,0},{0,0,0}, 0.f, 0};
    g_lights[7] = (LightDef){{0,0,0},{0,0,0}, 0.f, 0};
    for (int i = 0; i < NUM_LIGHTS; i++) ShaderSetLight(i);
}

// ── TYPES ────────────────────────────────────────────────────────────────────
typedef enum { GS_MENU, GS_PLAY, GS_DEAD } GameState;
typedef enum { ES_PATROL, ES_CHASE, ES_ATTACK } EnemyState;

typedef struct {
    Vector3 pos; float yaw, pitch, velY;
    bool onGround, dead;
    float hp, maxHp;
    float shootCD, kickAnim, bobT, hurtFlash, shake, switchAnim;
    int   weapon, bullets, shells, rockets, mgAmmo, score, kills;
} Player;

typedef struct {
    Vector3 pos; EnemyState state;
    int type; float hp, maxHp, speed, dmg, rate, cd, stateT;
    Vector3 pd; float legT, flashT, alertR, atkR;
    bool active; int score;
    bool  dying;       // true once HP hits 0 — corpse remains in scene
    float deathT;      // seconds since death started (drives death animation)
    float bleedT;      // bleeding drip timer (wounded enemies leak blood periodically)
} Enemy;

typedef struct { Vector3 pos, vel; float life, dmg; bool active, rocket; } Bullet;
typedef struct { Vector3 pos, vel; float life, maxLife, size; Color col; bool active, grav, stuck; } Part;
typedef struct { Vector3 pos; int type; int variant; bool active; float bobT; } Pickup;

// ── GLOBALS ──────────────────────────────────────────────────────────────────
static Player   g_p;
static float    g_swayX=0, g_swayY=0;   // weapon sway (screen pixels)
static Enemy    g_e[MAX_ENEMIES]; static int g_ec;
static Bullet   g_b[MAX_BULLETS];
static Part     g_pt[MAX_PARTS];
static Pickup   g_pk[MAX_PICKS];  static int g_pkc;
static int      g_wave;
static GameState g_gs;
static char     g_msg[80]; static float g_msgT;
static char     g_hypeMsg[80]; static float g_hypeT; static float g_hypeDur;
static Model    g_wallModel, g_floorModel, g_ceilModel;

// ── PLATFORMS (Q3-style 3D level geometry on top of the 2D floor) ───────────
typedef struct { float x0, z0, x1, z1, top; } Platform;
#define MAX_PLATS 64
static Platform g_plats[MAX_PLATS];
static int      g_platCount = 0;
static Model    g_platModel;     // shared unit cube scaled per-platform, lit via g_shader
// ── Sprite-based weapon viewmodels ───────────────────────────────────────────
// Each weapon has a frame[0]=idle and frame[1..count-1]=fire animation.
// When shootCD>0, we interpolate through fire frames; otherwise show idle.
#define MAX_WFRAMES 16
typedef struct {
    Texture2D frames[MAX_WFRAMES];
    int   count;
    float scale;      // display pixel scale
    int   yAnchor;    // "sh - yAnchor" is bottom of sprite baseline
    float xShift;     // fraction of screen width to offset horizontally (-0.1 = 10% left)
    float yShift;     // fraction of screen height to offset vertically (-0.04 = 4% up)
    Texture2D flash;  // optional muzzle-flash overlay drawn on top while firing (same dst rect)
    bool  hasFlash;
    bool  loaded;
} WepSprite;
static WepSprite g_wep[3];  // one per player weapon (0=shotgun 1=MG 2=rocket)

// Per-weapon crosshair overrides (NULL texture = use default tick-mark crosshair)
static Texture2D g_xhair[3];

// Pickup billboards (NULL texture = use default colored sphere)
// Health has 2 variants (CHIKA/EASTA), others are single textures keyed by pickup type.
static Texture2D g_healthTex[2];
// Ammo pickup textures indexed by Pickup.type:
//   [0]=unused (health), [1]=shells (SBOXA), [2]=rockets (MNRBB), [3]=pistol (MCLPA), [4]=MG (MCLPB)
static Texture2D g_ammoTex[5];

// Chef walk-cycle billboards (AFABA/B/C/D — boss sprite frames). NULL = fall back to cube mesh.
static Texture2D g_chefTex[4];
static bool      g_chefOK = false;
// Chef death animation (AFABG/H/I/J). Plays once, freezes on final frame.
static Texture2D g_chefDeathTex[4];
static bool      g_chefDeathOK = false;
#define CHEF_DEATH_FRAME_TIME 0.18f   // seconds per death frame
// Chef pain frame (AFABF0). Shown while flashT > 0.
static Texture2D g_chefPainTex;
static bool      g_chefPainOK = false;

// Heavy chef (type 1) — TORM* sprites. Same layout as type-0 chef:
//   A-D walk, F pain, G-J death.
static Texture2D g_tormTex[4];
static bool      g_tormOK = false;
static Texture2D g_tormPainTex;
static bool      g_tormPainOK = false;
static Texture2D g_tormDeathTex[4];
static bool      g_tormDeathOK = false;

// Fast chef (type 2) — SCH2* sprites. Same A-D walk / F pain / G-J death layout.
static Texture2D g_schTex[4];
static bool      g_schOK = false;
static Texture2D g_schPainTex;
static bool      g_schPainOK = false;
static Texture2D g_schDeathTex[4];
static bool      g_schDeathOK = false;

// Boss (enemy type 3) — AODE* sprites. Walk A-D, pain F, death G/H/L/M/N.
static Texture2D g_bossTex[4];
static bool      g_bossOK = false;
static Texture2D g_bossPainTex;
static bool      g_bossPainOK = false;
static Texture2D g_bossDeathTex[5];
static bool      g_bossDeathOK = false;
#define BOSS_DEATH_FRAME_TIME 0.22f

// Boss test mode — press B on menu. Spawns only bosses, respawns on kill
// so you can iterate on boss behaviour without clearing chef waves.
static bool      g_bossMode = false;
static bool      g_bossInterlude = false;  // true between waves while a boss is up

static Sound    g_sPistol, g_sShotgun, g_sRocket, g_sExplode;
static Sound    g_sHurt, g_sPickup, g_sEmpty, g_sDie;
static Sound    g_sHealthPickup;       // override g_sPickup specifically for health grabs
static bool     g_sHealthPickupOK = false;
static Sound    g_sMGPickup;           // override g_sPickup for MG-ammo grabs
static bool     g_sMGPickupOK = false;
static Sound    g_sOneLeft;            // plays when only one chef remains in the wave
static bool     g_sOneLeftOK = false;
// Additional chef death variants; g_sDie is variant 0, g_sDieAlt[i] are variants 1..N
#define CHEF_DIE_ALT_COUNT 3
static Sound    g_sDieAlt[CHEF_DIE_ALT_COUNT];
static bool     g_sDieAltOK[CHEF_DIE_ALT_COUNT] = {0};
static Sound    g_sHeadshot;      // UT announcer headshot sfx
static bool     g_sHeadshotOK = false;
static Sound    g_sFatality;      // MK announcer fatality sfx — plays on kill
static bool     g_sFatalityOK = false;
static Sound    g_sMulti;         // UT "Holy Shit!" — multi-kill (>=2 enemies in one shot)
static bool     g_sMultiOK = false;
static Sound    g_sFirstBlood;    // UT "First Blood!" — plays on first enemy kill each run
static bool     g_sFirstBloodOK = false;
// Enemy-sighted stingers — one is picked at random each time the first enemy
// enters the player's view. Add more by appending to the ENEMY_ALERT_FILES
// table; the loader skips any file that fails to load.
#define ENEMY_ALERT_MAX 4
static Sound    g_sEnemyAlert[ENEMY_ALERT_MAX];
static bool     g_sEnemyAlertOK[ENEMY_ALERT_MAX] = {0};
static int      g_sEnemyAlertCount = 0;
static bool     g_hadVisibleEnemy = false;   // previous-frame visibility state
static Sound    g_sShotgunKill; // stinger that plays when a shotgun kill lands
static bool     g_sShotgunKillOK = false;
static Sound    g_sNextWave;   // stinger when next wave starts
static bool     g_sNextWaveOK = false;
static Sound    g_sMG;          // machine gun firing sound
static bool     g_sMGOK = false;
static int      g_killsThisShot = 0;  // incremented by KillEnemy, reset around each shot/explosion
static Music    g_music;          // looping background track (streamed)
static bool     g_musicOK = false;
static float    g_musicVol = 0.36f;  // adjustable via - / + (persisted to disk)
#define VOL_CONFIG_FILE ".ironfist3d.cfg"

static void SaveMusicVol(void) {
#ifdef __EMSCRIPTEN__
    // Web: MEMFS doesn't persist across page reloads. Skip; the volume
    // will reset to the default on next load. (Could be upgraded to
    // localStorage via EM_JS if persistence is wanted.)
    return;
#else
    const char *home = getenv("HOME"); if (!home) home = getenv("USERPROFILE");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/" VOL_CONFIG_FILE, home);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%f\n", g_musicVol);
    fclose(f);
#endif
}
static void LoadMusicVol(void) {
#ifdef __EMSCRIPTEN__
    return;
#else
    const char *home = getenv("HOME"); if (!home) home = getenv("USERPROFILE");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/" VOL_CONFIG_FILE, home);
    FILE *f = fopen(path, "r");
    if (!f) return;
    float v;
    if (fscanf(f, "%f", &v) == 1 && v >= 0.f && v <= 1.5f) g_musicVol = v;
    fclose(f);
#endif
}
static bool     g_needMouseRelease = false;  // mouse must be released once before firing
static bool     g_lastHitHead = false;  // set while processing a headshot shot; KillEnemy reads it

// enemy stat tables — type 0/1/2 chefs, type 3 = BOSS
static const float ET_HP[]    = {65,   145,  42,   800 };
static const float ET_SPD[]   = {6.0f, 3.6f, 8.8f, 7.5f};  // boss rushes you
static const float ET_DMG[]   = {10,   24,   8,    40  };
static const float ET_RATE[]  = {1.5f, 2.1f, 1.0f, 1.6f};
static const float ET_AR[]    = {24,   20,   30,   40  };
static const float ET_ATK[]   = {3.6f, 3.1f, 4.2f, 1.6f};  // boss gets in your face
static const int   ET_SC[]    = {100,  300,  160,  2500};
static const Color ET_COL[]   = {{60,160,55,255},{140,55,185,255},{40,110,210,255},{220,60,60,255}};
static const Color ET_EYE[]   = {{255,30,20,255},{255,150,0,255},{0,230,255,255},{255,255,120,255}};

// weapon tables
// Weapons: [0]=shotgun (key 1), [1]=machine gun (key 2), [2]=rocket launcher (key 3)
static const char *WPN[]    = {"SHOTGUN", "MACHINE GUN", "LAUNCHER"};
static const float WR[]     = {0.59f, 0.09f, 0.96f};
static const int   WD[]     = {15, 18, 0};
static const int   WPEL[]   = {8, 1, 1};

// ── SOUND ────────────────────────────────────────────────────────────────────
static Sound MkSound(float *d, int n) {
    Wave w = {(unsigned int)n, 44100, 32, 1, d};
    Sound s = LoadSoundFromWave(w);
    return s;
}
static Sound MkGun(float pitch, float dur, float vol) {
    int n = (int)(44100*dur); float *d = malloc(n*4);
    for (int i=0;i<n;i++) {
        float t=(float)i/n, noise=((float)rand()/RAND_MAX)*2-1;
        float env=expf(-t*(12+pitch*5)), tone=sinf(t*6.28f*70*pitch)*0.25f;
        d[i]=(noise*0.75f+tone)*env*vol;
    }
    Sound s=MkSound(d,n); free(d); return s;
}
static Sound MkBoom(void) {
    int n=44100/2; float *d=malloc(n*4);
    for (int i=0;i<n;i++) {
        float t=(float)i/n, noise=((float)rand()/RAND_MAX)*2-1;
        float env=expf(-t*4.5f), r=sinf(t*180)*0.4f*expf(-t*9);
        d[i]=(noise*0.8f+r)*env*0.9f;
    }
    Sound s=MkSound(d,n); free(d); return s;
}
static Sound MkHurt(void) {
    int n=44100/7; float *d=malloc(n*4);
    for (int i=0;i<n;i++) { float t=(float)i/n; d[i]=sinf(t*6.28f*260)*expf(-t*14)*0.5f; }
    Sound s=MkSound(d,n); free(d); return s;
}
static Sound MkPickup(void) {
    int n=44100/5; float *d=malloc(n*4);
    for (int i=0;i<n;i++) { float t=(float)i/n; d[i]=sinf(t*6.28f*(600+t*700))*expf(-t*7)*0.4f; }
    Sound s=MkSound(d,n); free(d); return s;
}
static Sound MkEmpty(void) {
    int n=44100/18; float *d=malloc(n*4);
    for (int i=0;i<n;i++) { float t=(float)i/n; d[i]=sinf(t*6.28f*140)*expf(-t*28)*0.28f; }
    Sound s=MkSound(d,n); free(d); return s;
}
static Sound MkDie(void) {
    int n=44100/3; float *d=malloc(n*4);
    for (int i=0;i<n;i++) {
        float t=(float)i/n, noise=((float)rand()/RAND_MAX)*2-1;
        d[i]=(noise*0.5f+sinf(t*6.28f*(120-t*80))*0.5f)*expf(-t*6)*0.5f;
    }
    Sound s=MkSound(d,n); free(d); return s;
}

// ── TEXTURE GENERATION ───────────────────────────────────────────────────────
static Texture2D MkBrick(void) {
    int S=256; Image img=GenImageColor(S,S,(Color){40,20,12,255});
    int bw=64, bh=32;
    for (int row=0; row<S/bh+1; row++) {
        int ox=(row%2)*(bw/2);
        for (int col=-1; col<S/bw+1; col++) {
            int x0=col*bw+ox, y0=row*bh;
            unsigned char r=82+rand()%28, g=42+rand()%16, b=24+rand()%10;
            for (int py=y0+2; py<y0+bh-2 && py<S; py++)
                for (int px=x0+2; px<x0+bw-2 && px<S; px++)
                    if (px>=0 && py>=0) ImageDrawPixel(&img,px,py,(Color){r,g,b,255});
        }
    }
    Texture2D t=LoadTextureFromImage(img); UnloadImage(img);
    GenTextureMipmaps(&t); SetTextureFilter(t,TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t,TEXTURE_WRAP_REPEAT); return t;
}
static Texture2D MkFloor(void) {
    // Metal grating / industrial tile
    int S=256; Image img=GenImageColor(S,S,(Color){22,20,18,255});
    // Large concrete tiles
    int ts=64;
    for (int r=0;r<S/ts;r++) for (int c=0;c<S/ts;c++) {
        unsigned char base=38+rand()%14;
        // Tile fill with subtle noise
        for (int py=r*ts+2;py<r*ts+ts-2;py++) for (int px=c*ts+2;px<c*ts+ts-2;px++) {
            unsigned char v=base+(rand()%6);
            ImageDrawPixel(&img,px,py,(Color){v,v,(unsigned char)(v-4),255});
        }
        // Grout lines (dark border)
        ImageDrawRectangle(&img,c*ts,r*ts,ts,2,(Color){12,12,12,255});
        ImageDrawRectangle(&img,c*ts,r*ts,2,ts,(Color){12,12,12,255});
    }
    // Metal grate holes pattern over tiles
    for (int r=8;r<S-8;r+=16) for (int c=8;c<S-8;c+=16) {
        ImageDrawRectangle(&img,c,r,8,8,(Color){10,10,10,255});
        ImageDrawRectangle(&img,c+1,r+1,6,6,(Color){16,14,12,255});
    }
    // Worn edge highlights on tiles
    for (int r=0;r<S/ts;r++) for (int c=0;c<S/ts;c++) {
        ImageDrawRectangle(&img,c*ts+2,r*ts+2,ts-4,1,(Color){65,62,55,255});
        ImageDrawRectangle(&img,c*ts+2,r*ts+2,1,ts-4,(Color){65,62,55,255});
    }
    Texture2D t=LoadTextureFromImage(img); UnloadImage(img);
    GenTextureMipmaps(&t); SetTextureFilter(t,TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t,TEXTURE_WRAP_REPEAT); return t;
}

static Texture2D MkCeil(void) {
    // Concrete panels with recessed lighting strips
    int S=256; Image img=GenImageColor(S,S,(Color){20,20,28,255});
    // Panel grid
    int ps=64;
    for (int r=0;r<S/ps;r++) for (int c=0;c<S/ps;c++) {
        unsigned char base=26+rand()%10;
        for (int py=r*ps+3;py<r*ps+ps-3;py++) for (int px=c*ps+3;px<c*ps+ps-3;px++) {
            unsigned char v=base+(rand()%5);
            ImageDrawPixel(&img,px,py,(Color){(unsigned char)(v-2),(unsigned char)(v-2),v,255});
        }
        // Panel border (recessed look)
        ImageDrawRectangle(&img,c*ps,  r*ps,  ps,3,(Color){10,10,14,255});
        ImageDrawRectangle(&img,c*ps,  r*ps,  3,ps,(Color){10,10,14,255});
        ImageDrawRectangle(&img,c*ps,  r*ps+ps-3,ps,3,(Color){40,40,55,255});
        ImageDrawRectangle(&img,c*ps+ps-3,r*ps,3,ps,(Color){40,40,55,255});
    }
    // Lighting strip down the middle of each panel row
    for (int r=0;r<S/ps;r++) {
        int ly=r*ps+ps/2-3;
        ImageDrawRectangle(&img,4,ly,S-8,6,(Color){50,50,70,255});
        ImageDrawRectangle(&img,4,ly+1,S-8,4,(Color){70,68,90,200});
    }
    // Rivets at panel corners
    for (int r=0;r<=S/ps;r++) for (int c=0;c<=S/ps;c++) {
        int rx=c*ps-2, ry=r*ps-2;
        if (rx>=0&&ry>=0&&rx<S&&ry<S)
            ImageDrawRectangle(&img,rx,ry,4,4,(Color){55,55,70,255});
    }
    Texture2D t=LoadTextureFromImage(img); UnloadImage(img);
    GenTextureMipmaps(&t); SetTextureFilter(t,TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t,TEXTURE_WRAP_REPEAT); return t;
}

// ── WORLD MESH BUILDER ───────────────────────────────────────────────────────
// Build one big mesh of only exposed wall faces (proper normals)
static Mesh BuildWallMesh(void) {
    // count exposed faces
    int faces = 0;
    for (int r=0;r<ROWS;r++) for (int c=0;c<COLS;c++) {
        if (!MAP[r][c]) continue;
        if (r>0 && !MAP[r-1][c]) faces++; // south face (+Z neighbor is open)
        if (r<ROWS-1 && !MAP[r+1][c]) faces++;
        if (c>0 && !MAP[r][c-1]) faces++;
        if (c<COLS-1 && !MAP[r][c+1]) faces++;
    }

    int vCount = faces*4;
    int tCount = faces*2;
    float *verts  = (float*)malloc(vCount*3*4);
    float *norms  = (float*)malloc(vCount*3*4);
    float *uvs    = (float*)malloc(vCount*2*4);
    unsigned short *idx = (unsigned short*)malloc(tCount*3*2);

    (void)0;

    // helper: add one quad
    #define ADDQUAD(ax,ay,az, bx,by,bz, cx,cy,cz, dx,dy,dz, nx,ny,nz, u0,u1,v0,v1) \
    do { \
        int base=vi/3; \
        verts[vi++]=ax; verts[vi++]=ay; verts[vi++]=az; \
        verts[vi++]=bx; verts[vi++]=by; verts[vi++]=bz; \
        verts[vi++]=cx; verts[vi++]=cy; verts[vi++]=cz; \
        verts[vi++]=dx; verts[vi++]=dy; verts[vi++]=dz; \
        for (int _q=0;_q<4;_q++){norms[fi++]=nx;norms[fi++]=ny;norms[fi++]=nz;} \
        uvs[ii++]=u0;uvs[ii++]=v0; uvs[ii++]=u1;uvs[ii++]=v0; \
        uvs[ii++]=u1;uvs[ii++]=v1; uvs[ii++]=u0;uvs[ii++]=v1; \
        int ib=vi/3-4; \
        idx[ii]=(unsigned short)ib; idx[ii+1]=(unsigned short)(ib+1); idx[ii+2]=(unsigned short)(ib+2); \
        idx[ii+3]=(unsigned short)ib; idx[ii+4]=(unsigned short)(ib+2); idx[ii+5]=(unsigned short)(ib+3); \
        ii+=6; \
    } while(0)

    // rebuild properly without the macro index collision (use separate counters)
    int vi2=0, ni2=0, ui2=0, xi2=0, vtotal=0;
    free(verts); free(norms); free(uvs); free(idx);
    verts = (float*)malloc(vCount*3*sizeof(float));
    norms = (float*)malloc(vCount*3*sizeof(float));
    uvs   = (float*)malloc(vCount*2*sizeof(float));
    idx   = (unsigned short*)malloc(tCount*3*sizeof(unsigned short));

    for (int r=0;r<ROWS;r++) for (int c=0;c<COLS;c++) {
        if (!MAP[r][c]) continue;
        float x0=c*CELL, x1=(c+1)*CELL;
        float z0=r*CELL, z1=(r+1)*CELL;
        float y0=0, y1=WALL_H;
        float uw=1.f, uh=WALL_H/CELL;

        // helper lambda via local function-like macro
        #define FACE(ax,ay,az, bx,by,bz, cx,cy,cz, dx,dy,dz, nnx,nny,nnz) \
        { \
            int base=vtotal; \
            verts[vi2++]=ax; verts[vi2++]=ay; verts[vi2++]=az; \
            verts[vi2++]=bx; verts[vi2++]=by; verts[vi2++]=bz; \
            verts[vi2++]=cx; verts[vi2++]=cy; verts[vi2++]=cz; \
            verts[vi2++]=dx; verts[vi2++]=dy; verts[vi2++]=dz; \
            norms[ni2++]=nnx;norms[ni2++]=nny;norms[ni2++]=nnz; \
            norms[ni2++]=nnx;norms[ni2++]=nny;norms[ni2++]=nnz; \
            norms[ni2++]=nnx;norms[ni2++]=nny;norms[ni2++]=nnz; \
            norms[ni2++]=nnx;norms[ni2++]=nny;norms[ni2++]=nnz; \
            uvs[ui2++]=0;  uvs[ui2++]=0; \
            uvs[ui2++]=uw; uvs[ui2++]=0; \
            uvs[ui2++]=uw; uvs[ui2++]=uh; \
            uvs[ui2++]=0;  uvs[ui2++]=uh; \
            idx[xi2++]=(unsigned short)(base);   idx[xi2++]=(unsigned short)(base+1); idx[xi2++]=(unsigned short)(base+2); \
            idx[xi2++]=(unsigned short)(base);   idx[xi2++]=(unsigned short)(base+2); idx[xi2++]=(unsigned short)(base+3); \
            vtotal+=4; \
        }

        // -Z face (viewer stands at z < z0, normal points -Z)
        if (r>0 && !MAP[r-1][c])
            FACE(x0,y1,z0, x1,y1,z0, x1,y0,z0, x0,y0,z0, 0,0,-1)
        // +Z face (viewer stands at z > z1, normal points +Z)
        if (r<ROWS-1 && !MAP[r+1][c])
            FACE(x1,y1,z1, x0,y1,z1, x0,y0,z1, x1,y0,z1, 0,0,1)
        // -X face
        if (c>0 && !MAP[r][c-1])
            FACE(x0,y1,z1, x0,y1,z0, x0,y0,z0, x0,y0,z1, -1,0,0)
        // +X face
        if (c<COLS-1 && !MAP[r][c+1])
            FACE(x1,y1,z0, x1,y1,z1, x1,y0,z1, x1,y0,z0, 1,0,0)

        #undef FACE
    }

    Mesh m = {0};
    m.vertexCount  = vtotal;
    m.triangleCount = vtotal/2;
    m.vertices = verts;
    m.normals  = norms;
    m.texcoords = uvs;
    m.indices  = idx;
    UploadMesh(&m, false);
    return m;
}

static Mesh BuildPlaneMesh(float y, float uw, float uh, bool flipNorm) {
    float *v = malloc(4*3*4), *n = malloc(4*3*4), *uv = malloc(4*2*4);
    unsigned short *ix = malloc(6*2);
    float nx=0,ny=flipNorm?-1.f:1.f,nz=0;
    float x0=0,x1=COLS*CELL,z0=0,z1=ROWS*CELL;
    int vi=0,ni=0,ui=0;
    v[vi++]=x0;v[vi++]=y;v[vi++]=z0; v[vi++]=x1;v[vi++]=y;v[vi++]=z0;
    v[vi++]=x1;v[vi++]=y;v[vi++]=z1; v[vi++]=x0;v[vi++]=y;v[vi++]=z1;
    for (int i=0;i<4;i++){n[ni++]=nx;n[ni++]=ny;n[ni++]=nz;}
    uv[ui++]=0;uv[ui++]=0; uv[ui++]=uw;uv[ui++]=0;
    uv[ui++]=uw;uv[ui++]=uh; uv[ui++]=0;uv[ui++]=uh;
    ix[0]=0;ix[1]=1;ix[2]=2; ix[3]=0;ix[4]=2;ix[5]=3;
    Mesh m={0}; m.vertexCount=4; m.triangleCount=2;
    m.vertices=v; m.normals=n; m.texcoords=uv; m.indices=ix;
    UploadMesh(&m,false);
    return m;
}

static Model MakeShaderModel(Mesh mesh, Texture2D tex) {
    Model m = LoadModelFromMesh(mesh);
    m.materials[0].shader = g_shader;
    m.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    return m;
}

// ── COLLISION ────────────────────────────────────────────────────────────────
static bool IsWall(float wx, float wz) {
    int c=(int)(wx/CELL), r=(int)(wz/CELL);
    if (r<0||r>=ROWS||c<0||c>=COLS) return true;
    return MAP[r][c]==1;
}

// Circle-vs-grid wall check. Any wall cell whose AABB is within `rad` of
// (cx, cz) blocks. Smoother than 3-point shoulder tests — avoids false-block
// at wall corners where the body only grazes the cell diagonally.
static bool IsWallCircle(float cx, float cz, float rad) {
    int cMin = (int)((cx - rad) / CELL);
    int cMax = (int)((cx + rad) / CELL);
    int rMin = (int)((cz - rad) / CELL);
    int rMax = (int)((cz + rad) / CELL);
    for (int row = rMin; row <= rMax; row++) {
        for (int col = cMin; col <= cMax; col++) {
            if (row<0||row>=ROWS||col<0||col>=COLS) return true;
            if (MAP[row][col] != 1) continue;
            float ax0 = col * CELL, ax1 = (col+1) * CELL;
            float az0 = row * CELL, az1 = (row+1) * CELL;
            float qx = fmaxf(ax0, fminf(cx, ax1));
            float qz = fmaxf(az0, fminf(cz, az1));
            float dx = cx - qx, dz = cz - qz;
            if (dx*dx + dz*dz < rad*rad) return true;
        }
    }
    return false;
}

// ── PLATFORM COLLISION ──────────────────────────────────────────────────────
// Returns true if the position (x, z) would penetrate a platform whose top is
// more than STEP_H above currentY (i.e. too tall to walk up onto).
static bool PlatBlocks(float x, float z, float currentY, float rad) {
    for (int i = 0; i < g_platCount; i++) {
        Platform *p = &g_plats[i];
        if (x > p->x0 - rad && x < p->x1 + rad &&
            z > p->z0 - rad && z < p->z1 + rad) {
            if (currentY < p->top - STEP_H) return true;
        }
    }
    return false;
}

// How far inside a too-tall platform's safety margin the point lies — 0 if the
// point is clear, positive if penetrating. Used so an entity that just fell off
// a platform (and so overlaps the edge from the side) can still move AWAY —
// allow moves only if they don't deepen penetration.
static float PlatPenetration(float x, float z, float currentY, float rad) {
    float worst = 0.f;
    for (int i = 0; i < g_platCount; i++) {
        Platform *p = &g_plats[i];
        if (currentY >= p->top - STEP_H) continue;
        if (x <= p->x0 - rad || x >= p->x1 + rad ||
            z <= p->z0 - rad || z >= p->z1 + rad) continue;
        float dxL = x - (p->x0 - rad);
        float dxR = (p->x1 + rad) - x;
        float dzF = z - (p->z0 - rad);
        float dzB = (p->z1 + rad) - z;
        float d = fminf(fminf(dxL, dxR), fminf(dzF, dzB));
        if (d > worst) worst = d;
    }
    return worst;
}

// Return the highest platform top the player can stand on at (x, z) given
// their current Y (i.e. any platform whose top is <= currentY + epsilon).
// Default ground is 0.
static float PlatGroundAt(float x, float z, float currentY) {
    float best = 0.f;
    for (int i = 0; i < g_platCount; i++) {
        Platform *p = &g_plats[i];
        if (x >= p->x0 && x <= p->x1 && z >= p->z0 && z <= p->z1) {
            if (p->top > best && p->top <= currentY + 0.05f) best = p->top;
        }
    }
    return best;
}

// Same as PlatGroundAt but with a radius margin — the entity steps onto a
// platform as soon as its body overlaps the platform footprint, not only once
// its centre crosses the strict edge. Used for enemies so they climb stairs
// smoothly when approaching from the side instead of clipping into the mesh.
static float PlatGroundAtR(float x, float z, float currentY, float rad) {
    float best = 0.f;
    for (int i = 0; i < g_platCount; i++) {
        Platform *p = &g_plats[i];
        if (x >= p->x0 - rad && x <= p->x1 + rad &&
            z >= p->z0 - rad && z <= p->z1 + rad) {
            if (p->top > best && p->top <= currentY + 0.05f) best = p->top;
        }
    }
    return best;
}

// Per-axis move for enemies — blocks against walls AND tall platforms, matching
// the player's movement rules. Enemies can still step onto platforms within STEP_H.
static void EnemyMove(Enemy *e, float dx, float dz) {
    // Boss is a ~3m-wide billboard, so give him a collision radius that
    // roughly matches his sprite footprint — otherwise his body stops 0.85m
    // from a wall but the sprite keeps extending 0.65m INTO the wall.
    const float r = (e->type == 3) ? 1.2f : 0.42f;
    // Compare penetration instead of a hard block — an enemy already inside a
    // platform's margin (e.g. just fell off its edge) can still move AWAY from
    // it, they just can't push FURTHER in. No free re-entry either.
    float curPen = PlatPenetration(e->pos.x, e->pos.z, e->pos.y, r + 0.1f);
    // Try the combined diagonal move first so the enemy can round outside
    // corners smoothly. Falls back to per-axis if the diagonal is blocked.
    float nxD = e->pos.x + dx, nzD = e->pos.z + dz;
    float penD = PlatPenetration(nxD, nzD, e->pos.y, r + 0.1f);
    if (!IsWallCircle(nxD, nzD, r) && penD <= curPen + 0.001f) {
        e->pos.x = nxD; e->pos.z = nzD;
        return;
    }
    float nx = e->pos.x + dx;
    float newPenX = PlatPenetration(nx, e->pos.z, e->pos.y, r + 0.1f);
    if (!IsWallCircle(nx, e->pos.z, r) && newPenX <= curPen + 0.001f) e->pos.x = nx;
    float nz = e->pos.z + dz;
    float newPenZ = PlatPenetration(e->pos.x, nz, e->pos.y, r + 0.1f);
    if (!IsWallCircle(e->pos.x, nz, r) && newPenZ <= curPen + 0.001f) e->pos.z = nz;
}

// Build the level's platforms. Called once at init.
static void InitPlatforms(void) {
    g_platCount = 0;
    // Central corridor (rows 9-10) has the open space. Put a stair-up + platform + stair-down.
    //
    //   z=36 ──────────────────────────────────────────── z=44
    //        [step1][step2][step3] [ big plat 1.35 ] [step4][step5]
    //
    // Each step = 0.45m tall (under STEP_H = 0.55) so you can walk up smoothly.
    float z0 = 38.f, z1 = 42.f;  // stairs are 4m deep
    g_plats[g_platCount++] = (Platform){52.f, z0, 55.f, z1, 0.45f};
    g_plats[g_platCount++] = (Platform){55.f, z0, 58.f, z1, 0.90f};
    g_plats[g_platCount++] = (Platform){58.f, z0, 61.f, z1, 1.35f};
    // Big platform (full 8m wide, 8m deep — covers rows 9-10 mostly)
    g_plats[g_platCount++] = (Platform){61.f, 36.f, 69.f, 44.f, 1.35f};
    // Stair down
    g_plats[g_platCount++] = (Platform){69.f, z0, 72.f, z1, 0.90f};
    g_plats[g_platCount++] = (Platform){72.f, z0, 75.f, z1, 0.45f};

    // Second feature: a tall lone platform near start-ish that you must JUMP onto
    g_plats[g_platCount++] = (Platform){20.f, 24.f, 26.f, 28.f, 1.10f};
    // and a "balcony" in the far corner (accessed via jump)
    g_plats[g_platCount++] = (Platform){92.f, 60.f, 104.f, 68.f, 1.60f};
}

// ── PARTICLES ────────────────────────────────────────────────────────────────
static void SpawnPart(Vector3 p, Vector3 v, Color c, float life, float sz, bool grav) {
    for (int i=0;i<MAX_PARTS;i++) if (!g_pt[i].active) {
        g_pt[i]=(Part){p,v,life,life,sz,c,true,grav}; return;
    }
}
static void Blood(Vector3 p, int n) {
    // Realistic blood spatter. Each droplet starts at a small random offset
    // from the impact point so they never clump into one visible block.
    int count = n * 2;
    for (int i = 0; i < count; i++) {
        Vector3 offset = {
            ((float)rand()/RAND_MAX - 0.5f) * 0.35f,
            ((float)rand()/RAND_MAX - 0.5f) * 0.35f,
            ((float)rand()/RAND_MAX - 0.5f) * 0.35f
        };
        Vector3 spawn = { p.x + offset.x, p.y + offset.y, p.z + offset.z };
        Vector3 v = {
            ((float)rand()/RAND_MAX - 0.5f) * 8.f,
            (float)rand()/RAND_MAX * 3.f + 0.3f,
            ((float)rand()/RAND_MAX - 0.5f) * 8.f
        };
        unsigned char r = (unsigned char)(90 + rand() % 70);
        unsigned char g = (unsigned char)(rand() % 10);
        unsigned char b = (unsigned char)(rand() % 8);
        Color col = { r, g, b, 220 };  // slight transparency so clumps blend
        float size = 0.018f + (float)rand()/RAND_MAX * 0.030f; // smaller droplets
        float life = 0.35f + (float)rand()/RAND_MAX * 0.45f;
        SpawnPart(spawn, v, col, life, size, true);
    }
}
static void Sparks(Vector3 p, int n) {
    // Bright orange sparks (fast, short-lived)
    for (int i=0;i<n;i++) {
        Vector3 v = {
            ((float)rand()/RAND_MAX - 0.5f) * 10.f,
            (float)rand()/RAND_MAX * 6.f,
            ((float)rand()/RAND_MAX - 0.5f) * 10.f
        };
        SpawnPart(p, v, ORANGE,
                  0.2f + (float)rand()/RAND_MAX * 0.25f,
                  0.04f, true);
    }
    // Grey/brown debris chips that stick to the floor (concrete/brick chips)
    int chips = n * 2 / 3;
    for (int i = 0; i < chips; i++) {
        Vector3 v = {
            ((float)rand()/RAND_MAX - 0.5f) * 6.f,
            (float)rand()/RAND_MAX * 4.f + 0.5f,
            ((float)rand()/RAND_MAX - 0.5f) * 6.f
        };
        unsigned char shade = 60 + rand() % 40;
        Color chipCol = { shade, (unsigned char)(shade*0.7f), (unsigned char)(shade*0.5f), 255 };
        SpawnPart(p, v, chipCol,
                  0.5f + (float)rand()/RAND_MAX * 0.6f,
                  0.05f + (float)rand()/RAND_MAX * 0.05f, true);
    }
    // Dust puff — small near-static particles that fade quickly
    for (int i = 0; i < n / 3; i++) {
        Vector3 v = {
            ((float)rand()/RAND_MAX - 0.5f) * 2.f,
            (float)rand()/RAND_MAX * 1.5f + 0.2f,
            ((float)rand()/RAND_MAX - 0.5f) * 2.f
        };
        SpawnPart(p, v, (Color){170,160,150,200},
                  0.4f + (float)rand()/RAND_MAX * 0.3f,
                  0.08f, false);
    }
}
static void Explode(Vector3 p) {
    // Distance-scaled volume: up to 2.5x at point-blank, floor 0.25x far away
    float dx = p.x - g_p.pos.x, dz = p.z - g_p.pos.z;
    float d  = sqrtf(dx*dx + dz*dz);
    float vol = 2.5f - d * 0.07f;   // 2.5 at 0m, 1.1 at ~20m, floors at 0.25
    if (vol < 0.25f) vol = 0.25f;
    SetSoundVolume(g_sExplode, vol);
    PlaySound(g_sExplode);
    for (int i=0;i<28;i++) {
        Vector3 v={(float)rand()/RAND_MAX*16-8,(float)rand()/RAND_MAX*9+1,(float)rand()/RAND_MAX*16-8};
        Color c=i<14?(Color){255,90,0,255}:(Color){255,210,0,255};
        SpawnPart(p,v,c,0.55f+(float)rand()/RAND_MAX*0.5f,0.06f+(float)rand()/RAND_MAX*0.08f,true);
    }
    g_p.shake=fmaxf(g_p.shake,0.55f);
    // muzzle flash light slot 6 repurposed for explosion
    g_lights[6]=(LightDef){p,{1.0f,0.5f,0.1f},14.f,1}; ShaderSetLight(6);
}
static void UpdParts(float dt) {
    for (int i=0;i<MAX_PARTS;i++) {
        Part *p=&g_pt[i]; if (!p->active) continue;
        p->life-=dt;
        if (p->life<=0){p->active=false;continue;}
        if (p->grav) p->vel.y+=GRAV*dt;
        p->pos=Vector3Add(p->pos,Vector3Scale(p->vel,dt));
        if (p->pos.y<0.05f && p->grav) {
            // Only small droplets (blood) become floor decals. Big fire/smoke
            // particles from explosions just bounce and fade normally.
            float sp2 = p->vel.x*p->vel.x + p->vel.y*p->vel.y + p->vel.z*p->vel.z;
            if (sp2 < 3.5f && p->size < 0.08f) {
                p->pos.y = 0.005f;
                p->vel = (Vector3){0, 0, 0};
                p->grav = false;
                p->stuck = true;
                p->life    = 10.f + (float)rand()/RAND_MAX * 6.f;
                p->maxLife = p->life;
            } else {
                p->pos.y = 0.05f; p->vel.y *= -0.25f;
                p->vel.x *= 0.55f; p->vel.z *= 0.55f;
            }
        }
    }
}
static void DrawParts(void) {
    for (int i=0;i<MAX_PARTS;i++) {
        Part *p=&g_pt[i]; if (!p->active) continue;
        float t=p->life/p->maxLife;
        Color c=p->col; c.a=(unsigned char)(255*t);
        if (p->stuck) {
            // Flat splat on the floor — small droplet, wider than it is tall
            float s = p->size * 1.8f;
            // Fade over final 25% of life
            float fade = (t < 0.25f) ? (t / 0.25f) : 1.f;
            c.a = (unsigned char)(255 * fade);
            DrawCubeV(p->pos, (Vector3){s, 0.02f, s}, c);
        } else {
            float s = p->size * t;  // half-scale — tiny droplet, not chunky cube
            DrawCubeV(p->pos, (Vector3){s, s, s}, c);
        }
    }
}

// ── PICKUPS ──────────────────────────────────────────────────────────────────
static void SpawnPick(float x, float z, int type) {
    if (g_pkc>=MAX_PICKS) return;
    g_pk[g_pkc++]=(Pickup){{x,0.5f,z},type,rand()&1,true,0};
}
static void SeedPicks(void) {
    static const int POS[][2]={{5,5},{14,9},{24,5},{5,14},{14,14},{24,14},{9,18},{19,18},{2,9},{27,9},{9,2},{19,2},{14,18},{14,2},{2,14},{27,14}};
    static const int  TYP[]  ={0,1,0,1,0,2,0,1,0,1,2,0,0,1,0,2};
    for (int i=0;i<16;i++) {
        int c=POS[i][0],r=POS[i][1];
        if (r<ROWS&&c<COLS&&MAP[r][c]==0) SpawnPick(c*CELL+CELL/2.f,r*CELL+CELL/2.f,TYP[i]);
    }
}
static void UpdPicks(void) {
    float t=GetTime();
    for (int i=0;i<g_pkc;i++) {
        Pickup *pk=&g_pk[i]; if (!pk->active) continue;
        pk->pos.y=0.5f+sinf(t*2.5f+i)*0.15f;
        float dx=pk->pos.x-g_p.pos.x, dz=pk->pos.z-g_p.pos.z;
        if (sqrtf(dx*dx+dz*dz)<1.2f) {
            // Don't grab a health pickup if we're already at full HP
            if (pk->type == 0 && g_p.hp >= g_p.maxHp) continue;
            pk->active=false;
            if      (pk->type == 0 && g_sHealthPickupOK) PlaySound(g_sHealthPickup);
            else if (pk->type == 4 && g_sMGPickupOK)     PlaySound(g_sMGPickup);
            else                                         PlaySound(g_sPickup);
            g_msgT=1.8f;
            switch (pk->type) {
                case 0: g_p.hp     =fminf(g_p.maxHp,g_p.hp+35);         snprintf(g_msg,80,"+35 HEALTH");  break;
                case 1: g_p.shells =(int)fminf(99, g_p.shells +16);     snprintf(g_msg,80,"+16 SHELLS");  break;
                case 2: g_p.rockets=(int)fminf(30, g_p.rockets+ 5);     snprintf(g_msg,80,"+5 ROCKETS");  break;
                case 3: g_p.bullets=(int)fminf(200,g_p.bullets+24);     snprintf(g_msg,80,"+24 BULLETS"); break;
                case 4: g_p.mgAmmo =(int)fminf(300,g_p.mgAmmo +50);     snprintf(g_msg,80,"+50 MG ROUNDS"); break;
            }
        }
    }
}
// Volumetric-ish ceiling lights: visible fixture + translucent cone beam below.
// Uses the first 6 entries of g_lights (the static scene lights; slots 6-7 are dynamic).
static void DrawCeilingLights(Camera3D cam) {
    (void)cam;
    // Fixture geometry + light cone — additive blending for the "god ray" glow
    for (int i = 0; i < 6; i++) {
        LightDef *L = &g_lights[i];
        if (!L->enabled) continue;
        Color col = {
            (unsigned char)(fminf(L->color.x,1.f)*255),
            (unsigned char)(fminf(L->color.y,1.f)*255),
            (unsigned char)(fminf(L->color.z,1.f)*255),
            255
        };
        // Light fixture: a flat disc / emissive plate just under the ceiling
        Vector3 fixturePos = L->pos;
        DrawCube(fixturePos, 0.7f, 0.15f, 0.7f, (Color){col.r, col.g, col.b, 255});
        // Small glowing orb inside the housing
        DrawSphere((Vector3){fixturePos.x, fixturePos.y - 0.05f, fixturePos.z}, 0.22f, WHITE);
    }

    // Light cones (draw last, additive blended, so they brighten rather than occlude)
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    for (int i = 0; i < 6; i++) {
        LightDef *L = &g_lights[i];
        if (!L->enabled) continue;
        Color base = {
            (unsigned char)(L->color.x*90),
            (unsigned char)(L->color.y*90),
            (unsigned char)(L->color.z*90),
            70
        };
        // Cone going from ceiling down to floor — small radius at top, wider at bottom.
        // DrawCylinderEx(start, end, startRad, endRad, slices, color)
        Vector3 top    = {L->pos.x, L->pos.y - 0.1f, L->pos.z};
        Vector3 bottom = {L->pos.x, 0.02f,           L->pos.z};
        DrawCylinderEx(top, bottom, 0.3f, 2.4f, 16, base);
        // Inner brighter core shaft
        Color core = base; core.a = 120;
        DrawCylinderEx(top, bottom, 0.12f, 1.0f, 12, core);
    }
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
}

static void DrawPicks(Camera3D cam) {
    // Fallback colours if a sprite fails to load
    static Color tc[] = {
        {255, 60, 60,255},  // health
        {255,220,  0,255},  // shells
        {255,140,  0,255},  // rockets
        {220,220,120,255},  // bullets
        {180,200,255,255},  // MG
    };
    // Size per ammo type (the sprites have different native sizes)
    static const float sz[] = {0.9f, 0.7f, 0.65f, 0.45f, 0.85f};
    for (int i=0;i<g_pkc;i++) {
        Pickup *pk=&g_pk[i]; if (!pk->active) continue;
        Texture2D tex = {0};
        if (pk->type == 0 && g_healthTex[pk->variant].id) tex = g_healthTex[pk->variant];
        else if (pk->type > 0 && pk->type < 5 && g_ammoTex[pk->type].id) tex = g_ammoTex[pk->type];

        if (tex.id) {
            DrawBillboard(cam, tex, pk->pos, sz[pk->type], WHITE);
        } else {
            DrawSphere(pk->pos, 0.22f, tc[pk->type]);
            DrawCircle3D(pk->pos, 0.36f, (Vector3){1,0,0}, 90.f, Fade(tc[pk->type], 0.35f));
        }
    }
}

// ── MSG ──────────────────────────────────────────────────────────────────────
static void Msg(const char *s) { strncpy(g_msg,s,79); g_msgT=2.0f; }

// ── ENEMIES ──────────────────────────────────────────────────────────────────
// "Alive" = active AND not dying (corpse/dying enemies are still drawn but don't count)
static int Alive(void) { int n=0; for(int i=0;i<g_ec;i++) if(g_e[i].active && !g_e[i].dying) n++; return n; }

// Pick a random open corner of the map to spawn the boss. All four interior
// corner cells ((1,1), (1,28), (18,1), (18,28)) are open in MAP[][] — centred
// in the cell they give the boss ~1m of clearance from the perimeter walls.
static void PickBossSpawn(float *bx, float *bz) {
    static const struct { int row, col; } corners[4] = {
        { 1,  1 }, { 1,  28 }, { 18, 1 }, { 18, 28 }
    };
    int i = rand() % 4;
    *bx = corners[i].col * CELL + CELL * 0.5f;
    *bz = corners[i].row * CELL + CELL * 0.5f;
}

// ── DEBUG LOG ────────────────────────────────────────────────────────────────
// Overwrites /tmp/ironfist-debug.log at 5 Hz with the player + enemy snapshot.
// Tail it in another terminal via debug.sh when you hit a pathing weirdness
// and want to describe it precisely. Format is append-by-frame so scrollback
// shows the run-up to whatever you're investigating.
#define DEBUG_LOG_PATH "/tmp/ironfist-debug.log"
static FILE *g_dbgLog = NULL;
static double g_dbgLastT = 0.0;

static const char *StateName(EnemyState s) {
    return s==ES_PATROL ? "PATROL" : s==ES_CHASE ? "CHASE" : "ATTACK";
}
static const char *TypeName(int t) {
    return t==0 ? "CHEF" : t==1 ? "HEAVY" : t==2 ? "FAST" : t==3 ? "BOSS" : "?";
}

static void DebugLogTick(void) {
    if (!g_dbgLog) return;
    double now = GetTime();
    if (now - g_dbgLastT < 0.2) return;   // 5 Hz
    g_dbgLastT = now;

    fprintf(g_dbgLog,
        "[t=%7.2fs] wave=%d bossMode=%d bossFight=%d  player: pos=(%.2f, %.2f, %.2f) "
        "yaw=%.2frad (%.0fdeg) hp=%.0f/%.0f weapon=%d\n",
        now, g_wave, g_bossMode ? 1 : 0, g_bossInterlude ? 1 : 0,
        g_p.pos.x, g_p.pos.y, g_p.pos.z,
        g_p.yaw, g_p.yaw * 180.0f / 3.14159265f,
        g_p.hp, g_p.maxHp, g_p.weapon);

    int alive = 0;
    for (int i = 0; i < g_ec; i++) {
        Enemy *e = &g_e[i];
        if (!e->active) continue;
        // Facing direction: toward player for chase, patrol dir otherwise
        float dx = g_p.pos.x - e->pos.x, dz = g_p.pos.z - e->pos.z;
        float d  = sqrtf(dx*dx + dz*dz);
        float fxdir = 0.f, fzdir = 0.f;
        if (d > 0.01f) { fxdir = dx/d; fzdir = dz/d; }
        fprintf(g_dbgLog,
            "  #%02d %-5s %-5s pos=(%6.2f, %5.2f, %6.2f) "
            "to-player=(%+.2f, %+.2f) dist=%5.2f state=%-6s "
            "hp=%4.0f/%4.0f pd=(%+.2f, %+.2f) flashT=%.2f bleedT=%.2f\n",
            i, TypeName(e->type),
            e->dying ? "DYING" : "alive",
            e->pos.x, e->pos.y, e->pos.z,
            fxdir, fzdir, d,
            StateName(e->state),
            e->hp, e->maxHp,
            e->pd.x, e->pd.z,
            e->flashT, e->bleedT);
        if (!e->dying) alive++;
    }
    fprintf(g_dbgLog, "  (alive=%d)\n\n", alive);
    fflush(g_dbgLog);
}

static void KillEnemy(int i) {
    Enemy *e=&g_e[i];
    // Keep the enemy active=true so the sprite keeps rendering — mark dying so
    // AI/bullets/minimap/alive-count skip it.
    e->dying = true; e->deathT = 0.f; e->hp = 0.f;
    Vector3 bp={e->pos.x,1.0f,e->pos.z};
    // Massive blood burst on death — waist, chest, head for a gorey finish
    Blood(bp, 50);
    Blood((Vector3){e->pos.x, 1.6f, e->pos.z}, 35);
    Blood((Vector3){e->pos.x, 0.4f, e->pos.z}, 25);
    // Pick a random death vocalisation (g_sDie + g_sDieAlt[])
    {
        int total = 1;  // g_sDie itself
        for (int i = 0; i < CHEF_DIE_ALT_COUNT; i++) if (g_sDieAltOK[i]) total++;
        int pick = rand() % total;
        if (pick == 0) PlaySound(g_sDie);
        else {
            int idx = 0;
            for (int i = 0; i < CHEF_DIE_ALT_COUNT; i++) if (g_sDieAltOK[i]) {
                if (++idx == pick) { PlaySound(g_sDieAlt[i]); break; }
            }
        }
    }
    // Only play the MK "FATALITY" shout for non-headshot kills (headshot has its own SFX)
    if (g_sFatalityOK && !g_lastHitHead) {
        SetSoundVolume(g_sFatality, 1.5f);
        PlaySound(g_sFatality);
    }
    g_p.score+=e->score*g_wave; g_p.kills++; g_killsThisShot++;
    if (g_p.kills == 1 && g_sFirstBloodOK) {
        SetSoundVolume(g_sFirstBlood, 1.5f);
        PlaySound(g_sFirstBlood);
    }
    // "One left" stinger — fires the frame the kill takes Alive() from 2 to 1,
    // i.e. one chef remains in this wave. Boss phase always has Alive()==1
    // throughout, so gate on !g_bossInterlude to avoid firing on boss spawn or
    // on any stray kill while the boss is up.
    if (!g_bossInterlude && Alive() == 1 && g_sOneLeftOK && e->type != 3) {
        SetSoundVolume(g_sOneLeft, 1.5f);
        PlaySound(g_sOneLeft);
    }
    // (shotgun-kill stinger now plays on every shotgun shot in Shoot(), not here)
    if ((float)rand()/RAND_MAX<0.55f) {
        // Drop chances: health 40%, shells 25%, MG 20%, rockets 15%
        float r = (float)rand()/RAND_MAX;
        int t = r<0.40f ? 0 : r<0.65f ? 1 : r<0.85f ? 4 : 2;
        // Offset the drop so the pickup billboard doesn't intersect the corpse billboard
        float ang = (float)rand()/RAND_MAX * 6.28318f;
        float dist = 0.7f + (float)rand()/RAND_MAX * 0.4f;  // 0.7..1.1 away
        float ox = cosf(ang)*dist, oz = sinf(ang)*dist;
        // Make sure we didn't offset into a wall
        if (IsWall(e->pos.x+ox, e->pos.z+oz)) { ox = -ox*0.4f; oz = -oz*0.4f; }
        SpawnPick(e->pos.x+ox, e->pos.z+oz, t);
    }
    static const char *names[]={"CHEF","HEAVY CHEF","FAST CHEF"};
    char buf[80]; snprintf(buf,80,"%s DOWN  +%d",names[e->type],e->score*g_wave);
    Msg(buf);
    // Hype text when the wave is down to its final chef. Matches the same
    // guard as the sOneLeft stinger above so audio + text always fire on the
    // same transition. Routed through the dedicated hype banner (bigger,
    // longer, flashing) instead of the regular Msg slot.
    if (!g_bossInterlude && Alive() == 1 && e->type != 3) {
        static const char *hype[] = {
            "LAST ONE STANDING!",
            "ONE LEFT - FINISH THEM!",
            "FINAL CHEF - HUNT IT DOWN!",
            "LONE SURVIVOR - END THIS!",
            "ONE REMAINS - NO MERCY!",
        };
        strncpy(g_hypeMsg, hype[rand() % (int)(sizeof(hype)/sizeof(hype[0]))], 79);
        g_hypeMsg[79] = 0;
        g_hypeDur = 4.5f;
        g_hypeT   = g_hypeDur;
    }
    if (Alive()==0) {
        // Test mode: respawn a boss forever so we can iterate on combat
        if (g_bossMode) {
            Msg("BOSS DOWN - RESPAWN");
            Enemy *ne = &g_e[g_ec++];
            *ne = (Enemy){0};
            float bx, bz; PickBossSpawn(&bx, &bz);
            ne->pos = (Vector3){bx, PlatGroundAt(bx, bz, 100.f), bz};
            ne->type = 3; ne->state = ES_CHASE;  // hunts on spawn — no wander phase
            ne->hp = ne->maxHp = ET_HP[3];
            ne->speed = ET_SPD[3]; ne->dmg = ET_DMG[3];
            ne->rate = ne->cd = ET_RATE[3];
            ne->alertR = ET_AR[3]; ne->atkR = ET_ATK[3];
            ne->score = ET_SC[3]; ne->active = true;
            float ang = (float)rand()/RAND_MAX * 6.28f;
            ne->pd = (Vector3){sinf(ang), 0, cosf(ang)};
            ne->stateT = 1.f + (float)rand()/RAND_MAX * 2.f;
            return;
        }

        // Normal flow:
        //   1. Chefs clear → spawn a boss (interlude begins)
        //   2. Boss dies  → wave++, spawn next wave of chefs
        if (!g_bossInterlude) {
            g_bossInterlude = true;
            if (g_sNextWaveOK) { SetSoundVolume(g_sNextWave, 1.5f); PlaySound(g_sNextWave); }
            char bmsg[64]; snprintf(bmsg, 64, "-- BOSS FIGHT --");
            Msg(bmsg);
            Enemy *ne = &g_e[g_ec++];
            *ne = (Enemy){0};
            float bx, bz; PickBossSpawn(&bx, &bz);
            ne->pos = (Vector3){bx, PlatGroundAt(bx, bz, 100.f), bz};
            ne->type = 3; ne->state = ES_CHASE;  // hunts on spawn — no wander phase
            // Boss scales with wave like chefs do
            float hm = 1.f + g_wave*0.12f;
            ne->hp = ne->maxHp = ET_HP[3] * hm;
            ne->speed = ET_SPD[3] + g_wave*0.10f;
            ne->dmg = ET_DMG[3];
            ne->rate = ne->cd = ET_RATE[3];
            ne->alertR = ET_AR[3]; ne->atkR = ET_ATK[3];
            ne->score = ET_SC[3]; ne->active = true;
            float ang = (float)rand()/RAND_MAX * 6.28f;
            ne->pd = (Vector3){sinf(ang), 0, cosf(ang)};
            ne->stateT = 1.f + (float)rand()/RAND_MAX * 2.f;
            return;
        }
        // Boss just died — bump wave and kick off the next chef round
        g_bossInterlude = false;
        g_wave++;
        if (g_sNextWaveOK) {
            SetSoundVolume(g_sNextWave, 1.5f);
            PlaySound(g_sNextWave);
        }
        char wbuf[64]; snprintf(wbuf,64,"-- WAVE %d INCOMING --",g_wave);
        Msg(wbuf);
        // spawn next wave
        int cnt=10+g_wave*4;
        for (int k=0;k<cnt&&g_ec<MAX_ENEMIES;k++) {
            for (int tries=0;tries<120;tries++) {
                int r=1+rand()%(ROWS-2), c=1+rand()%(COLS-2);
                if (MAP[r][c]) continue;
                float wx=c*CELL+CELL/2.f, wz=r*CELL+CELL/2.f;
                if (hypotf(wx-g_p.pos.x,wz-g_p.pos.z)<10.f) continue;
                float rng=(float)rand()/RAND_MAX;
                int type=g_wave<2?0:rng<0.5f?0:rng<0.78f?2:1;
                Enemy *ne=&g_e[g_ec++];
                *ne=(Enemy){0};
                ne->pos=(Vector3){wx, PlatGroundAt(wx,wz,100.f), wz};  // snap to highest platform top
                ne->type=type;
                ne->state=ES_PATROL;
                float hm=1.f+g_wave*0.12f;
                ne->hp=ne->maxHp=ET_HP[type]*hm;
                ne->speed=ET_SPD[type]+g_wave*0.18f;
                ne->dmg=ET_DMG[type]; ne->rate=ne->cd=ET_RATE[type];
                ne->alertR=ET_AR[type]; ne->atkR=ET_ATK[type];
                ne->score=ET_SC[type]; ne->active=true;
                float ang=(float)rand()/RAND_MAX*6.28318f;
                ne->pd=(Vector3){sinf(ang),0,cosf(ang)};
                ne->stateT=1.f+(float)rand()/RAND_MAX*2.f;
                break;
            }
        }
    }
}

static void DmgEnemy(int i, float d) {
    Enemy *e=&g_e[i]; if (!e->active || e->dying) return;
    e->hp-=d; e->flashT=0.12f; e->state=ES_CHASE;
    if (e->hp<=0) KillEnemy(i);
}

static void UpdEnemies(float dt) {
    for (int i=0;i<g_ec;i++) {
        Enemy *e=&g_e[i]; if (!e->active) continue;
        if (e->dying) { e->deathT += dt; continue; }  // corpse: run death timer, skip AI
        if (e->flashT>0) e->flashT-=dt;
        // Wounded enemies leak blood as they move (drips land behind them and
        // become floor decals). Boss scales up: 4× the body, so spawn several
        // drops per tick, larger, and wider Y spread so the trail is visible.
        if (e->hp < e->maxHp) {
            e->bleedT -= dt;
            if (e->bleedT <= 0.f) {
                bool isBoss = (e->type == 3);
                int   drops      = isBoss ? 4    : 1;
                float bodyRad    = isBoss ? 1.4f : 0.4f;
                float yBase      = isBoss ? 0.5f : 0.5f;
                float ySpread    = isBoss ? 2.8f : 0.5f;
                float dropSize   = isBoss ? 0.075f : 0.045f;
                for (int d = 0; d < drops; d++) {
                    Vector3 dripPos = {
                        e->pos.x + ((float)rand()/RAND_MAX - 0.5f) * bodyRad,
                        yBase + (float)rand()/RAND_MAX * ySpread,
                        e->pos.z + ((float)rand()/RAND_MAX - 0.5f) * bodyRad
                    };
                    Vector3 dripVel = {
                        ((float)rand()/RAND_MAX - 0.5f) * 0.6f,
                        0.2f,
                        ((float)rand()/RAND_MAX - 0.5f) * 0.6f
                    };
                    unsigned char br = 95 + rand() % 55;
                    SpawnPart(dripPos, dripVel, (Color){br, 0, 0, 255},
                              1.5f + (float)rand()/RAND_MAX, dropSize, true);
                }
                // More frequent drips when more hurt. Boss drips ~2× as often
                // as a chef so the trail keeps up with his higher move speed.
                float healthFrac = e->hp / e->maxHp;
                float bleedMult  = isBoss ? 0.5f : 1.f;
                e->bleedT = (0.08f + healthFrac * 0.25f) * bleedMult;
            }
        }
        float dx=g_p.pos.x-e->pos.x, dz=g_p.pos.z-e->pos.z;
        float dist=sqrtf(dx*dx+dz*dz);
        if (dist<e->alertR) e->state=ES_CHASE;
        e->cd-=dt; e->stateT-=dt; e->legT+=dt*e->speed*2.8f;
        if (e->state==ES_PATROL) {
            EnemyMove(e, e->pd.x*e->speed*dt, e->pd.z*e->speed*dt);
            // Snap enemy Y to highest reachable platform under them (auto-step stairs).
            // Use body-radius margin so the chef steps up as soon as his footprint
            // overlaps a step, not only once his centre crosses the strict edge.
            float er = (e->type == 3) ? 1.2f : 0.42f;
            e->pos.y = PlatGroundAtR(e->pos.x, e->pos.z, e->pos.y + STEP_H, er);
            if (e->stateT<0){float a=(float)rand()/RAND_MAX*6.28f;e->pd=(Vector3){sinf(a),0,cosf(a)};e->stateT=1.f+(float)rand()/RAND_MAX*2.5f;}
        } else if (e->state==ES_CHASE) {
            if (dist>e->atkR) {
                // Separation: sum a push-away vector from all nearby live enemies,
                // so chasing chefs fan out instead of stacking on the same line.
                float sepX = 0, sepZ = 0;
                const float SEP_RADIUS = 1.5f;
                for (int j = 0; j < g_ec; j++) {
                    if (j == i) continue;
                    Enemy *o = &g_e[j];
                    if (!o->active || o->dying) continue;
                    float ox = e->pos.x - o->pos.x;
                    float oz = e->pos.z - o->pos.z;
                    float od = sqrtf(ox*ox + oz*oz);
                    if (od > 0.01f && od < SEP_RADIUS) {
                        float push = (SEP_RADIUS - od) / SEP_RADIUS;
                        sepX += (ox/od) * push;
                        sepZ += (oz/od) * push;
                    }
                }
                // Blend: seek player + separation (weighted)
                float sx = dx/dist + sepX * 1.4f;
                float sz = dz/dist + sepZ * 1.4f;
                float slen = sqrtf(sx*sx + sz*sz);
                if (slen > 0.01f) { sx /= slen; sz /= slen; }
                // Obstacle avoidance: if a too-tall platform is on the path to
                // the player, skirt around it tangentially (always the same side
                // per-enemy, so the enemy doesn't oscillate between left/right).
                // IMPORTANT: the probe direction is the RAW seek-to-player unit
                // vector — NOT the separation-blended sx/sz. With several chefs
                // clustered near a platform face, the separation vector can flip
                // the blended direction away from the player frame-to-frame,
                // making the skirt activate on some frames and not others. That
                // caused the whole cluster to jitter in place. Separation still
                // perturbs the *final* motion; it just doesn't get a vote on
                // whether "is the platform blocking my path to the player".
                const float r = (e->type == 3) ? 1.2f : 0.42f;
                const float probe = 1.5f;
                float seekX = dx/dist, seekZ = dz/dist;
                float px = e->pos.x + seekX*probe, pz = e->pos.z + seekZ*probe;
                // Effective Y at the probe: if a walkable step sits there, the
                // enemy will lift to its top when they walk onto it. Without
                // this, a staircase gets flagged as a blocker because the next
                // step looks unreachable from the ground — but it's reachable
                // via the first step. So probeY = highest step the enemy could
                // climb onto at the probe point.
                float probeY = e->pos.y;
                for (int pi = 0; pi < g_platCount; pi++) {
                    Platform *pp = &g_plats[pi];
                    if (px >= pp->x0 - r && px <= pp->x1 + r &&
                        pz >= pp->z0 - r && pz <= pp->z1 + r &&
                        pp->top > probeY && pp->top <= e->pos.y + STEP_H + 0.05f) {
                        probeY = pp->top;
                    }
                }
                Platform *bp = NULL;
                for (int pi = 0; pi < g_platCount; pi++) {
                    Platform *pp = &g_plats[pi];
                    if (px > pp->x0 - (r+0.1f) && px < pp->x1 + (r+0.1f) &&
                        pz > pp->z0 - (r+0.1f) && pz < pp->z1 + (r+0.1f) &&
                        probeY < pp->top - STEP_H) { bp = pp; break; }
                }
                if (bp) {
                    // Skirt direction: perpendicular to the enemy->platform-centre
                    // vector, sign picked per-enemy so a group fans around both sides.
                    float cx = (bp->x0 + bp->x1) * 0.5f;
                    float cz = (bp->z0 + bp->z1) * 0.5f;
                    float ex = e->pos.x - cx, ez = e->pos.z - cz;
                    float el = sqrtf(ex*ex + ez*ez);
                    if (el > 0.01f) { ex /= el; ez /= el; }
                    int bias = ((i * 2654435761u) & 1) ? 1 : -1;
                    float tx = -ez * bias, tz = ex * bias;
                    // Use PURE tangent — no pull-toward-player blend. With a
                    // small player-pull the tangent was getting tipped into
                    // the platform's margin on the adjacent side, triggering
                    // a flip to the opposite tangent, which was ALSO blocked,
                    // so chefs would jitter at the edge of a platform. Pure
                    // tangent keeps constant distance from the plat centre;
                    // as the enemy moves the radial rotates and the tangent
                    // with it, so they naturally round the plat. The skirt
                    // deactivates once their raw seek probe no longer hits it.
                    sx = tx; sz = tz;
                    // If even the pure tangent side is blocked by another
                    // platform or a wall, flip to the opposite tangent so we
                    // at least try the other way round.
                    float qx = e->pos.x + sx*probe, qz = e->pos.z + sz*probe;
                    if (PlatBlocks(qx, qz, e->pos.y, r + 0.1f) || IsWall(qx, qz)) {
                        sx = -tx; sz = -tz;
                    }
                }
                // Wall skirt — if the probe 1.5m ahead along the seek direction
                // lands in a wall cell, rotate the seek progressively until we
                // find a clear direction. This handles both cases:
                //   1. enemy pressed against a wall (boss) — rotation finds a
                //      tangent direction that doesn't require walking into it
                //   2. wall sits between enemy and player — rotation discovers
                //      a path around the wall's corner.
                // Previously we pushed away from nearby walls, but that pointed
                // an enemy AWAY from the player when a wall was directly between
                // them (e.g. chef stuck south of a wall with player north).
                {
                    float wProbeX = e->pos.x + sx*probe;
                    float wProbeZ = e->pos.z + sz*probe;
                    if (IsWallCircle(wProbeX, wProbeZ, r + 0.1f)) {
                        int bias = ((i * 2654435761u) & 1) ? 1 : -1;
                        // Try rotating ±30°, ±60°, ±90°, ±120°; first clear direction wins
                        float angs[] = { 0.52f*bias, -0.52f*bias,
                                         1.05f*bias, -1.05f*bias,
                                         1.57f*bias, -1.57f*bias,
                                         2.09f*bias, -2.09f*bias };
                        for (int a = 0; a < 8; a++) {
                            float c = cosf(angs[a]), s = sinf(angs[a]);
                            float tx = sx*c - sz*s;
                            float tz = sx*s + sz*c;
                            float qx = e->pos.x + tx*probe, qz = e->pos.z + tz*probe;
                            if (!IsWallCircle(qx, qz, r + 0.1f)) {
                                sx = tx; sz = tz;
                                break;
                            }
                        }
                    }
                }
                EnemyMove(e, sx*e->speed*dt, sz*e->speed*dt);
                e->pos.y = PlatGroundAtR(e->pos.x, e->pos.z, e->pos.y + STEP_H, r);
            }
            // Only enter ATTACK when the player is on roughly the same level —
            // otherwise a chef on the floor next to a raised platform would
            // melee-loop at the wall while the player hovers above on the deck.
            // Forcing CHASE makes the enemy pathfind around to the stairs.
            else if (fabsf(g_p.pos.y - e->pos.y) <= STEP_H + 0.1f) e->state=ES_ATTACK;
        } else {
            // Same y-reachability check — if the player has jumped or climbed
            // onto a different level, break out of ATTACK and go back to CHASE
            // so the enemy resumes pathfinding.
            if (dist>e->atkR*1.5f ||
                fabsf(g_p.pos.y - e->pos.y) > STEP_H + 0.1f) e->state=ES_CHASE;
            if (e->cd<=0) {
                g_p.hp-=e->dmg; g_p.hurtFlash=0.22f; g_p.shake=fmaxf(g_p.shake,0.16f);
                PlaySound(g_sHurt); e->cd=e->rate;
                if (g_p.hp<=0){g_p.dead=true;g_gs=GS_DEAD;}
            }
        }
    }
}

static void DrawEnemies(Camera3D cam) {
    // Sort enemies by distance to camera (far first) so billboards blend correctly.
    // Without back-to-front, a live enemy drawn before a closer live enemy gets clipped
    // by the closer one's depth writes on transparent pixels.
    static int order[MAX_ENEMIES];
    static float dist[MAX_ENEMIES];
    int n = 0;
    for (int i = 0; i < g_ec; i++) {
        if (!g_e[i].active) continue;
        order[n] = i;
        float dx = g_e[i].pos.x - cam.position.x;
        float dz = g_e[i].pos.z - cam.position.z;
        dist[n] = dx*dx + dz*dz;
        n++;
    }
    // Insertion sort descending (small n, negligible cost)
    for (int i = 1; i < n; i++) {
        int oi = order[i]; float di = dist[i]; int j = i-1;
        while (j >= 0 && dist[j] < di) { order[j+1] = order[j]; dist[j+1] = dist[j]; j--; }
        order[j+1] = oi; dist[j+1] = di;
    }

    // Draw sprites with depth test ON but depth mask OFF so transparent pixels don't clip
    // subsequent billboards. Walls (already depth-written) still occlude enemies correctly.
    rlDrawRenderBatchActive();
    rlDisableDepthMask();

    for (int k = 0; k < n; k++) {
        Enemy *e = &g_e[order[k]];
        float bh=(e->type==1)?1.1f:(e->type==2)?0.72f:0.9f;

        // BOSS (type 3) — draws via ATR3 sprites, 4× the size of a chef
        if (e->type == 3 && g_bossOK) {
            float spriteH = 3.0f;  // smaller than before so HP bar sits near the head
            // e->pos.y is the enemy's feet: 0 on floor, platform-top when on a
            // platform. Centre the billboard at feet + spriteH/2 so he stands
            // ON the platform instead of sinking feet through its top surface.
            Vector3 pos = {e->pos.x, e->pos.y + spriteH*0.5f, e->pos.z};
            Texture2D tex;
            bool isCorpse = e->dying && g_bossDeathOK;
            if (isCorpse) {
                int df = (int)(e->deathT / BOSS_DEATH_FRAME_TIME);
                if (df > 3) df = 3;               // 4 death frames (G/H/I/J)
                tex = g_bossDeathTex[df];
            } else if (e->flashT > 0.f && g_bossPainOK) {
                tex = g_bossPainTex;
            } else {
                int frame = (int)(e->legT * 0.5f) % 4;
                if (frame < 0) frame += 4;
                tex = g_bossTex[frame];
            }
            // Boss writes depth for his opaque pixels so pickups/enemies behind
            // him don't bleed through. Flush the batch around the toggle.
            if (!isCorpse) { rlDrawRenderBatchActive(); rlEnableDepthMask(); }
            DrawBillboard(cam, tex, pos, spriteH, WHITE);
            if (!isCorpse) { rlDrawRenderBatchActive(); rlDisableDepthMask(); }
            // HP bar floats just above the head
            if (!e->dying && e->hp < e->maxHp) {
                float hp = e->hp / e->maxHp;
                float bary = e->pos.y + spriteH + 0.35f;
                Vector3 HF = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
                Vector3 HR = Vector3Normalize(Vector3CrossProduct(HF, cam.up));
                Vector3 HU = Vector3CrossProduct(HR, HF);
                const float W = 2.4f, H = 0.20f;
                Vector3 center = {e->pos.x, bary, e->pos.z};
                Vector3 bgTL = Vector3Add(center, Vector3Add(Vector3Scale(HR,-W*0.5f), Vector3Scale(HU, H*0.5f)));
                Vector3 bgTR = Vector3Add(center, Vector3Add(Vector3Scale(HR, W*0.5f), Vector3Scale(HU, H*0.5f)));
                Vector3 bgBR = Vector3Add(center, Vector3Add(Vector3Scale(HR, W*0.5f), Vector3Scale(HU,-H*0.5f)));
                Vector3 bgBL = Vector3Add(center, Vector3Add(Vector3Scale(HR,-W*0.5f), Vector3Scale(HU,-H*0.5f)));
                rlBegin(RL_TRIANGLES);
                rlColor4ub(25,25,25,250);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBL.x,bgBL.y,bgBL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z); rlVertex3f(bgTR.x,bgTR.y,bgTR.z);
                Vector3 fTR = Vector3Add(bgTL, Vector3Scale(HR, W*hp));
                Vector3 fBR = Vector3Add(bgBL, Vector3Scale(HR, W*hp));
                rlColor4ub(220,40,40,255);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBL.x,bgBL.y,bgBL.z); rlVertex3f(fBR.x,fBR.y,fBR.z);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(fBR.x,fBR.y,fBR.z); rlVertex3f(fTR.x,fTR.y,fTR.z);
                rlEnd();
            }
            continue;  // skip chef/fallback drawing for this enemy
        }

        if (g_chefOK) {
            // Size tuned per enemy type
            float spriteH = (e->type==1) ? 2.3f : (e->type==2) ? 1.7f : 2.0f;
            // Lift billboard to sit on the enemy's actual feet height (e->pos.y).
            // Was pinned to spriteH*0.5 which meant chefs walking on a platform
            // were drawn sunk into it (feet stuck at floor y=0).
            Vector3 pos = {e->pos.x, e->pos.y + spriteH*0.5f, e->pos.z};

            // Pick the sprite set for this chef type.
            // Defaults: type 0 = AFAB chef. type 1 = TORM. type 2 = SCH2.
            // Any set that fails to load falls back to the chef set.
            Texture2D *walkTex   = g_chefTex;
            Texture2D *deathTex  = g_chefDeathTex;
            Texture2D painTex    = g_chefPainTex;
            bool  walkOK = g_chefOK;
            bool  deathOK = g_chefDeathOK;
            bool  painOK = g_chefPainOK;
            if (e->type == 1 && g_tormOK) {
                walkTex  = g_tormTex;
                deathTex = g_tormDeathTex;
                painTex  = g_tormPainTex;
                walkOK   = g_tormOK;
                deathOK  = g_tormDeathOK;
                painOK   = g_tormPainOK;
            } else if (e->type == 2 && g_schOK) {
                walkTex  = g_schTex;
                deathTex = g_schDeathTex;
                painTex  = g_schPainTex;
                walkOK   = g_schOK;
                deathOK  = g_schDeathOK;
                painOK   = g_schPainOK;
            }
            (void)walkOK;

            Texture2D tex;
            bool isCorpse = e->dying && deathOK;
            if (isCorpse) {
                int df = (int)(e->deathT / CHEF_DEATH_FRAME_TIME);
                if (df > 3) df = 3;
                tex = deathTex[df];
            } else if (e->flashT > 0.f && painOK) {
                tex = painTex;
            } else {
                int frame = (int)(e->legT * 0.6f) % 4;
                if (frame < 0) frame += 4;
                tex = walkTex[frame];
            }
            DrawBillboard(cam, tex, pos, spriteH, WHITE);
            (void)isCorpse;
        } else {
            // Fallback cube mesh (unchanged)
            Color base=e->flashT>0?WHITE:ET_COL[e->type];
            Color dark={(unsigned char)(base.r/2),(unsigned char)(base.g/2),(unsigned char)(base.b/2),255};
            Color eye=ET_EYE[e->type];
            float lk=sinf(e->legT)*0.14f, ak=sinf(e->legT+1.57f)*0.09f;
            float dx=g_p.pos.x-e->pos.x, dz=g_p.pos.z-e->pos.z;
            float faceDeg=atan2f(-dx,-dz)*RAD2DEG;
            rlPushMatrix();
            rlTranslatef(e->pos.x, 0, e->pos.z);
            rlRotatef(faceDeg, 0, 1, 0);
            DrawCube((Vector3){-0.19f,0.22f, lk},0.22f,0.42f,0.22f,dark);
            DrawCube((Vector3){ 0.19f,0.22f,-lk},0.22f,0.42f,0.22f,dark);
            DrawCube((Vector3){0,bh/2.f+0.42f,0},0.72f,bh,0.52f,base);
            DrawCube((Vector3){-0.5f,bh*0.6f+ak,0},0.2f,0.52f,0.2f,dark);
            DrawCube((Vector3){ 0.5f,bh*0.6f-ak,0},0.2f,0.52f,0.2f,dark);
            DrawCube((Vector3){0,bh+0.67f,0},0.52f,0.46f,0.46f,base);
            DrawSphere((Vector3){-0.13f,bh+0.7f,-0.22f},0.075f,eye);
            DrawSphere((Vector3){ 0.13f,bh+0.7f,-0.22f},0.075f,eye);
            rlPopMatrix();
        }

        // HP bar — billboarded quads so they always face the camera (hidden once dying)
        if (!e->dying && e->hp<e->maxHp) {
            float hp = e->hp / e->maxHp;
            float bary = bh + 1.25f;
            Vector3 HF = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
            Vector3 HR = Vector3Normalize(Vector3CrossProduct(HF, cam.up));
            Vector3 HU = Vector3CrossProduct(HR, HF);
            const float W = 0.82f, H = 0.10f;
            Vector3 center = {e->pos.x, bary, e->pos.z};
            // Background (full width, dark)
            Vector3 bgTL = Vector3Add(center, Vector3Add(Vector3Scale(HR,-W*0.5f), Vector3Scale(HU, H*0.5f)));
            Vector3 bgTR = Vector3Add(center, Vector3Add(Vector3Scale(HR, W*0.5f), Vector3Scale(HU, H*0.5f)));
            Vector3 bgBR = Vector3Add(center, Vector3Add(Vector3Scale(HR, W*0.5f), Vector3Scale(HU,-H*0.5f)));
            Vector3 bgBL = Vector3Add(center, Vector3Add(Vector3Scale(HR,-W*0.5f), Vector3Scale(HU,-H*0.5f)));
            rlBegin(RL_TRIANGLES);
            rlColor4ub(30,30,30,240);
            rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBL.x,bgBL.y,bgBL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z);
            rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z); rlVertex3f(bgTR.x,bgTR.y,bgTR.z);
            // Fill (proportional width from the left edge)
            Vector3 fTL = bgTL;
            Vector3 fBL = bgBL;
            Vector3 fTR = Vector3Add(bgTL, Vector3Scale(HR, W*hp));
            Vector3 fBR = Vector3Add(bgBL, Vector3Scale(HR, W*hp));
            Color hc = hp>0.6f ? (Color){40,200,40,255} : hp>0.3f ? (Color){220,180,0,255} : (Color){220,30,30,255};
            rlColor4ub(hc.r,hc.g,hc.b,hc.a);
            rlVertex3f(fTL.x,fTL.y,fTL.z); rlVertex3f(fBL.x,fBL.y,fBL.z); rlVertex3f(fBR.x,fBR.y,fBR.z);
            rlVertex3f(fTL.x,fTL.y,fTL.z); rlVertex3f(fBR.x,fBR.y,fBR.z); rlVertex3f(fTR.x,fTR.y,fTR.z);
            rlEnd();
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

// ── BULLETS ──────────────────────────────────────────────────────────────────
static float RSphere(Vector3 ro, Vector3 rd, Vector3 sc, float sr) {
    Vector3 oc=Vector3Subtract(ro,sc);
    float b=Vector3DotProduct(oc,rd), c=Vector3DotProduct(oc,oc)-sr*sr, h=b*b-c;
    if (h<0) return -1; return -b-sqrtf(h);
}
static void FireBullet(Vector3 pos, Vector3 dir, float dmg, bool rocket) {
    for (int i=0;i<MAX_BULLETS;i++) if (!g_b[i].active) {
        g_b[i]=(Bullet){pos,Vector3Scale(dir,rocket?22.f:65.f),rocket?6.f:1.f,dmg,true,rocket};
        return;
    }
}
static void UpdBullets(float dt) {
    for (int i=0;i<MAX_BULLETS;i++) {
        Bullet *b=&g_b[i]; if (!b->active) continue;
        b->life-=dt; if (b->life<=0){b->active=false;continue;}
        Vector3 prev=b->pos;
        b->pos=Vector3Add(b->pos,Vector3Scale(b->vel,dt));
        if (IsWall(b->pos.x,b->pos.z)||b->pos.y>WALL_H||b->pos.y<0) {
            if (b->rocket) {
                Explode(b->pos);
                g_killsThisShot = 0;
                for (int j=0;j<g_ec;j++) {
                    if (!g_e[j].active || g_e[j].dying) continue;
                    float d=Vector3Distance(b->pos,g_e[j].pos);
                    if (d<5.f) DmgEnemy(j,200.f*(1.f-d/5.f));
                }
                if (g_killsThisShot>=2 && g_sMultiOK){ SetSoundVolume(g_sMulti,8.0f); PlaySound(g_sMulti); Msg("MULTI KILL!"); }
                float pd=Vector3Distance(b->pos,g_p.pos);
                if (pd<5.f){g_p.hp-=30.f*(1.f-pd/5.f);g_p.hurtFlash=0.3f;if(g_p.hp<=0){g_p.dead=true;g_gs=GS_DEAD;}}
            } else Sparks(prev,22);
            b->active=false; continue;
        }
        for (int j=0;j<g_ec;j++) {
            Enemy *e=&g_e[j]; if (!e->active || e->dying) continue;
            float _ex=b->pos.x-e->pos.x, _ez=b->pos.z-e->pos.z;
            float _xzdist=sqrtf(_ex*_ex+_ez*_ez);
            // Per-type hit volume — boss is much bigger than a chef
            float _bodyY   = (e->type==3) ? 2.0f : 1.0f;
            float _bodyR   = (e->type==3) ? 1.5f : 0.9f;
            float _bodyH   = (e->type==3) ? 2.3f : 1.2f;
            float _ydist=fabsf(b->pos.y-_bodyY);
            if ((_xzdist<_bodyR && _ydist<_bodyH) || Vector3Distance(b->pos,(Vector3){e->pos.x,_bodyY,e->pos.z})<_bodyR) {
                if (b->rocket) {
                    Explode(b->pos);
                    g_killsThisShot = 0;
                    for (int k=0;k<g_ec;k++){if(!g_e[k].active||g_e[k].dying)continue;float d=Vector3Distance(b->pos,g_e[k].pos);if(d<5.f)DmgEnemy(k,200.f*(1.f-d/5.f));}
                    if (g_killsThisShot>=2 && g_sMultiOK){ SetSoundVolume(g_sMulti,8.0f); PlaySound(g_sMulti); Msg("MULTI KILL!"); }
                    float pd=Vector3Distance(b->pos,g_p.pos);
                    if (pd<5.f){g_p.hp-=30.f*(1.f-pd/5.f);g_p.hurtFlash=0.3f;if(g_p.hp<=0){g_p.dead=true;g_gs=GS_DEAD;}}
                } else { Blood(b->pos, e->type==3 ? 40 : 6); DmgEnemy(j,b->dmg); }
                b->active=false; break;
            }
        }
    }
}
static void DrawBullets(void) {
    for (int i=0;i<MAX_BULLETS;i++) {
        Bullet *b=&g_b[i]; if (!b->active) continue;
        DrawSphere(b->pos,b->rocket?0.14f:0.05f,b->rocket?ORANGE:YELLOW);
        if (b->rocket) SpawnPart(b->pos,(Vector3){((float)rand()/RAND_MAX-.5f)*.4f,0,((float)rand()/RAND_MAX-.5f)*.4f},(Color){255,120,0,200},0.22f,0.08f,false);
    }
}

// ── PLAYER SHOOT ─────────────────────────────────────────────────────────────
static void Shoot(void) {
    if (g_needMouseRelease) return;  // swallow held click from menu/death screen
    if (g_p.shootCD>0) return;
    int w=g_p.weapon;
    if (w==0&&g_p.shells<=0){PlaySound(g_sEmpty);return;}
    if (w==1&&g_p.mgAmmo<=0){PlaySound(g_sEmpty);return;}
    if (w==2&&g_p.rockets<=0){PlaySound(g_sEmpty);return;}
    // Shotgun sound is deferred until after the pellet loop so we can make it
    // LOUDER when the shot didn't actually kill an enemy (no kill stinger plays).
    if (w==0){g_p.shells--;}
    else if (w==1){PlaySound(g_sMGOK ? g_sMG : g_sPistol); g_p.mgAmmo--;}
    else {PlaySound(g_sRocket);g_p.rockets--;}
    g_p.shootCD=WR[w]; g_p.kickAnim=0.18f; g_p.shake=fmaxf(g_p.shake,0.07f);
    // muzzle flash light
    float cy=cosf(g_p.pitch), sy=sinf(g_p.pitch);
    float syw=sinf(g_p.yaw+3.14159f), cyw=cosf(g_p.yaw+3.14159f);
    Vector3 fwd={syw*cy, sy, cyw*cy};
    Vector3 eyePos={g_p.pos.x,g_p.pos.y+EYE_H,g_p.pos.z};
    g_lights[6]=(LightDef){Vector3Add(eyePos,Vector3Scale(fwd,1.5f)),{1.f,0.85f,0.4f},8.f,1};
    ShaderSetLight(6);
    if (w==2){FireBullet(eyePos,fwd,200,true);return;}
    g_killsThisShot = 0;          // reset per-shot counter for multi-kill detection
    int pels=WPEL[w]; float sprd=(w==1)?0.10f:0.012f;
    for (int p=0;p<pels;p++) {
        Vector3 rd=fwd;
        if (pels>1){rd.x+=((float)rand()/RAND_MAX-.5f)*sprd*2;rd.y+=((float)rand()/RAND_MAX-.5f)*sprd;rd.z+=((float)rand()/RAND_MAX-.5f)*sprd*2;rd=Vector3Normalize(rd);}
        float best=1e9f; int bi=-1; bool headshot=false;
        for (int j=0;j<g_ec;j++){
            if (!g_e[j].active || g_e[j].dying) continue;
            // Per-type head position + hitbox sizes (boss is much bigger)
            float headY, headR, bodyY, bodyR;
            if (g_e[j].type == 3) {          // BOSS
                headY = 2.5f; headR = 0.45f;
                bodyY = 1.4f; bodyR = 0.95f;
            } else {
                float bh=(g_e[j].type==1)?1.1f:(g_e[j].type==2)?0.72f:0.9f;
                headY = bh + 0.67f; headR = 0.26f;
                bodyY = 0.85f;      bodyR = 0.68f;
            }
            // headY/bodyY are offsets above the enemy's FEET, so they track
            // the sprite when it rides up onto a platform (e->pos.y > 0).
            Vector3 hc={g_e[j].pos.x, g_e[j].pos.y + headY, g_e[j].pos.z};
            float ht=RSphere(eyePos,rd,hc,headR);
            if (ht>0&&ht<best){best=ht;bi=j;headshot=true;}
            Vector3 bc={g_e[j].pos.x, g_e[j].pos.y + bodyY, g_e[j].pos.z};
            float bt=RSphere(eyePos,rd,bc,bodyR);
            if (bt>0&&bt<best){best=bt;bi=j;headshot=false;}
        }
        if (bi>=0){
            Vector3 hp=Vector3Add(eyePos,Vector3Scale(rd,best));
            // Boss bleeds 4x as much as a chef per pellet — it's a big target
            int bloodCount = headshot ? 25 : 14;
            if (g_e[bi].type == 3) bloodCount *= 4;
            Blood(hp, bloodCount);
            float dmg=(float)WD[w]*(headshot?2.5f:1.0f);
            // Shotgun damage falloff curve:
            //   0m  → 2.5x (point-blank)
            //   6m  → 1.0x (normal)
            //   15m+→ 0.25x (floor — pellets barely tickle at long range)
            if (w==0) {
                if (best < 6.f)      dmg *= 1.f + (6.f - best) * 0.25f;
                else                 dmg *= fmaxf(0.25f, 1.f - (best - 6.f) * 0.083f);
            }
            g_lastHitHead = headshot;    // flag read inside KillEnemy so fatality sfx is skipped
            DmgEnemy(bi,dmg);
            g_lastHitHead = false;
            if (headshot && p==0) {
                Msg("HEADSHOT!");
                if (g_sHeadshotOK) { SetSoundVolume(g_sHeadshot, 1.5f); PlaySound(g_sHeadshot); }
            }
        }
        else{for(float t=0.5f;t<50.f;t+=0.5f){Vector3 pt=Vector3Add(eyePos,Vector3Scale(rd,t));if(IsWall(pt.x,pt.z)){Sparks(pt,18);break;}}}
    }
    // Shotgun blast SFX — always use the shotgun-kill stinger for every shot
    if (g_p.weapon == 0 && g_sShotgunKillOK) {
        SetSoundVolume(g_sShotgunKill, 1.0f);
        PlaySound(g_sShotgunKill);
    }
    // Multi-kill announcement (2+ enemies dropped by this shot)
    if (g_killsThisShot >= 2 && g_sMultiOK) {
        SetSoundVolume(g_sMulti, 8.0f);
        PlaySound(g_sMulti);
        Msg("MULTI KILL!");
    }
}

// ── WEAPON VIEWMODEL ─────────────────────────────────────────────────────────
// 2D screen-space weapon — always correct, no 3D rotation issues
// Draw an oriented box with world-space vertex positions computed from camera axes.
// bx/by/bz: offset from center along right/up/fwd.  hw/hh/hl: half-extents.
// Draw the current weapon's sprite over the 3D scene (2D overlay, post-EndMode3D).
// Frame[0] = idle.  Frame[1..count-1] = fire animation, played back during shootCD.
static void DrawSpriteWeapon(void) {
    int w = g_p.weapon;
    if (w < 0 || w >= 3) return;
    WepSprite *ws = &g_wep[w];
    if (!ws->loaded) return;

    // Pick frame index
    int frame = 0;
    if (g_p.shootCD > 0.f && ws->count > 1) {
        float prog = 1.f - (g_p.shootCD / WR[w]);        // 0→1 across the fire window
        if (prog < 0.f) prog = 0.f; if (prog > 1.f) prog = 1.f;
        int fireFrames = ws->count - 1;
        frame = 1 + (int)(prog * fireFrames);
        if (frame >= ws->count) frame = ws->count - 1;
    }

    Texture2D tex = ws->frames[frame];
    if (tex.id == 0) return;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    float kickY   = g_p.kickAnim  * 45.f;
    float bobY    = sinf(g_p.bobT) * 7.f;
    float switchY = g_p.switchAnim * 110.f;
    float dstW    = tex.width  * ws->scale;
    float dstH    = tex.height * ws->scale;
    float dstX    = sw * 0.5f - dstW * 0.5f + sw * ws->xShift;  // crosshair + per-weapon shift
    float dstY    = (float)sh - dstH + kickY + bobY + switchY + sh * ws->yShift;

    DrawTexturePro(tex,
        (Rectangle){0, 0, (float)tex.width, (float)tex.height},
        (Rectangle){dstX, dstY, dstW, dstH},
        (Vector2){0, 0}, 0.f, WHITE);

    // Muzzle-flash overlay: same dst rect, drawn only in first ~30% of the fire window
    if (ws->hasFlash && g_p.shootCD > WR[w] * 0.70f) {
        DrawTexturePro(ws->flash,
            (Rectangle){0, 0, (float)ws->flash.width, (float)ws->flash.height},
            (Rectangle){dstX, dstY, dstW, dstH},
            (Vector2){0, 0}, 0.f, WHITE);
    }
}

static void OBox(Vector3 c, float hw, float hh, float hl,
                  Vector3 R, Vector3 U, Vector3 F, Color col) {
    Vector3 v[8];
    for (int i=0;i<8;i++) {
        float sr=(i&1)?hw:-hw, su=(i&2)?hh:-hh, sf=(i&4)?hl:-hl;
        v[i]=(Vector3){c.x+R.x*sr+U.x*su+F.x*sf,
                       c.y+R.y*sr+U.y*su+F.y*sf,
                       c.z+R.z*sr+U.z*su+F.z*sf};
    }
    // 6 faces as quads (each split into 2 tris), winding chosen for visibility
    static const int qi[6][4]={{0,2,3,1},{4,5,7,6},{2,6,7,3},{0,1,5,4},{1,3,7,5},{0,4,6,2}};
    rlBegin(RL_TRIANGLES); rlColor4ub(col.r,col.g,col.b,col.a);
    for (int f=0;f<6;f++){
        const int*q=qi[f];
        rlVertex3f(v[q[0]].x,v[q[0]].y,v[q[0]].z);
        rlVertex3f(v[q[1]].x,v[q[1]].y,v[q[1]].z);
        rlVertex3f(v[q[2]].x,v[q[2]].y,v[q[2]].z);
        rlVertex3f(v[q[0]].x,v[q[0]].y,v[q[0]].z);
        rlVertex3f(v[q[2]].x,v[q[2]].y,v[q[2]].z);
        rlVertex3f(v[q[3]].x,v[q[3]].y,v[q[3]].z);
    }
    rlEnd();
}

static void DrawWeapon3D(Camera3D cam) {
    // Camera orientation vectors in world space — NO rlgl matrix tricks.
    // We compute vertex positions directly in world space so the view matrix
    // inside BeginMode3D correctly projects them without double-transforming.
    Vector3 F = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 R = Vector3Normalize(Vector3CrossProduct(F, cam.up));
    Vector3 U = Vector3CrossProduct(R, F);

    float kick  = g_p.kickAnim;
    float bob   = sinf(g_p.bobT) * 0.012f;
    float sw    = g_p.switchAnim;
    float sxOff = g_swayX / 300.f;
    float syOff = g_swayY / 300.f;

    // Anchor: camera position + local offsets (right/up/forward)
    // bz > 0 = further into scene (forward), bz < 0 = toward camera
    float ar = 0.28f + sxOff;
    float au = -0.20f - sw*0.15f + bob + syOff;
    float af = 0.45f - kick*0.08f;   // positive = forward into scene
    Vector3 anch = {
        cam.position.x + R.x*ar + U.x*au + F.x*af,
        cam.position.y + R.y*ar + U.y*au + F.y*af,
        cam.position.z + R.z*ar + U.z*au + F.z*af
    };

    // WBOX: offset from anchor by (bx along R, by along U, bz along F)
    #define WBOX(bx,by,bz,hw,hh,hl,col) do { \
        Vector3 _c={anch.x+R.x*(bx)+U.x*(by)+F.x*(bz), \
                    anch.y+R.y*(bx)+U.y*(by)+F.y*(bz), \
                    anch.z+R.z*(bx)+U.z*(by)+F.z*(bz)}; \
        OBox(_c,hw,hh,hl,R,U,F,col); \
    } while(0)

    Color dk={50,52,56,255}, md={82,84,90,255}, lt={115,118,126,255};
    Color wd={95,58,26,255}, wd2={72,44,18,255};
    int w = g_p.weapon;
    if (g_wep[w].loaded) return;  // sprite handles this weapon

    rlDisableDepthTest();
    rlDisableBackfaceCulling();

    if (w==0) { // PISTOL  (bz>0 = barrel direction = forward)
        WBOX( 0.0f, -0.05f,  0.0f, 0.06f,0.12f,0.06f, wd);   // grip
        WBOX( 0.0f,  0.01f,  0.06f, 0.05f,0.05f,0.16f, md);  // barrel
        WBOX( 0.0f,  0.03f,  0.04f, 0.045f,0.045f,0.12f, lt);// slide
        WBOX( 0.0f,  0.04f,  0.14f, 0.02f,0.015f,0.01f, dk); // front sight
    } else if (w==1) { // SHOTGUN
        WBOX( 0.0f,    0.0f,  -0.06f, 0.06f,0.07f,0.18f, wd);  // stock
        WBOX(-0.025f,  0.01f,  0.09f, 0.04f,0.04f,0.30f, dk);  // barrel L
        WBOX( 0.025f,  0.01f,  0.09f, 0.04f,0.04f,0.30f, dk);  // barrel R
        WBOX( 0.0f,    0.0f,   0.02f, 0.07f,0.06f,0.14f, wd2); // fore-end
    } else if (w==2) { // ROCKET
        WBOX( 0.0f,  0.0f,    0.02f, 0.08f,0.08f,0.38f, dk); // tube
        WBOX( 0.0f, -0.08f,  -0.05f, 0.05f,0.10f,0.06f, wd); // grip
        WBOX( 0.0f,  0.06f,   0.01f, 0.03f,0.03f,0.16f, md); // scope
    } else { // MACHINE GUN
        WBOX( 0.0f,   0.01f,  0.06f, 0.07f,0.06f,0.26f, dk); // receiver
        WBOX( 0.0f,   0.01f,  0.20f, 0.04f,0.04f,0.20f, md); // barrel
        WBOX( 0.0f,  -0.07f, -0.03f, 0.04f,0.09f,0.05f, wd); // grip
        WBOX( 0.09f, -0.01f,  0.01f, 0.09f,0.09f,0.07f, dk); // drum
    }

    // Muzzle flash
    if (g_p.shootCD > WR[w] - 0.07f) {
        float mz = (w==0)?0.16f:(w==1)?0.26f:(w==2)?0.22f:0.32f;
        Color fl={(unsigned char)255,(unsigned char)220,(unsigned char)80,(unsigned char)220};
        WBOX(0,0, mz+0.01f, 0.10f,0.10f,0.03f, fl);
        WBOX(0,0, mz+0.03f, 0.05f,0.05f,0.02f, WHITE);
    }

    #undef WBOX
    rlEnableBackfaceCulling();
    rlEnableDepthTest();
}

// ── HUD ──────────────────────────────────────────────────────────────────────
static void DrawHUD(void) {
    int sw=GetScreenWidth(), sh=GetScreenHeight();
    // hurt vignette
    if (g_p.hurtFlash>0) {
        int a=(int)(g_p.hurtFlash/0.22f*180);
        DrawRectangle(0,0,sw,sh,(Color){200,0,0,(unsigned char)a});
    }
    // scanlines (subtle)
    for (int y=0;y<sh;y+=3) DrawLine(0,y,sw,y,(Color){0,0,0,18});
    // crosshair — per-weapon texture override, else default tick marks
    // Rocket launcher: no crosshair (it's a rocket, you aim with the whole barrel)
    int cx=sw/2,cy=sh/2;
    int wpn=g_p.weapon;
    if (wpn == 2) {
        // no crosshair for rocket
    } else if (wpn>=0 && wpn<3 && g_xhair[wpn].id) {
        Texture2D xh = g_xhair[wpn];
        float xsc = 1.2f;
        float xw = xh.width*xsc, xh2 = xh.height*xsc;
        DrawTexturePro(xh,
            (Rectangle){0,0,(float)xh.width,(float)xh.height},
            (Rectangle){cx-xw/2, cy-xh2/2, xw, xh2},
            (Vector2){0,0}, 0.f, WHITE);
    } else {
        DrawRectangle(cx-14,cy-1,10,2,WHITE); DrawRectangle(cx+4,cy-1,10,2,WHITE);
        DrawRectangle(cx-1,cy-14,2,10,WHITE); DrawRectangle(cx-1,cy+4,2,10,WHITE);
    }
    // bottom panel bg
    DrawRectangle(0,sh-64,sw,64,(Color){8,8,12,220});
    DrawLine(0,sh-64,sw,sh-64,(Color){80,0,0,200});
    // health
    float hp=fmaxf(0.f,g_p.hp/g_p.maxHp);
    Color hcol=hp>0.5f?(Color){50,220,50,255}:hp>0.25f?(Color){220,180,0,255}:(Color){220,30,30,255};
    DrawText("HP",20,sh-56,12,(Color){180,180,180,255});
    DrawRectangle(20,sh-40,200,18,(Color){30,0,0,200});
    DrawRectangle(21,sh-39,(int)(198*hp),16,hcol);
    DrawRectangle(20,sh-40,200,18,(Color){80,80,80,80}); // border
    char hpBuf[16]; snprintf(hpBuf,16,"%d",(int)g_p.hp);
    DrawText(hpBuf,228,sh-43,18,hcol);
    // ammo
    char aBuf[16];
    if      (g_p.weapon==0) snprintf(aBuf,16,"%d",g_p.shells);
    else if (g_p.weapon==1) snprintf(aBuf,16,"%d",g_p.mgAmmo);
    else                    snprintf(aBuf,16,"%d",g_p.rockets);
    DrawText(WPN[g_p.weapon],sw-220,sh-58,13,SKYBLUE);
    DrawText(aBuf,sw-220,sh-46,36,YELLOW);
    // score / wave top right
    char sc[48]; snprintf(sc,48,"SCORE %d",g_p.score);
    DrawText(sc,sw-MeasureText(sc,18)-170,10,18,WHITE); // left of minimap
    char wv[24]; snprintf(wv,24,"WAVE %d",g_wave);
    DrawText(wv,12,10,18,(Color){255,160,40,255});
    char en[32]; snprintf(en,32,"ENEMIES: %d",Alive());
    DrawText(en,12,32,14,(Color){200,60,60,255});
    // kill counter bottom-center
    char kl[32]; snprintf(kl,32,"KILLS: %d",g_p.kills);
    int klw=MeasureText(kl,16);
    DrawRectangle(sw/2-klw/2-6,sh-36,klw+12,22,(Color){8,8,12,200});
    DrawText(kl,sw/2-klw/2,sh-33,16,(Color){255,80,80,255});
    // weapon hint
    DrawText("[ 1 ] SHOTGUN   [ 2 ] MACHINE GUN   [ 3 ] LAUNCHER",sw/2-175,sh-18,12,(Color){80,80,80,255});
    // kill msg
    if (g_msgT>0) {
        unsigned char a=(unsigned char)(255.f*fminf(1.f,g_msgT));
        int tw=MeasureText(g_msg,20);
        DrawText(g_msg,sw/2-tw/2,sh/3,20,(Color){255,100,100,a});
    }
    // Hype banner — bigger, longer, flashing. Used for "last chef" stinger.
    if (g_hypeT>0) {
        // Alpha: full until the last 0.9s, then linear fade out.
        float aF = fminf(1.f, g_hypeT/0.9f);
        // Color strobe yellow <-> white at ~5Hz for arcade-style flash.
        float t = (float)GetTime();
        bool onBeat = (sinf(t*31.4f) > 0.f);
        Color c = onBeat ? (Color){255,230, 40,(unsigned char)(255.f*aF)}
                         : (Color){255,255,255,(unsigned char)(255.f*aF)};
        // Scale pulse: 1.0..1.12 at ~3Hz for a bit of throb.
        float pulse = 1.f + 0.12f*(0.5f + 0.5f*sinf(t*19.0f));
        int fs = (int)(44.f*pulse + 0.5f);
        int tw = MeasureText(g_hypeMsg, fs);
        int tx = sw/2 - tw/2;
        int ty = sh/4;
        // Shadow first, then colour — cheap outline for readability over any bg.
        DrawText(g_hypeMsg, tx+3, ty+3, fs, (Color){0,0,0,(unsigned char)(200.f*aF)});
        DrawText(g_hypeMsg, tx,   ty,   fs, c);
    }
    // ── MINIMAP (player-centred, rotates so forward=up) ──────────────────────────
    {
        int cx2=sw-90, cy2=88;   // screen centre of minimap
        int radius=78;
        float scale=0.95f;       // world units per pixel — fits entire 120×80 map in the radar
        float yaw=g_p.yaw+3.14159f; // camera yaw: forward direction angle
        float cosY=cosf(yaw), sinY=sinf(yaw);

        // Clip circle background
        DrawCircle(cx2,cy2,radius+2,(Color){0,0,0,200});
        DrawCircle(cx2,cy2,radius,(Color){15,10,8,210});

        // Render every wall cell that fits in the circle
        // For each cell, compute its offset from player in world space,
        // then rotate by -yaw so player's forward maps to screen-up.
        float pw=g_p.pos.x, pz=g_p.pos.z;
        for (int r=0;r<ROWS;r++) for (int c=0;c<COLS;c++) {
            if (!MAP[r][c]) continue;
            // cell centre in world space
            float wx=c*CELL+CELL*0.5f - pw;
            float wz=r*CELL+CELL*0.5f - pz;
            // rotate: screen x = world right, screen y = world -forward (up=forward)
            float sx2= wz*sinY - wx*cosY;  // fixed: right-of-player direction (was mirrored)
            float sy2= wx*sinY + wz*cosY;  // y in "forward" direction (screen-up = positive)
            int px3=cx2+(int)(sx2/scale);
            int py3=cy2-(int)(sy2/scale);  // negate: up on screen = forward
            int cs=(int)(CELL/scale)+1;
            if (cs<2) cs=2;
            // Only draw if within circle
            float dx2=(float)(px3+cs/2-cx2), dy2=(float)(py3+cs/2-cy2);
            if (dx2*dx2+dy2*dy2 < (float)(radius*radius))
                DrawRectangle(px3,py3,cs,cs,(Color){80,50,35,230});
        }

        // Enemies (skip corpses on minimap)
        for (int i=0;i<g_ec;i++) {
            Enemy *e=&g_e[i]; if (!e->active || e->dying) continue;
            float wx=e->pos.x-pw, wz=e->pos.z-pz;
            float sx2= wz*sinY - wx*cosY;  // fixed: right-of-player direction (was mirrored)
            float sy2= wx*sinY + wz*cosY;
            int ex=cx2+(int)(sx2/scale);
            int ey=cy2-(int)(sy2/scale);
            float dd=(float)((ex-cx2)*(ex-cx2)+(ey-cy2)*(ey-cy2));
            if (dd<(float)(radius*radius)){
                Color ec2=ET_COL[e->type]; ec2.a=240;
                DrawCircle(ex,ey,4,ec2);
            }
        }

        // Player dot + forward triangle (always at centre, pointing up)
        DrawCircle(cx2,cy2,5,WHITE);
        DrawTriangle((Vector2){(float)cx2,(float)(cy2-10)},
                     (Vector2){(float)(cx2-5),(float)(cy2+2)},
                     (Vector2){(float)(cx2+5),(float)(cy2+2)}, YELLOW);

        // Border ring
        DrawCircleLines(cx2,cy2,radius+1,(Color){100,80,60,180});
        DrawText("N",cx2-4,cy2-radius-14,12,(Color){180,180,180,200});
    }
    // Ensure cursor stays hidden during gameplay — call every frame AND park
    // the cursor at screen centre so even if macOS briefly shows it on alt-tab,
    // it's forced off the crosshair region and then re-hidden.
    HideCursor();
    DisableCursor();
    SetMousePosition(GetScreenWidth()/2, GetScreenHeight()/2);
}

// ── PLAYER ───────────────────────────────────────────────────────────────────
static void UpdPlayer(float dt, Camera3D *cam) {
    if (g_p.dead) return;
    // --- MOUSE LOOK ---
    // Native: the SetMousePosition re-center trick dodges some macOS GLFW
    //         mouse-delta accumulation quirks, so keep it.
    // Web:    browsers can't warp the OS cursor for security reasons, so
    //         SetMousePosition is a no-op and the re-centered delta is
    //         always zero. Under Pointer Lock (engaged by DisableCursor
    //         after a user gesture) GetMouseDelta returns the raw movement
    //         delivered by the browser's pointer-lock event — use it.
#ifdef __EMSCRIPTEN__
    Vector2 md = GetMouseDelta();
    float mdx = md.x, mdy = md.y;
#else
    int cx2=GetScreenWidth()/2, cy2=GetScreenHeight()/2;
    Vector2 mp=GetMousePosition();
    float mdx=mp.x-(float)cx2, mdy=mp.y-(float)cy2;
    SetMousePosition(cx2,cy2);
#endif
    g_p.yaw   -= mdx*SENS;
    g_p.pitch  = Clamp(g_p.pitch - mdy*SENS, -MAX_PITCH, MAX_PITCH);
    // weapon sway — gun lags behind mouse movement (inertia feel)
    float tswX = Clamp(-mdx*0.45f, -30.f, 30.f);
    float tswY = Clamp(-mdy*0.30f, -20.f, 20.f);
    g_swayX += (tswX - g_swayX) * 0.18f;
    g_swayY += (tswY - g_swayY) * 0.18f;

    // --- MOVE ---
    bool sprint=IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);
    float spd=SPEED*(sprint?1.65f:1.f)*dt;
    float sy=sinf(g_p.yaw+3.14159f), cy=cosf(g_p.yaw+3.14159f);
    float mx=0,mz=0;
    if (IsKeyDown(KEY_W)||IsKeyDown(KEY_UP))    {mx+=sy;mz+=cy;}
    if (IsKeyDown(KEY_S)||IsKeyDown(KEY_DOWN))  {mx-=sy;mz-=cy;}
    if (IsKeyDown(KEY_A)||IsKeyDown(KEY_LEFT))  {mx+=cy;mz-=sy;}
    if (IsKeyDown(KEY_D)||IsKeyDown(KEY_RIGHT)) {mx-=cy;mz+=sy;}
    float mlen=sqrtf(mx*mx+mz*mz); if(mlen>0){mx/=mlen;mz/=mlen;}
    // Per-axis movement with wall + platform blocking. Tall platforms block,
    // stairs (within STEP_H) let you pass and the step-up happens in gravity.
    {
        float dx = mx * spd, dz = mz * spd;
        float nx = g_p.pos.x + dx;
        if (!IsWall(nx+(dx>=0?PRAD:-PRAD),g_p.pos.z) &&
            !IsWall(nx+(dx>=0?PRAD:-PRAD),g_p.pos.z+PRAD) &&
            !IsWall(nx+(dx>=0?PRAD:-PRAD),g_p.pos.z-PRAD) &&
            !PlatBlocks(nx, g_p.pos.z, g_p.pos.y, PRAD)) g_p.pos.x = nx;
        float nz = g_p.pos.z + dz;
        if (!IsWall(g_p.pos.x,nz+(dz>=0?PRAD:-PRAD)) &&
            !IsWall(g_p.pos.x+PRAD,nz+(dz>=0?PRAD:-PRAD)) &&
            !IsWall(g_p.pos.x-PRAD,nz+(dz>=0?PRAD:-PRAD)) &&
            !PlatBlocks(g_p.pos.x, nz, g_p.pos.y, PRAD)) g_p.pos.z = nz;
    }
    // Auto step-up onto low platforms as player walks onto them
    if (g_p.onGround) {
        float groundHere = PlatGroundAt(g_p.pos.x, g_p.pos.z, g_p.pos.y + STEP_H);
        if (groundHere > g_p.pos.y && groundHere <= g_p.pos.y + STEP_H) {
            g_p.pos.y = groundHere;
            g_p.velY = 0.f;
        }
    }

    // Push player out of any live enemies so you can't walk through them
    for (int i = 0; i < g_ec; i++) {
        Enemy *e = &g_e[i];
        if (!e->active || e->dying) continue;
        float edx = g_p.pos.x - e->pos.x;
        float edz = g_p.pos.z - e->pos.z;
        float d2 = edx*edx + edz*edz;
        float ENEMY_RADIUS = (e->type == 3) ? 0.95f : 0.45f;  // boss is bigger
        float minD = PRAD + ENEMY_RADIUS;
        if (d2 > 0.0001f && d2 < minD*minD) {
            float d = sqrtf(d2);
            float push = minD - d;
            g_p.pos.x += (edx/d) * push;
            g_p.pos.z += (edz/d) * push;
        }
    }

    // jump / gravity with platform-aware ground
    if (IsKeyPressed(KEY_SPACE)&&g_p.onGround){g_p.velY=JUMP;g_p.onGround=false;}
    g_p.velY+=GRAV*dt; g_p.pos.y+=g_p.velY*dt;
    float ground = PlatGroundAt(g_p.pos.x, g_p.pos.z, g_p.pos.y);
    if (g_p.pos.y <= ground) {
        g_p.pos.y = ground; g_p.velY = 0; g_p.onGround = true;
    } else {
        g_p.onGround = false;
    }

    // weapon switch
    if (IsKeyPressed(KEY_ONE)  &&g_p.weapon!=0){g_p.weapon=0;g_p.switchAnim=0.3f;}
    if (IsKeyPressed(KEY_TWO)  &&g_p.weapon!=1){g_p.weapon=1;g_p.switchAnim=0.3f;}
    if (IsKeyPressed(KEY_THREE)&&g_p.weapon!=2){g_p.weapon=2;g_p.switchAnim=0.3f;}

    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_needMouseRelease = false;
    else if (!g_needMouseRelease) Shoot();

    // timers
    if (g_p.shootCD>0)   g_p.shootCD-=dt;
    if (g_p.kickAnim>0)  g_p.kickAnim=fmaxf(0,g_p.kickAnim-dt*6.f);
    if (g_p.hurtFlash>0) g_p.hurtFlash-=dt;
    if (g_p.switchAnim>0)g_p.switchAnim=fmaxf(0,g_p.switchAnim-dt*5.f);
    if (g_p.shake>0)     g_p.shake=fmaxf(0,g_p.shake-dt*4.f);
    if (g_msgT>0)        g_msgT-=dt;
    if (g_hypeT>0)       g_hypeT-=dt;
    bool moving=(mlen>0)&&g_p.onGround;
    if (moving) g_p.bobT+=dt*(sprint?10.f:7.f);

    // fade muzzle flash light
    if (g_lights[6].enabled) {
        g_lights[6].radius-=dt*40.f;
        if (g_lights[6].radius<=0){g_lights[6].enabled=0;g_lights[6].radius=0;}
        ShaderSetLight(6);
    }
    if (g_lights[7].enabled) {
        g_lights[7].radius-=dt*30.f;
        if (g_lights[7].radius<=0){g_lights[7].enabled=0;g_lights[7].radius=0;}
        ShaderSetLight(7);
    }

    // build camera
    float shx=0,shy=0;
    if (g_p.shake>0){shx=((float)rand()/RAND_MAX-.5f)*g_p.shake*0.04f;shy=((float)rand()/RAND_MAX-.5f)*g_p.shake*0.04f;}
    float eyeY=g_p.pos.y+EYE_H+sinf(g_p.bobT)*0.02f*(moving?1.f:0.f);
    cam->position=(Vector3){g_p.pos.x+shx,eyeY+shy,g_p.pos.z};
    float pc=cosf(g_p.pitch),ps=sinf(g_p.pitch);
    float yw=g_p.yaw+3.14159f;
    cam->target=(Vector3){cam->position.x+sinf(yw)*pc,cam->position.y+ps,cam->position.z+cosf(yw)*pc};
    cam->up=(Vector3){0,1,0};
    // update shader viewPos
    SetShaderValue(g_shader,u_viewPos,&cam->position,SHADER_UNIFORM_VEC3);
    // flicker scene lights
    float flicker=1.f+sinf(GetTime()*7.3f)*0.04f+sinf(GetTime()*13.1f)*0.02f;
    for (int i=0;i<6;i++) {
        Vector3 fc={g_lights[i].color.x*flicker,g_lights[i].color.y*flicker,g_lights[i].color.z*flicker};
        SetShaderValue(g_shader,u_lColor[i],&fc,SHADER_UNIFORM_VEC3);
    }
}

// ── INIT ─────────────────────────────────────────────────────────────────────
static void InitGame(void) {
    srand((unsigned)time(NULL));
    memset(&g_p,0,sizeof(g_p));
    g_p.pos=(Vector3){1.5f*CELL,0,1.5f*CELL};
    g_p.hp=g_p.maxHp=100; g_p.shells=32; g_p.rockets=8; g_p.mgAmmo=120; g_p.weapon=0;
    g_wave=1; g_ec=0; g_pkc=0;
    memset(g_e,0,sizeof(g_e)); memset(g_b,0,sizeof(g_b));
    memset(g_pt,0,sizeof(g_pt)); memset(g_pk,0,sizeof(g_pk));
    SeedPicks();

    if (g_bossMode) {
        // BOSS TEST: spawn one boss straight away, no chefs. Drop him in a
        // random open map corner (same helper as the wave-end + respawn paths)
        // — the old hardcoded (56, 36) put his body into a wall corner so he
        // couldn't move.
        Enemy *ne = &g_e[g_ec++];
        *ne = (Enemy){0};
        float bx, bz; PickBossSpawn(&bx, &bz);
        ne->pos = (Vector3){bx, PlatGroundAt(bx, bz, 100.f), bz};
        ne->type = 3;                           // boss
        ne->state = ES_PATROL;
        ne->hp = ne->maxHp = ET_HP[3];
        ne->speed = ET_SPD[3];
        ne->dmg = ET_DMG[3];
        ne->rate = ne->cd = ET_RATE[3];
        ne->alertR = ET_AR[3];
        ne->atkR = ET_ATK[3];
        ne->score = ET_SC[3];
        ne->active = true;
        float ang = (float)rand()/RAND_MAX * 6.28f;
        ne->pd = (Vector3){sinf(ang), 0, cosf(ang)};
        ne->stateT = 1.f + (float)rand()/RAND_MAX * 2.f;
    } else {
        // Normal mode: spawn wave 1 (16 chefs)
        for (int k=0;k<16&&g_ec<MAX_ENEMIES;k++) {
            for (int tries=0;tries<120;tries++) {
                int r=1+rand()%(ROWS-2), c=1+rand()%(COLS-2);
                if (MAP[r][c]) continue;
                float wx=c*CELL+CELL/2.f, wz=r*CELL+CELL/2.f;
                if (hypotf(wx-g_p.pos.x,wz-g_p.pos.z)<10.f) continue;
                // Wave 1 mix: 70% regular chef, 20% fast, 10% heavy
                float rng = (float)rand()/RAND_MAX;
                int type = rng < 0.70f ? 0 : rng < 0.90f ? 2 : 1;
                Enemy *ne=&g_e[g_ec++]; *ne=(Enemy){0};
                ne->pos=(Vector3){wx, PlatGroundAt(wx,wz,100.f), wz};
                ne->type=type; ne->state=ES_PATROL;
                ne->hp=ne->maxHp=ET_HP[type]; ne->speed=ET_SPD[type];
                ne->dmg=ET_DMG[type]; ne->rate=ne->cd=ET_RATE[type];
                ne->alertR=ET_AR[type]; ne->atkR=ET_ATK[type]; ne->score=ET_SC[type]; ne->active=true;
                float ang=(float)rand()/RAND_MAX*6.28f; ne->pd=(Vector3){sinf(ang),0,cosf(ang)};
                ne->stateT=1.f+(float)rand()/RAND_MAX*2.f;
                break;
            }
        }
    }
    g_gs=GS_PLAY;
    HideCursor();
    SetMousePosition(GetScreenWidth()/2,GetScreenHeight()/2);
    // Block firing until mouse released — stops menu-click from triggering a shot
    g_needMouseRelease = true;
    g_hadVisibleEnemy = false;  // reset so the first spotted enemy triggers the stinger
    g_bossInterlude = false;    // not in a boss fight yet
    strcpy(g_msg,""); g_msgT=0;
    g_hypeMsg[0]=0; g_hypeT=0; g_hypeDur=0;
}

// File-scope camera + per-frame step. `g_cam` needs to outlive a single main()
// invocation because on the web build emscripten_set_main_loop keeps calling
// StepFrame after main has returned (the while-loop model doesn't work in a
// single-threaded browser context).
static Camera3D g_cam;

static void StepFrame(void) {
    float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;
    DebugLogTick();
    if (g_musicOK) {
        UpdateMusicStream(g_music);   // feed the streaming decoder
        // Music volume: - / + (and numpad equivalents)
        bool vDown = IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT);
        bool vUp   = IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD);
        if (vDown) g_musicVol = fmaxf(0.f,  g_musicVol - 0.1f);
        if (vUp)   g_musicVol = fminf(1.5f, g_musicVol + 0.1f);
        if (vDown || vUp) {
            SetMusicVolume(g_music, g_musicVol);
            SaveMusicVol();  // persist to disk
            char buf[48]; snprintf(buf, 48, "MUSIC VOL %d%%", (int)(g_musicVol * 100.f + 0.5f));
            Msg(buf);
        }
    }

    if (g_gs==GS_MENU) {
        if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)||IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            g_bossMode = false;
            InitGame();
        }
        if (IsKeyPressed(KEY_B)) {
            g_bossMode = true;
            InitGame();
        }
    } else if (g_gs==GS_PLAY) {
        if (IsKeyPressed(KEY_ESCAPE)){g_gs=GS_MENU;}
        UpdPlayer(dt,&g_cam);
        UpdEnemies(dt);
        UpdBullets(dt);
        UpdParts(dt);
        UpdPicks();

        // "Distant enemy" stinger — plays once when the first enemy comes into sight
        // after a period of having none visible. Uses dot-product FOV cone + wall raycast.
        {
            bool anyVisible = false;
            float cy = cosf(g_p.pitch);
            float syw = sinf(g_p.yaw + 3.14159f), cyw = cosf(g_p.yaw + 3.14159f);
            float fx = syw * cy, fz = cyw * cy;     // camera forward XZ (normalized)
            for (int i = 0; i < g_ec && !anyVisible; i++) {
                Enemy *e = &g_e[i];
                if (!e->active || e->dying) continue;
                float dx = e->pos.x - g_p.pos.x;
                float dz = e->pos.z - g_p.pos.z;
                float dist = sqrtf(dx*dx + dz*dz);
                if (dist < 0.1f || dist > 50.f) continue;   // too close / too far
                float dot = (dx*fx + dz*fz) / dist;          // cone: >= 0.6 ~= 53° half-FOV
                if (dot < 0.6f) continue;
                // Simple raycast: step along the line of sight, bail if we hit a wall
                bool blocked = false;
                int steps = (int)(dist / 0.5f);
                for (int s = 1; s < steps; s++) {
                    float t = s / (float)steps;
                    if (IsWall(g_p.pos.x + dx*t, g_p.pos.z + dz*t)) { blocked = true; break; }
                }
                if (!blocked) anyVisible = true;
            }
            if (anyVisible && !g_hadVisibleEnemy && g_sEnemyAlertCount > 0) {
                // 10-second cooldown — otherwise a sprinkle of line-of-sight
                // flips (chef rounding a corner, re-emerging) would stack
                // stingers on top of each other.
                static double lastAlertT = -1000.0;
                const double ALERT_COOLDOWN = 10.0;
                double now = GetTime();
                bool anyPlaying = false;
                for (int a = 0; a < g_sEnemyAlertCount; a++) {
                    if (g_sEnemyAlertOK[a] && IsSoundPlaying(g_sEnemyAlert[a])) {
                        anyPlaying = true; break;
                    }
                }
                if (!anyPlaying && (now - lastAlertT) >= ALERT_COOLDOWN) {
                    // Pick a random loaded alert stinger
                    int pick = rand() % g_sEnemyAlertCount;
                    for (int tries = 0; tries < g_sEnemyAlertCount; tries++) {
                        int a = (pick + tries) % g_sEnemyAlertCount;
                        if (g_sEnemyAlertOK[a]) {
                            SetSoundVolume(g_sEnemyAlert[a], 2.0f);
                            PlaySound(g_sEnemyAlert[a]);
                            lastAlertT = now;
                            break;
                        }
                    }
                }
            }
            g_hadVisibleEnemy = anyVisible;
        }
    } else if (g_gs==GS_DEAD) {
        if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)) InitGame();
    }

    BeginDrawing();
    ClearBackground((Color){4,3,6,255});

    if (g_gs==GS_PLAY||g_gs==GS_DEAD) {
        BeginMode3D(g_cam);
        DrawModel(g_wallModel,Vector3Zero(),1.f,WHITE);
        DrawModel(g_floorModel,Vector3Zero(),1.f,(Color){110,110,110,255});
        DrawModel(g_ceilModel,Vector3Zero(),1.f,(Color){70,70,90,255});
        // Platforms (stairs + raised decks) — unit cube mesh scaled per-platform
        for (int pi = 0; pi < g_platCount; pi++) {
            Platform *p = &g_plats[pi];
            float sx = p->x1 - p->x0;
            float sy = p->top;
            float sz = p->z1 - p->z0;
            Vector3 center = { (p->x0+p->x1)*0.5f, sy*0.5f, (p->z0+p->z1)*0.5f };
            DrawModelEx(g_platModel, center, (Vector3){0,1,0}, 0.f,
                        (Vector3){sx, sy, sz}, (Color){170,140,110,255});
        }
        DrawEnemies(g_cam);
        DrawBullets();
        DrawParts();
        DrawPicks(g_cam);
        // Ceiling lights drawn LAST so the additive glow shines over enemies,
        // pickups, and bullets — otherwise opaque billboards cover the light cones.
        DrawCeilingLights(g_cam);
        DrawWeapon3D(g_cam);
        EndMode3D();
        DrawSpriteWeapon();
        DrawHUD();
    }

    if (g_gs==GS_MENU) {
        int sw2=GetScreenWidth(),sh2=GetScreenHeight();
        ClearBackground(BLACK);
        // grid lines for style
        for (int x=0;x<sw2;x+=60) DrawLine(x,0,x,sh2,(Color){20,0,0,80});
        for (int y=0;y<sh2;y+=60) DrawLine(0,y,sw2,y,(Color){20,0,0,80});
        const char *t1="IRON FIST 3D";
        int tw=MeasureText(t1,80);
        DrawText(t1,sw2/2-tw/2+3,sh2/3+3,80,(Color){100,0,0,255});
        DrawText(t1,sw2/2-tw/2,sh2/3,80,RED);
        const char *t2="S L A U G H T E R  S T Y L E";
        DrawText(t2,sw2/2-MeasureText(t2,16)/2,sh2/3+96,16,(Color){120,120,120,255});
        const char *ctrl="WASD / ARROWS - MOVE     MOUSE - LOOK     LMB - FIRE\n"
                         "SPACE - JUMP     SHIFT - SPRINT     - / + - MUSIC VOL\n"
                         "1 - SHOTGUN     2 - MACHINE GUN     3 - LAUNCHER";
        DrawText(ctrl,sw2/2-MeasureText("WASD / ARROWS - MOVE     MOUSE - LOOK     LMB - FIRE",15)/2,sh2/2+20,15,(Color){90,90,90,255});
        const char *st="[ ENTER  /  CLICK  TO  START ]";
        if (sinf(GetTime()*3.f)>0)
            DrawText(st,sw2/2-MeasureText(st,22)/2,sh2*3/4,22,RED);
        const char *bt="[ B FOR BOSS TEST ]";
        DrawText(bt,sw2/2-MeasureText(bt,16)/2,sh2*3/4+36,16,(Color){200,120,120,255});
        DrawFPS(10,10);
    } else if (g_gs==GS_DEAD) {
        int sw2=GetScreenWidth(),sh2=GetScreenHeight();
        DrawRectangle(0,0,sw2,sh2,(Color){100,0,0,130});
        const char *d="YOU DIED";
        DrawText(d,sw2/2-MeasureText(d,88)/2+4,sh2/3+4,88,(Color){80,0,0,255});
        DrawText(d,sw2/2-MeasureText(d,88)/2,sh2/3,88,RED);
        char sc2[64]; snprintf(sc2,64,"SCORE: %d     WAVE: %d",g_p.score,g_wave);
        DrawText(sc2,sw2/2-MeasureText(sc2,24)/2,sh2/2+10,24,YELLOW);
        if (sinf(GetTime()*3.f)>0)
            DrawText("[ ENTER  /  SPACE  TO  PLAY  AGAIN ]",sw2/2-190,sh2*2/3,20,WHITE);
    }
    if (g_gs==GS_PLAY) DrawFPS(GetScreenWidth()-58,GetScreenHeight()-18);
    EndDrawing();
}

// ── MAIN ─────────────────────────────────────────────────────────────────────
int main(int argc, char **argv) {
#ifdef _WIN32
    ExtractBundle();  // unpack embedded sprites/sounds to %TEMP%/IronFist3D/
#endif
    // Opt-in debug log — fresh file each run, 5 Hz tick snapshot of player +
    // enemy state. Enable with --debug; tail /tmp/ironfist-debug.log via
    // debug.sh in another terminal.
    bool debugMode = false;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--debug") == 0) debugMode = true;
    }
    if (debugMode) {
        // "w" truncates any existing file, but be explicit — a prior crashed
        // run could leave stale content, and seeing a fresh header confirms
        // this launch owns the log.
        g_dbgLog = fopen(DEBUG_LOG_PATH, "w");
        if (g_dbgLog) {
            fprintf(g_dbgLog, "# Iron Fist 3D debug log — new session\n\n");
            fflush(g_dbgLog);
        }
    }
    // FLAG_WINDOW_RESIZABLE so users can drag to any size; every draw call
    // pulls GetScreenWidth/Height live so the HUD and minimap re-lay out
    // automatically. SW/SH stays as the fallback/initial size.
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(SW,SH,"IRON FIST 3D");
    // Default to maximised on native desktop launches — on high-DPI Windows
    // monitors 1280x720 looks tiny otherwise. Skipped on web: the Emscripten
    // shell owns canvas sizing.
#ifndef PLATFORM_WEB
    MaximizeWindow();
#endif
    HideCursor();
    // Generate iron fist icon
    {
        Image ico = GenImageColor(64,64,(Color){20,0,0,255});
        ImageDrawCircle(&ico,32,32,30,(Color){180,0,0,255});
        // fist knuckles
        for (int k=0;k<4;k++) ImageDrawCircle(&ico,14+k*13,22,8,(Color){220,180,120,255});
        // thumb
        ImageDrawCircle(&ico,10,32,7,(Color){220,180,120,255});
        // palm
        ImageDrawRectangle(&ico,12,28,38,22,(Color){220,180,120,255});
        // shadow/detail
        ImageDrawRectangle(&ico,13,38,36,3,(Color){160,120,80,255});
        SetWindowIcon(ico);
        UnloadImage(ico);
    }
    SetTargetFPS(FPS);
    InitAudioDevice();
    srand(42);
    g_sPistol  = MkGun(2.0f,0.15f,0.7f);
    g_sShotgun = MkGun(0.8f,0.28f,0.9f);
    g_sRocket  = MkGun(0.5f,0.18f,0.6f);
    g_sExplode = MkBoom();
    g_sHurt    = MkHurt();
    g_sPickup  = MkPickup();
    g_sEmpty   = MkEmpty();
    g_sDie     = MkDie();
    // Load announcer SFX (UT headshot, MK fatality) from bundle Resources
    {
        char fp[700];
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/headshot.mp3", AppDir());
        g_sHeadshot = LoadSound(fp);
        g_sHeadshotOK = (g_sHeadshot.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/fatality.mp3", AppDir());
        g_sFatality = LoadSound(fp);
        g_sFatalityOK = (g_sFatality.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/holy-shit.mp3", AppDir());
        g_sMulti = LoadSound(fp);
        g_sMultiOK = (g_sMulti.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/first-blood.mp3", AppDir());
        g_sFirstBlood = LoadSound(fp);
        g_sFirstBloodOK = (g_sFirstBlood.frameCount > 0);

        static const char *ENEMY_ALERT_FILES[] = {
            "sounds/distant-enemy.mp3",
            "sounds/bombin-alert.mp3",
            "sounds/scary-alert.mp3",
        };
        int na = (int)(sizeof(ENEMY_ALERT_FILES)/sizeof(ENEMY_ALERT_FILES[0]));
        if (na > ENEMY_ALERT_MAX) na = ENEMY_ALERT_MAX;
        for (int a = 0; a < na; a++) {
            snprintf(fp, sizeof(fp), "%s" RES_PREFIX "%s", AppDir(), ENEMY_ALERT_FILES[a]);
            g_sEnemyAlert[a] = LoadSound(fp);
            g_sEnemyAlertOK[a] = (g_sEnemyAlert[a].frameCount > 0);
        }
        g_sEnemyAlertCount = na;

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/shotgun-kill.mp3", AppDir());
        g_sShotgunKill = LoadSound(fp);
        g_sShotgunKillOK = (g_sShotgunKill.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/next-wave.mp3", AppDir());
        g_sNextWave = LoadSound(fp);
        g_sNextWaveOK = (g_sNextWave.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/mg-sound.mp3", AppDir());
        g_sMG = LoadSound(fp);
        g_sMGOK = (g_sMG.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/health-pickup.mp3", AppDir());
        g_sHealthPickup = LoadSound(fp);
        g_sHealthPickupOK = (g_sHealthPickup.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/mg-ammo-pickup.mp3", AppDir());
        g_sMGPickup = LoadSound(fp);
        g_sMGPickupOK = (g_sMGPickup.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/one-left.mp3", AppDir());
        g_sOneLeft = LoadSound(fp);
        g_sOneLeftOK = (g_sOneLeft.frameCount > 0);

        // Real rocket launcher firing sound (overrides procedural g_sRocket)
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/launcher-shot.mp3", AppDir());
        Sound realLauncher = LoadSound(fp);
        if (realLauncher.frameCount > 0) {
            UnloadSound(g_sRocket);
            g_sRocket = realLauncher;
        }

        // Real Doom shotgun sound (overrides the procedural one)
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/shotgun.mp3", AppDir());
        Sound realShotgun = LoadSound(fp);
        if (realShotgun.frameCount > 0) {
            UnloadSound(g_sShotgun);
            g_sShotgun = realShotgun;
        }

        // Real rocket-hit explosion sound (overrides procedural g_sExplode)
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/rocket-hit.mp3", AppDir());
        Sound realExplode = LoadSound(fp);
        if (realExplode.frameCount > 0) {
            UnloadSound(g_sExplode);
            g_sExplode = realExplode;
        }

        // Real chef death screams — multiple variants played randomly
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/chef-die.mp3", AppDir());
        Sound realDie = LoadSound(fp);
        if (realDie.frameCount > 0) {
            UnloadSound(g_sDie);
            g_sDie = realDie;
        }
        // chef-die-1..N variants
        for (int i = 0; i < CHEF_DIE_ALT_COUNT; i++) {
            snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/chef-die-%d.mp3", AppDir(), i+1);
            g_sDieAlt[i] = LoadSound(fp);
            g_sDieAltOK[i] = (g_sDieAlt[i].frameCount > 0);
        }

        // Streamed background music (C&C Red Alert — Hell March)
        LoadMusicVol();  // restore user's last-saved volume from ~/.ironfist3d.cfg
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/hell-march.mp3", AppDir());
        g_music = LoadMusicStream(fp);
        g_musicOK = (g_music.frameCount > 0);
        if (g_musicOK) {
            g_music.looping = true;
            SetMusicVolume(g_music, g_musicVol);  // under the gunfire / sfx
            PlayMusicStream(g_music);
        }
    }
    srand((unsigned)time(NULL));

    InitShader();

    // Load WolfenDoom weapon sprite viewmodels from bundle Resources.
    // Each entry: folder, file list (idle first, then fire frames), scale, count.
    {
        char appBase[512];
        snprintf(appBase, sizeof(appBase), "%s" RES_PREFIX "sprites/", AppDir());

        struct { int wepIdx; const char *folder; const char *files[MAX_WFRAMES]; int n; float scale; } packs[] = {
            // 0 SHOTGUN (browning): GA idle then GE muzzle flash first, then GB/GC/GD recover
            { 0, "browning",     {"BA5GA0.png","BA5GE0.png","BA5GB0.png","BA5GC0.png","BA5GD0.png"},  5, 4.4f },
            // 1 MACHINE GUN (mp40/rif): single-frame with RIFGA muzzle overlay
            { 1, "mp40",         {"RIFGB0.png"},                                                      1, 3.23f },
            // 2 ROCKET LAUNCHER (panzerschreck): static ready pose
            { 2, "panzerschreck",{"PANZA0.png"},                                                      1, 2.5f },
        };
        for (int p = 0; p < (int)(sizeof(packs)/sizeof(packs[0])); p++) {
            WepSprite *ws = &g_wep[packs[p].wepIdx];
            ws->count = packs[p].n;
            ws->scale = packs[p].scale;
            ws->loaded = true;
            for (int i = 0; i < packs[p].n; i++) {
                char path[700];
                snprintf(path, sizeof(path), "%s%s/%s", appBase, packs[p].folder, packs[p].files[i]);
                ws->frames[i] = LoadTexture(path);
                SetTextureFilter(ws->frames[i], TEXTURE_FILTER_POINT);
                SetTextureWrap(ws->frames[i], TEXTURE_WRAP_CLAMP); // prevent edge wrap artifacts
                if (ws->frames[i].id == 0) ws->loaded = false;
            }
        }
        // Per-weapon horizontal shifts (fraction of screen width)
        g_wep[0].xShift = -0.01f;  // SHOTGUN: 1% left
        g_wep[1].xShift = -0.06f;  // MG:      6% left
        g_wep[2].xShift =  0.10f;  // ROCKET: 10% right
        g_wep[2].yShift = -0.085f; // ROCKET: 8.5% up (lowered 0.5%)

        // MG muzzle flash: RIFGA0 is a full-canvas overlay aligned with RIFGB0
        {
            char fp[700];
            snprintf(fp, sizeof(fp), "%smp40/RIFGA0.png", appBase);
            g_wep[1].flash = LoadTexture(fp);
            if (g_wep[1].flash.id) {
                SetTextureFilter(g_wep[1].flash, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_wep[1].flash, TEXTURE_WRAP_CLAMP);
                g_wep[1].hasFlash = true;
            }
        }

        // Per-weapon crosshair textures (only shotgun for now)
        {
            char fp[700];
            snprintf(fp, sizeof(fp), "%scrosshairs/SHOT.png", appBase);
            g_xhair[0] = LoadTexture(fp);  // shotgun is weapon 0 now
            if (g_xhair[0].id) {
                SetTextureFilter(g_xhair[0], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_xhair[0], TEXTURE_WRAP_CLAMP);
            }
        }

        // Health pickup sprite variants — CHIKA (roast chicken) & EASTA (easter egg)
        {
            char fp[700];
            const char *names[2] = {"CHIKA0.png","EASTA0.png"};
            for (int i = 0; i < 2; i++) {
                snprintf(fp, sizeof(fp), "%spickups/%s", appBase, names[i]);
                g_healthTex[i] = LoadTexture(fp);
                if (g_healthTex[i].id) {
                    SetTextureFilter(g_healthTex[i], TEXTURE_FILTER_POINT);
                    SetTextureWrap  (g_healthTex[i], TEXTURE_WRAP_CLAMP);
                }
            }
        }

        // Ammo pickup billboards — indexed by Pickup.type (0 is unused/health)
        {
            const char *ammoFiles[5] = { NULL, "SBOXA0.png", "MNRBB0.png", "MCLPA0.png", "MCLPB0.png" };
            char fp[700];
            for (int t = 1; t < 5; t++) {
                snprintf(fp, sizeof(fp), "%spickups/%s", appBase, ammoFiles[t]);
                g_ammoTex[t] = LoadTexture(fp);
                if (g_ammoTex[t].id) {
                    SetTextureFilter(g_ammoTex[t], TEXTURE_FILTER_POINT);
                    SetTextureWrap  (g_ammoTex[t], TEXTURE_WRAP_CLAMP);
                }
            }
        }

        // Chef walking animation (4 frames from WolfenDoom bosses/AFAB*)
        {
            char fp[700];
            const char *names[4] = {"AFABA0.png","AFABB0.png","AFABC0.png","AFABD0.png"};
            g_chefOK = true;
            for (int i = 0; i < 4; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/%s", appBase, names[i]);
                g_chefTex[i] = LoadTexture(fp);
                if (g_chefTex[i].id == 0) { g_chefOK = false; continue; }
                SetTextureFilter(g_chefTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_chefTex[i], TEXTURE_WRAP_CLAMP);
            }
        }

        // Chef death animation (G→H→I→J, freezes on J)
        {
            char fp[700];
            const char *names[4] = {"AFABG0.png","AFABH0.png","AFABI0.png","AFABJ0.png"};
            g_chefDeathOK = true;
            for (int i = 0; i < 4; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/%s", appBase, names[i]);
                g_chefDeathTex[i] = LoadTexture(fp);
                if (g_chefDeathTex[i].id == 0) { g_chefDeathOK = false; continue; }
                SetTextureFilter(g_chefDeathTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_chefDeathTex[i], TEXTURE_WRAP_CLAMP);
            }
        }

        // Chef pain frame (shown briefly when taking damage)
        {
            char fp[700];
            snprintf(fp, sizeof(fp), "%smonsters/AFABF0.png", appBase);
            g_chefPainTex = LoadTexture(fp);
            if (g_chefPainTex.id) {
                SetTextureFilter(g_chefPainTex, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_chefPainTex, TEXTURE_WRAP_CLAMP);
                g_chefPainOK = true;
            }
        }

        // HEAVY CHEF (type 1) — TORM* WolfenDoom boss sprites.
        {
            char fp[700];
            const char *wnames[4] = {"TORMA0.png","TORMB0.png","TORMC0.png","TORMD0.png"};
            g_tormOK = true;
            for (int i = 0; i < 4; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/%s", appBase, wnames[i]);
                g_tormTex[i] = LoadTexture(fp);
                if (g_tormTex[i].id == 0) { g_tormOK = false; continue; }
                SetTextureFilter(g_tormTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_tormTex[i], TEXTURE_WRAP_CLAMP);
            }
            snprintf(fp, sizeof(fp), "%smonsters/TORMF0.png", appBase);
            g_tormPainTex = LoadTexture(fp);
            if (g_tormPainTex.id) {
                SetTextureFilter(g_tormPainTex, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_tormPainTex, TEXTURE_WRAP_CLAMP);
                g_tormPainOK = true;
            }
            const char *dnames[4] = {"TORMG0.png","TORMH0.png","TORMI0.png","TORMJ0.png"};
            g_tormDeathOK = true;
            for (int i = 0; i < 4; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/%s", appBase, dnames[i]);
                g_tormDeathTex[i] = LoadTexture(fp);
                if (g_tormDeathTex[i].id == 0) { g_tormDeathOK = false; continue; }
                SetTextureFilter(g_tormDeathTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_tormDeathTex[i], TEXTURE_WRAP_CLAMP);
            }
        }

        // FAST CHEF (type 2) — SCH2* WolfenDoom boss sprites.
        {
            char fp[700];
            const char *wnames[4] = {"SCH2A0.png","SCH2B0.png","SCH2C0.png","SCH2D0.png"};
            g_schOK = true;
            for (int i = 0; i < 4; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/%s", appBase, wnames[i]);
                g_schTex[i] = LoadTexture(fp);
                if (g_schTex[i].id == 0) { g_schOK = false; continue; }
                SetTextureFilter(g_schTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_schTex[i], TEXTURE_WRAP_CLAMP);
            }
            snprintf(fp, sizeof(fp), "%smonsters/SCH2F0.png", appBase);
            g_schPainTex = LoadTexture(fp);
            if (g_schPainTex.id) {
                SetTextureFilter(g_schPainTex, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_schPainTex, TEXTURE_WRAP_CLAMP);
                g_schPainOK = true;
            }
            const char *sdnames[4] = {"SCH2G0.png","SCH2H0.png","SCH2I0.png","SCH2J0.png"};
            g_schDeathOK = true;
            for (int i = 0; i < 4; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/%s", appBase, sdnames[i]);
                g_schDeathTex[i] = LoadTexture(fp);
                if (g_schDeathTex[i].id == 0) { g_schDeathOK = false; continue; }
                SetTextureFilter(g_schDeathTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_schDeathTex[i], TEXTURE_WRAP_CLAMP);
            }
        }

        // BOSS — BTCN monster (WolfenDoom). A-D walk, F pain, G/H/I/J death.
        {
            char fp[700];
            const char *wnames[4] = {"BTCNA0.png","BTCNB0.png","BTCNC0.png","BTCND0.png"};
            g_bossOK = true;
            for (int i = 0; i < 4; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/%s", appBase, wnames[i]);
                g_bossTex[i] = LoadTexture(fp);
                if (g_bossTex[i].id == 0) { g_bossOK = false; continue; }
                SetTextureFilter(g_bossTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_bossTex[i], TEXTURE_WRAP_CLAMP);
            }
            // Pain frame
            snprintf(fp, sizeof(fp), "%smonsters/BTCNF0.png", appBase);
            g_bossPainTex = LoadTexture(fp);
            if (g_bossPainTex.id) {
                SetTextureFilter(g_bossPainTex, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_bossPainTex, TEXTURE_WRAP_CLAMP);
                g_bossPainOK = true;
            }
            // Death frames: G → H → I → J (freeze on J)
            const char *dnames[4] = {"BTCNG0.png","BTCNH0.png","BTCNI0.png","BTCNJ0.png"};
            g_bossDeathOK = true;
            for (int i = 0; i < 4; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/%s", appBase, dnames[i]);
                g_bossDeathTex[i] = LoadTexture(fp);
                if (g_bossDeathTex[i].id == 0) { g_bossDeathOK = false; continue; }
                SetTextureFilter(g_bossDeathTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_bossDeathTex[i], TEXTURE_WRAP_CLAMP);
            }
        }
    }

    // Set ambient
    float amb[4]={0.40f,0.32f,0.46f,1.f};
    SetShaderValue(g_shader,u_ambient,amb,SHADER_UNIFORM_VEC4);

    // Try to load the bundled BRIK texture first; fall back to procedural.
    Texture2D tBrick;
    {
        char fp[700];
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sprites/textures/BRIK_B01.png",
                 AppDir());
        tBrick = LoadTexture(fp);
        if (tBrick.id == 0) {
            tBrick = MkBrick();
        } else {
            GenTextureMipmaps(&tBrick);
            SetTextureFilter(tBrick, TEXTURE_FILTER_TRILINEAR);
            SetTextureWrap  (tBrick, TEXTURE_WRAP_REPEAT);
        }
    }
    Texture2D tFloor = MkFloor();
    Texture2D tCeil  = MkCeil();

    Mesh wm = BuildWallMesh();
    g_wallModel  = MakeShaderModel(wm, tBrick);
    Mesh fm = BuildPlaneMesh(0.f,  (float)COLS*CELL/4.f, (float)ROWS*CELL/4.f, false);
    g_floorModel = MakeShaderModel(fm, tFloor);
    Mesh cm = BuildPlaneMesh(WALL_H, (float)COLS*CELL/6.f, (float)ROWS*CELL/6.f, true);
    g_ceilModel  = MakeShaderModel(cm, tCeil);

    // Shared unit cube for platforms — lit via custom shader, textured with brick
    Mesh platMesh = GenMeshCube(1.f, 1.f, 1.f);
    g_platModel   = MakeShaderModel(platMesh, tBrick);
    InitPlatforms();

    g_cam = (Camera3D){0};
    g_cam.fovy=90.f; g_cam.projection=CAMERA_PERSPECTIVE; g_cam.up=(Vector3){0,1,0};
    g_cam.position=(Vector3){1.5f*CELL,EYE_H,1.5f*CELL};
    g_cam.target=(Vector3){1.5f*CELL+1,EYE_H,1.5f*CELL};

    g_gs=GS_MENU;

#ifdef __EMSCRIPTEN__
    // Browser main loop: hand control back to the runtime after each frame so
    // the event loop can deliver input + repaint. simulate_infinite_loop=1
    // unwinds here via a JS exception, so the cleanup code below never runs
    // on web (that's fine — page teardown reclaims GL/audio resources).
    emscripten_set_main_loop(StepFrame, 0, 1);
    return 0;
#endif

    while (!WindowShouldClose()) StepFrame();

    UnloadModel(g_wallModel); UnloadModel(g_floorModel); UnloadModel(g_ceilModel);
    UnloadModel(g_platModel);
    UnloadTexture(tBrick); UnloadTexture(tFloor); UnloadTexture(tCeil);
    for (int w=0;w<3;w++) {
        for (int i=0;i<g_wep[w].count;i++)
            if (g_wep[w].frames[i].id) UnloadTexture(g_wep[w].frames[i]);
        if (g_wep[w].hasFlash && g_wep[w].flash.id) UnloadTexture(g_wep[w].flash);
        if (g_xhair[w].id) UnloadTexture(g_xhair[w]);
    }
    for (int i=0;i<2;i++) if (g_healthTex[i].id) UnloadTexture(g_healthTex[i]);
    for (int i=0;i<5;i++) if (g_ammoTex[i].id)   UnloadTexture(g_ammoTex[i]);
    for (int i=0;i<4;i++) if (g_chefTex[i].id)      UnloadTexture(g_chefTex[i]);
    for (int i=0;i<4;i++) if (g_chefDeathTex[i].id) UnloadTexture(g_chefDeathTex[i]);
    if (g_chefPainTex.id) UnloadTexture(g_chefPainTex);
    for (int i=0;i<4;i++) if (g_tormTex[i].id)      UnloadTexture(g_tormTex[i]);
    for (int i=0;i<4;i++) if (g_tormDeathTex[i].id) UnloadTexture(g_tormDeathTex[i]);
    if (g_tormPainTex.id) UnloadTexture(g_tormPainTex);
    for (int i=0;i<4;i++) if (g_schTex[i].id)       UnloadTexture(g_schTex[i]);
    for (int i=0;i<4;i++) if (g_schDeathTex[i].id)  UnloadTexture(g_schDeathTex[i]);
    if (g_schPainTex.id)  UnloadTexture(g_schPainTex);
    for (int i=0;i<4;i++) if (g_bossTex[i].id)      UnloadTexture(g_bossTex[i]);
    for (int i=0;i<5;i++) if (g_bossDeathTex[i].id) UnloadTexture(g_bossDeathTex[i]);
    if (g_bossPainTex.id) UnloadTexture(g_bossPainTex);
    UnloadShader(g_shader);
    UnloadSound(g_sPistol); UnloadSound(g_sShotgun); UnloadSound(g_sRocket);
    UnloadSound(g_sExplode); UnloadSound(g_sHurt); UnloadSound(g_sPickup);
    UnloadSound(g_sEmpty); UnloadSound(g_sDie);
    for (int i=0;i<CHEF_DIE_ALT_COUNT;i++) if (g_sDieAltOK[i]) UnloadSound(g_sDieAlt[i]);
    if (g_sHeadshotOK) UnloadSound(g_sHeadshot);
    if (g_sFatalityOK) UnloadSound(g_sFatality);
    if (g_sMultiOK)      UnloadSound(g_sMulti);
    if (g_sFirstBloodOK)   UnloadSound(g_sFirstBlood);
    for (int a = 0; a < g_sEnemyAlertCount; a++) {
        if (g_sEnemyAlertOK[a]) UnloadSound(g_sEnemyAlert[a]);
    }
    if (g_sShotgunKillOK)  UnloadSound(g_sShotgunKill);
    if (g_sMGOK)           UnloadSound(g_sMG);
    if (g_sNextWaveOK)     UnloadSound(g_sNextWave);
    if (g_sHealthPickupOK) UnloadSound(g_sHealthPickup);
    if (g_sMGPickupOK)     UnloadSound(g_sMGPickup);
    if (g_sOneLeftOK)      UnloadSound(g_sOneLeft);
    if (g_musicOK) { StopMusicStream(g_music); UnloadMusicStream(g_music); }
    if (g_dbgLog) { fclose(g_dbgLog); g_dbgLog = NULL; }
    CloseAudioDevice(); CloseWindow();
    return 0;
}
