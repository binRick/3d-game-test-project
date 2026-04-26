// IRON FIST 3D - raylib native FPS
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "common.h"
#include "hud.h"
#include "effects.h"
#include "level.h"
#include <math.h>
#include <stdio.h>
#include <stdarg.h>  // va_list / vsnprintf — used by the dev console (ConPrintf)
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
// CELL/ROWS/COLS/MAX_ENEMIES/EYE_H now live in common.h
#define WALL_H      5.1f
#define MAX_BULLETS 256
// MAX_PARTS, GRAV moved to common.h
#define MAX_PICKS   48
#define PRAD        0.32f
#define SPEED       7.5f
#define SENS        0.0016f
#define JUMP        8.5f
// STEP_H, MAX_PLATS, Platform moved to common.h
#define MAX_PITCH   1.44f

// ── MAP ──────────────────────────────────────────────────────────────────────

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
"#define NL 22\n"
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

// ── ALPHA-TEST BILLBOARD SHADER ──────────────────────────────────────────────
// raylib's default billboard shader writes depth for every fragment, even
// fully-transparent ones. That makes a sprite's transparent fringe punch
// holes in the depth buffer where there's no visible pixel — anything drawn
// AFTER the billboard at greater depth then gets clipped by that ghost
// rectangle. The flaming-barrel sprites have a wide transparent fringe
// around the flame, so an enemy walking behind a barrel was getting masked.
//
// The fix is alpha-test rendering: the fragment shader discards low-alpha
// fragments so they never write depth. Wrap DrawBarrels (and any other
// fringe-prone billboard) in BeginShaderMode(g_alphaShader) / EndShaderMode.
static Shader g_alphaShader;
static const char *ALPHA_VS = GLSL_VERSION
"in vec3 vertexPosition;\n"
"in vec2 vertexTexCoord;\n"
"in vec4 vertexColor;\n"
"uniform mat4 mvp;\n"
"out vec2 fragUV;\n"
"out vec4 fragCol;\n"
"void main(){\n"
"  fragUV = vertexTexCoord;\n"
"  fragCol = vertexColor;\n"
"  gl_Position = mvp*vec4(vertexPosition,1.0);\n"
"}\n";
static const char *ALPHA_FS = GLSL_VERSION GLSL_PRECISION
"in vec2 fragUV;\n"
"in vec4 fragCol;\n"
"uniform sampler2D texture0;\n"
"uniform vec4 colDiffuse;\n"
"out vec4 outColor;\n"
"void main(){\n"
"  vec4 t = texture(texture0, fragUV) * colDiffuse * fragCol;\n"
"  if (t.a < 0.5) discard;\n"
"  outColor = t;\n"
"}\n";

// ── LIGHTING ─────────────────────────────────────────────────────────────────
// Slot layout (NUM_LIGHTS must match the shader's NL):
//   [0..NUM_LAMPS)                    static ceiling lamps
//   [NUM_LAMPS..NUM_LAMPS+NUM_BARRELS) flaming-barrel lights (animated)
//   MUZZLE_LIGHT                      rocket boom + gun flash
//   PULSE_LIGHT                       player damage pulse
#define NUM_LAMPS         14
#define NUM_BARRELS       6
#define NUM_LIGHTS        (NUM_LAMPS + NUM_BARRELS + 2)
#define BARREL_LIGHT_BASE NUM_LAMPS
#define MUZZLE_LIGHT      (NUM_LAMPS + NUM_BARRELS)
#define PULSE_LIGHT       (NUM_LAMPS + NUM_BARRELS + 1)
typedef struct { Vector3 pos; Vector3 color; float radius; int enabled; } LightDef;
static Shader    g_shader;
static LightDef  g_lights[NUM_LIGHTS];
// Per-lamp destroyed flag for slots 0..5 (the static ceiling lights). Kept
// separate from g_lights[i].enabled because slots 6/7 are also toggled
// every frame for muzzle flash + explosion glow, and we need a stable bit
// that says "this fixture has been shot off — draw it as broken".
static bool      g_lampBroken[NUM_LAMPS];

// Flaming barrel scenery — billboarded sprite + per-barrel point light.
// Position fixed at game start, light flickers per frame.
typedef struct { Vector3 pos; float phase; bool active; } Barrel;
static Barrel    g_barrels[NUM_BARRELS];
static int       g_barrelCount = 0;
static Texture2D g_barrelTex[3];     // fcana0/fcanb0/fcanc0 — 3-frame fire cycle
static bool      g_barrelOK = false;

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
    g_alphaShader = LoadShaderFromMemory(ALPHA_VS, ALPHA_FS);
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

    // Fixed scene lights – industrial/hell palette across the arena.
    // Mounted just below the ceiling so the visible fixture sits flush.
    // Cell coords (col, row) checked against MAP — must be MAP[r][c]==0.
    LightDef scene[NUM_LAMPS] = {
        // Original six
        {{ 5*CELL, WALL_H-0.15f,  2*CELL}, {1.0f, 0.65f, 0.2f},  40.f, 1},
        {{14*CELL, WALL_H-0.15f,  9*CELL}, {0.2f, 0.5f,  1.0f},  50.f, 1},
        {{24*CELL, WALL_H-0.15f,  5*CELL}, {1.0f, 0.15f, 0.1f},  40.f, 1},
        {{ 5*CELL, WALL_H-0.15f, 16*CELL}, {0.15f,1.0f,  0.3f},  40.f, 1},
        {{24*CELL, WALL_H-0.15f, 16*CELL}, {0.9f, 0.3f,  1.0f},  40.f, 1},
        {{14*CELL, WALL_H-0.15f, 17*CELL}, {1.0f, 0.8f,  0.3f},  40.f, 1},
        // Eight extras spread across the upper + middle bands
        {{ 2*CELL, WALL_H-0.15f,  6*CELL}, {1.0f, 0.55f, 0.15f}, 35.f, 1},  // amber, NW
        {{ 9*CELL, WALL_H-0.15f,  9*CELL}, {0.3f, 0.7f,  1.0f},  35.f, 1},  // ice blue, mid-W
        {{19*CELL, WALL_H-0.15f,  9*CELL}, {0.3f, 1.0f,  0.4f},  35.f, 1},  // green, mid-E
        {{27*CELL, WALL_H-0.15f,  9*CELL}, {1.0f, 0.3f,  0.85f}, 35.f, 1},  // magenta, far-E
        {{ 2*CELL, WALL_H-0.15f, 13*CELL}, {1.0f, 0.2f,  0.15f}, 35.f, 1},  // red, SW
        {{ 9*CELL, WALL_H-0.15f, 13*CELL}, {1.0f, 0.85f, 0.55f}, 35.f, 1},  // warm white
        {{20*CELL, WALL_H-0.15f, 13*CELL}, {0.2f, 0.9f,  0.85f}, 35.f, 1},  // teal
        {{27*CELL, WALL_H-0.15f, 13*CELL}, {1.0f, 0.7f,  0.25f}, 35.f, 1},  // orange, SE
    };
    for (int i = 0; i < NUM_LAMPS; i++) g_lights[i] = scene[i];
    // Reserved dynamic slots — muzzle/explosion glow + player damage pulse
    g_lights[MUZZLE_LIGHT] = (LightDef){{0,0,0},{0,0,0}, 0.f, 0};
    g_lights[PULSE_LIGHT]  = (LightDef){{0,0,0},{0,0,0}, 0.f, 0};
    for (int i = 0; i < NUM_LIGHTS; i++) ShaderSetLight(i);
}

// ── TYPES ────────────────────────────────────────────────────────────────────
// Player, Enemy, Pickup, Part, GameState, EnemyState moved to common.h
typedef struct { Vector3 pos, vel; float life, dmg; bool active, rocket; } Bullet;
// Tesla chain-lightning bolt — ordered points from muzzle through victims.
// Drawn as jagged DrawLine3D segments per chain edge, fades over BOLT_LIFE.
#define BOLT_MAX_PTS 7     // muzzle + up to 6 victims
#define BOLT_LIFE   0.18f
typedef struct { Vector3 pts[BOLT_MAX_PTS]; int n; float life; bool active; } Bolt;
// Enemy projectile. Mutant fires energy balls (single-target, purple trail).
// Mech fires rockets (slower, orange smoke, splash AOE on impact).
typedef struct { Vector3 pos, vel; float life, dmg; bool active, isRocket; } EShot;

// ── GLOBALS ──────────────────────────────────────────────────────────────────
Player   g_p;
static float    g_swayX=0, g_swayY=0;   // weapon sway (screen pixels)
Enemy    g_e[MAX_ENEMIES]; int g_ec;
static Bullet   g_b[MAX_BULLETS];
Pickup   g_pk[MAX_PICKS];  int g_pkc;
#define MAX_BOLTS 16
static Bolt     g_bolts[MAX_BOLTS];
#define MAX_ESHOTS 32
static EShot    g_es[MAX_ESHOTS];
int      g_wave;
GameState g_gs;
char     g_msg[80]; float g_msgT;
char     g_hypeMsg[80]; float g_hypeT; float g_hypeDur;
// Wall geometry is split into multiple models so each can be drawn with a
// different texture. Cells are grouped by connected component (4-neighbour
// flood-fill on MAP[][]), then each component is assigned one of
// WALL_TEX_COUNT texture slots — a contiguous wall segment is therefore
// one uniform texture, while an isolated column gets its own slot.
#define WALL_TEX_COUNT 6
static Model    g_wallModels[WALL_TEX_COUNT];
static int      g_wallSlot[ROWS][COLS];   // texture slot per wall cell, -1 if not a wall
static Model    g_floorModel, g_ceilModel;

// Forward decl for the high-score POST. Implemented below in score_post.c
// (web: EM_JS fetch; native: NSURLSession on macOS, no-op on Windows).
static void SubmitScore(void);
// Forward decls for the rear-warning helpers — implemented just above
// the dev console block but called from earlier in the file (enemy fire
// sites + the rear-stinger / arc indicator blocks).
static int  FindClosestRearEnemy(float range);
static void PlayPositionalSound(Sound s, Vector3 pos);

// Forward decl — TriggerMultiKill is defined alongside Shoot() but called
// from the earlier rocket-splash branches in UpdBullets.
static void TriggerMultiKill(void);
static void SpawnEShot(Vector3 origin, Vector3 target, float dmg);
static void SpawnEShotRocket(Vector3 origin, Vector3 target, float dmg);
static bool TeslaLOS(Vector3 a, Vector3 b);

// ── PLATFORMS (Q3-style 3D level geometry on top of the 2D floor) ───────────
// Platform/g_plats/g_platCount moved to level.c
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
static WepSprite g_wep[4];  // one per player weapon (0=shotgun 1=MG 2=rocket 3=tesla)

// Per-weapon crosshair overrides (NULL texture = use default tick-mark crosshair)
Texture2D g_xhair[4];

// Pickup billboards (NULL texture = use default colored sphere)
// Health has 2 variants (CHIKA/EASTA), others are single textures keyed by pickup type.
static Texture2D g_healthTex[2];
// Ammo pickup textures indexed by Pickup.type:
//   [0]=unused (health), [1]=shells (SBOXA), [2]=rockets (MNRBB), [3]=pistol (MCLPA), [4]=MG (MCLPB)
static Texture2D g_ammoTex[5];
// Tesla pickup billboard (Pickup.type==7) — separate slot since it's outside
// the 0..4 ammo range and grants the weapon itself on first grab.
static Texture2D g_teslaPickupTex;

// DOOM-style-Game enemies — sprite container for soldier (type 7),
// cacodemon (type 8), cyber demon (type 9). Same struct backs the arena
// picker preview AND the in-game render path. Up to PREV_WALK_MAX walk,
// PREV_ATK_MAX attack, PREV_PAIN_MAX pain, PREV_DEATH_MAX death frames.
#define PREV_WALK_MAX  8
#define PREV_ATK_MAX   5
#define PREV_PAIN_MAX  2
#define PREV_DEATH_MAX 9
typedef struct {
    Texture2D walk [PREV_WALK_MAX ]; int walkCount;
    Texture2D atk  [PREV_ATK_MAX  ]; int atkCount;
    Texture2D pain [PREV_PAIN_MAX ]; int painCount;
    Texture2D death[PREV_DEATH_MAX]; int deathCount;
    bool ok;
} PreviewEnemy;
static PreviewEnemy g_prevSoldier;
static PreviewEnemy g_prevCaco;
static PreviewEnemy g_prevCyber;
static PreviewEnemy g_prevRevenant;
static PreviewEnemy g_prevLostSoul;
static PreviewEnemy g_prevPainElem;
#define DGAME_DEATH_FRAME_TIME 0.16f
#define DGAME_ATK_FRAME_TIME   0.10f

// Doom-style HUD mugshot (WolfenDoom STF* frames). 5 health tiers × 3 idle
// looks, plus pain (OUCH) / kill (KILL) reactions and a final DEAD frame.
// Mugshot textures moved to hud.c

// Animated blood-splat test (Beautiful-Doom YBL7 series, frames A-S).
// Single-variant evaluation only — see also: SpawnSplat() at the bullet hit
// site in Shoot() for the integration point.
#define BLOOD_SPLAT_FRAMES 19
#define BLOOD_SPLAT_FPS    15.f
#define MAX_SPLATS         64
typedef struct { Vector3 pos; int frame; float frameT; bool alive; } BloodSplat;
static Texture2D g_bloodSplatTex[BLOOD_SPLAT_FRAMES];
static bool      g_bloodSplatOK = false;
static BloodSplat g_splats[MAX_SPLATS];

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
// Chef attack frame (AFABE0). Shown briefly during ATTACK windup.
static Texture2D g_chefAtkTex;
static bool      g_chefAtkOK = false;

// Heavy chef (type 1) — TORM* sprites. Same layout as type-0 chef:
//   A-D walk, E attack, F pain, G-J death.
static Texture2D g_tormTex[4];
static bool      g_tormOK = false;
static Texture2D g_tormPainTex;
static bool      g_tormPainOK = false;
static Texture2D g_tormAtkTex;
static bool      g_tormAtkOK = false;
static Texture2D g_tormDeathTex[4];
static bool      g_tormDeathOK = false;

// Fast chef (type 2) — SCH2* sprites. Same A-D walk / E attack / F pain / G-J death layout.
static Texture2D g_schTex[4];
static bool      g_schOK = false;
static Texture2D g_schPainTex;
static bool      g_schPainOK = false;
static Texture2D g_schAtkTex;
static bool      g_schAtkOK = false;
static Texture2D g_schDeathTex[4];
static bool      g_schDeathOK = false;

// Cultist (enemy type 4) — SSCT* WolfenDoom occult sprites. 8-directional.
// Doom rotation pairs: each "DirFrame" is 5 unique PNGs covering the 8 view
// angles via mirror (rot 1/5 are unique, rot 2/8, 3/7, 4/6 share with flip).
typedef struct { Texture2D rot[5]; bool ok; } DirFrame;
static DirFrame  g_cultWalk[4];   // A-D walk cycle
static bool      g_cultOK = false;
static DirFrame  g_cultPain;      // E pain frame
static bool      g_cultPainOK = false;
static DirFrame  g_cultAtk;       // G fire pose — used as attack windup telegraph
static bool      g_cultAtkOK = false;
static Texture2D g_cultDeathTex[5];  // I0..M0 death (corpse — single rot)
static bool      g_cultDeathOK = false;
#define CULT_DEATH_FRAME_TIME 0.22f

// Ranged mutant (enemy type 5) — MTNT* WolfenDoom mutant_range sprites.
// 8-directional via 5-rotation mirror, same scheme as cultist.
//   A-D walk, E attack-charge, F pain, G attack-fire, J0..M0 death corpses.
static DirFrame  g_mutWalk[4];
static bool      g_mutOK = false;
static DirFrame  g_mutPain;       static bool g_mutPainOK = false;
static DirFrame  g_mutAtkCharge;  // E — arm raised, ball forming
static DirFrame  g_mutAtkFire;    // G — arm fully up, ball released
static bool      g_mutAtkOK = false;
static Texture2D g_mutDeathTex[4];   // J0..M0
static bool      g_mutDeathOK = false;
#define MUT_DEATH_FRAME_TIME 0.22f

// Heavy mech (enemy type 6) — MAVY* WolfenDoom Wehrmacht-mecha sprites.
// 8-directional, walks heavily, fires alternating-arm rocket bursts.
//   N = idle/standing, A-D = walk cycle, F = left arm fire, G = right arm fire.
//   No dedicated death sprites in source — we explode + remove.
static DirFrame  g_mechIdle;       static bool g_mechIdleOK = false;
static DirFrame  g_mechWalk[4];    static bool g_mechOK = false;
static DirFrame  g_mechFireL;      // F frame (left arm)
static DirFrame  g_mechFireR;      // G frame (right arm)
static bool      g_mechFireOK = false;

// Boss (enemy type 3) — BTCN* sprites. Walk A-D, attack E, pain F, death G/H/I/J.
static Texture2D g_bossTex[4];
static bool      g_bossOK = false;
static Texture2D g_bossPainTex;
static bool      g_bossPainOK = false;
static Texture2D g_bossAtkTex;
static bool      g_bossAtkOK = false;
static Texture2D g_bossDeathTex[5];
static bool      g_bossDeathOK = false;
#define BOSS_DEATH_FRAME_TIME 0.22f

// ARENA mode — unified test mode that replaces the older per-type
// g_bossMode / g_mutMode / g_mechMode flags. Pick any enemy type from a
// menu screen.
// Press A on the main menu → GS_PICK_ENEMY screen → arrow keys / 1-7 to pick
// → Enter to start. Spawns 8 of the chosen type, respawns on clear.
static bool      g_arenaMode = false;
static int       g_arenaType = 0;     // 0..6 — which enemy type the arena spawns
static int       g_pickerIdx = 0;     // current selection on the GS_PICK_ENEMY screen
static float     g_pickerT   = 0.f;   // time accumulator for preview anim
static bool      g_paused    = false; // P toggles in-game pause; freezes Upd* calls
static bool      g_quit      = false; // set by ESC on the main menu; main loop exits next iteration

// Cheat / godmode state shared between the dev console (sets) and several
// damage application sites (read). Hoisted up here so they're visible
// before the first use in UpdEnemies / UpdBullets etc.
//
// IMPORTANT — any console command that ENHANCES PLAYER CAPABILITY (extra
// ammo / health / score-affecting state, godmode, kill-all, teleport,
// wave skip, etc.) MUST set g_cheated. The death-screen leaderboard entry
// is suppressed when set so cheated runs can't pollute the scoreboard.
// Pure-info / no-effect commands (help, pos, clear, quit) leave it
// alone. See ConExecute below + CLAUDE.md.
static bool      g_god       = false; // toggled by `god` console cmd
static bool      g_cheated   = false; // any cheat-class command flips this true

// Rear-warning experiments — independent toggles via the `warn` console
// command so the player can audition each indicator and pick which one(s)
// to keep. NONE of these set g_cheated; they're UX prefs, not capability
// enhancers. Default off so the player opts in.
static bool      g_warnArc      = false; // red arrow at screen edge for rear enemies
static bool      g_warnPan      = false; // stereo-pan enemy sounds based on bearing
static bool      g_warnStinger  = false; // one-shot vocal when an enemy lingers in rear arc
       bool      g_warnMinimap  = false; // tint the rear half of the minimap red (extern in hud.c)

// High-score submission stats — accumulated through one game session and
// posted to https://ironfist.ximg.app/api/scores on death (or skip via
// ESC on the initials screen). Reset in InitGame.
static int       g_statShots    = 0;
static float     g_statDamage   = 0.f;
static int       g_statPickups  = 0;
static float     g_statTime     = 0.f;
// Rank returned by the leaderboard POST response. 0 = unknown / not yet
// arrived; >0 = the rank from the server. Set asynchronously by the
// EM_JS fetch().then() chain on web and by the NSURLSession completion
// handler on native macOS, both of which call IronFistRankReceived
// below. Reset in InitGame.
static int       g_lastRank     = 0;
// Three-character initials entry on the death screen. Letters A-Z and
// digits 0-9; arrows cycle the highlighted slot, letter/digit keys type
// directly. ENTER submits, ESC skips submission.
static char      g_initials[4]    = "AAA";
static int       g_initialsPos    = 0;
static bool      g_initialsDone   = false;  // true after submit-or-skip
static bool      g_initialsSubmitted = false; // true if posted (vs skipped)
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
static Sound    g_sMonster;       // "Monster Kill" — 4+ enemies in one shot
static bool     g_sMonsterOK = false;
static Sound    g_sFirstBlood;    // UT "First Blood!" — plays on first enemy kill each run
static bool     g_sFirstBloodOK = false;
// Enemy-sighted stingers — one is picked at random each time the first enemy
// enters the player's view. Add more by appending to the ENEMY_ALERT_FILES
// table; the loader skips any file that fails to load.
#define ENEMY_ALERT_MAX 8
static Sound    g_sEnemyAlert[ENEMY_ALERT_MAX];
static bool     g_sEnemyAlertOK[ENEMY_ALERT_MAX] = {0};
static int      g_sEnemyAlertCount = 0;
static bool     g_hadVisibleEnemy = false;   // previous-frame visibility state
static Sound    g_sShotgunKill; // stinger that plays when a shotgun kill lands
static bool     g_sShotgunKillOK = false;
static Sound    g_sTesla;       // tesla cannon zap (sounds/tesla.ogg)
static bool     g_sTeslaOK = false;
static Sound    g_sMechRocket;  // mech rocket launch (sounds/mech-rocket.mp3)
static bool     g_sMechRocketOK = false;
static Sound    g_sMutAttack;   // mutant energy-ball fire (sounds/mutant-attack.mp3)
static bool     g_sMutAttackOK = false;
static Sound    g_sChefHit;     // chef melee inflicts damage on player (sounds/player-ouch.mp3)
static bool     g_sChefHitOK = false;
// Critical-health stinger — plays once when the player crosses below 10%
// HP. Re-arms once HP climbs back to 20% so it can fire again next time
// the player gets low (hysteresis avoids rapid re-fire if HP wobbles).
static Sound    g_sLowHealth;
static bool     g_sLowHealthOK = false;
static bool     g_lowHealthFired = false;
// Soldier (enemy type 7) — heavy MG fire sample (sounds/soldier-mg.mp3),
// played on each hitscan tick instead of the shared shotgun blast.
static Sound    g_sSoldierMG;
static bool     g_sSoldierMGOK = false;
// SS guard / cultist (enemy type 4) — G36 fire sample (sounds/ss-fire.mp3),
// played on each hitscan tick. Replaces the generic shotgun blast.
static Sound    g_sSSFire;
static bool     g_sSSFireOK = false;
// Cyber demon (enemy type 9) — distant gun-fire sample
// (sounds/cyber-fire.mp3) played when launching a rocket. Replaces the
// shared launcher sample for cyber specifically.
static Sound    g_sCyberFire;
static bool     g_sCyberFireOK = false;
// Echoey first-blood-of-wave sample. Plays on the FIRST kill of each
// wave (g_sFirstBlood plays once per RUN; this fires every wave).
static Sound    g_sFirstBloodWave;
static bool     g_sFirstBloodWaveOK = false;
static bool     g_firstKillThisWave = false;
// Idle-sigh sample played when the player has been wandering with no
// enemy visible for more than a few seconds (g_quietTime > 5s).
// One-shot per quiet stretch — re-arms when an enemy becomes visible.
static Sound    g_sIdleSigh;
static bool     g_sIdleSighOK = false;
static float    g_quietTime = 0.f;
static bool     g_quietSighFired = false;
static Sound    g_sMechHit;     // mech rocket splash on player (sounds/player-ough.mp3)
static bool     g_sMechHitOK = false;
static Sound    g_sNextWave;   // stinger when next wave starts
static bool     g_sNextWaveOK = false;
static Sound    g_sMG;          // machine gun firing sound
static bool     g_sMGOK = false;
static int      g_killsThisShot = 0;  // incremented by KillEnemy, reset around each shot/explosion
// Alternating background-music playlist. Tracks are streamed (not loaded
// into memory), play once each (looping=false), and the per-frame Update
// block below swaps to the next when the current one ends. g_musicOK is
// true when ANY track loaded — existing pause/volume/update guards still
// short-circuit correctly when the user has no audio assets at all.
//
// Title music is a SEPARATE looping stream that plays on the GS_MENU
// state. The state-transition handler in StepFrame swaps between the
// title track and the in-game playlist on entering / leaving GS_MENU.
#define MUSIC_TRACK_COUNT 3
static Music    g_musicTracks[MUSIC_TRACK_COUNT];
static bool     g_musicTracksOK[MUSIC_TRACK_COUNT] = {false};
static int      g_musicIdx = 0;
static bool     g_musicOK = false;        // true if any in-game track loaded
static const char *g_musicFiles[MUSIC_TRACK_COUNT] = {
    "sounds/hell-march.mp3",
    "sounds/funeral-queen-mary.mp3",
    "sounds/soviet-march.mp3",
};
static Music    g_titleMusic;
static bool     g_titleMusicOK = false;
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

// enemy stat tables — type 0/1/2 chefs, 3 = BOSS, 4 = CULTIST/SS,
// 5 = MUTANT (ranged ball), 6 = MECH (heavy rocket),
// 7 = SOLDIER (Doom shotgunner, hitscan), 8 = CACODEMON (flying, fireball),
// 9 = CYBER DEMON (boss-tier, rockets), 10 = REVENANT (preview/melee for
// now), 11 = LOST SOUL (preview/charging melee), 12 = PAIN ELEMENTAL
// (preview/floater).
static const float ET_HP[]    = {65,   145,  42,   800,  80,   90,   260,  120,  220,  1500, 250,  60,   420  };
static const float ET_SPD[]   = {6.0f, 3.6f, 8.8f, 7.5f, 5.0f, 4.2f, 2.6f, 5.0f, 3.5f, 3.8f, 5.5f, 9.0f, 3.0f };
static const float ET_DMG[]   = {10,   24,   8,    40,   14,   14,   28,   14,   18,   34,   16,   8,    18   };
static const float ET_RATE[]  = {1.5f, 2.1f, 1.0f, 1.6f, 1.8f, 1.4f, 2.6f, 1.4f, 2.0f, 2.6f, 1.6f, 0.9f, 2.0f };
static const float ET_AR[]    = {24,   20,   30,   40,   30,   30,   45,   30,   35,   50,   30,   25,   30   };
static const float ET_ATK[]   = {3.6f, 3.1f, 4.2f, 1.6f, 4.0f, 12.0f,20.0f,4.0f, 14.0f,14.0f,3.6f, 2.8f, 4.2f };
static const int   ET_SC[]    = {100,  300,  160,  2500, 220,  240,  500,  200,  500,  3000, 350,  120,  450  };
const Color ET_COL[]   = {{60,160,55,255},{140,55,185,255},{40,110,210,255},{220,60,60,255},{120,40,160,255},{60,180,90,255},{80,90,180,255},{180,140,80,255},{220,30,30,255},{220,180,40,255},{200,200,210,255},{255,200,80,255},{180,80,160,255}};
static const Color ET_EYE[]   = {{255,30,20,255},{255,150,0,255},{0,230,255,255},{255,255,120,255},{200,80,255,255}};

// weapon tables
// Weapons: [0]=shotgun (key 1), [1]=machine gun (key 2), [2]=rocket launcher (key 3), [3]=tesla (key 4)
const char * const WPN[]    = {"SHOTGUN", "MACHINE GUN", "LAUNCHER", "TESLA"};
static const float WR[]     = {0.59f, 0.09f, 0.96f, 0.45f};
static const int   WD[]     = {15, 18, 0, 140};       // tesla: primary-target damage; chain falloff is in Shoot()
static const int   WPEL[]   = {8, 1, 1, 1};
// Tesla wind-up: how long after click before the bolt actually fires. The
// charge frames B/C/D play across this window; the discharge frames E/F/G
// play across (WR[3] - TESLA_WINDUP) afterwards.
static const float TESLA_WINDUP = 0.15f;

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
// 4-neighbour flood-fill: every connected group of wall cells gets a single
// component id, mapped via component_id % WALL_TEX_COUNT to a texture slot.
// Result: each contiguous wall segment is uniform; an isolated column gets
// its own slot. Stored once into g_wallSlot[][], read by BuildWallMesh.
static void ComputeWallSlots(void) {
    for (int r=0;r<ROWS;r++) for (int c=0;c<COLS;c++) g_wallSlot[r][c] = -1;
    // Flood-fill stack
    int stack[ROWS*COLS][2];
    int compId = 0;
    for (int r=0;r<ROWS;r++) for (int c=0;c<COLS;c++) {
        if (!MAP[r][c] || g_wallSlot[r][c] != -1) continue;
        int slot = compId % WALL_TEX_COUNT;
        compId++;
        int top = 0;
        stack[top][0] = r; stack[top][1] = c; top++;
        g_wallSlot[r][c] = slot;
        while (top > 0) {
            top--;
            int rr = stack[top][0], cc = stack[top][1];
            static const int dr[4] = {-1, 1, 0, 0};
            static const int dc[4] = { 0, 0,-1, 1};
            for (int k=0;k<4;k++) {
                int nr = rr + dr[k], nc = cc + dc[k];
                if (nr<0||nr>=ROWS||nc<0||nc>=COLS) continue;
                if (!MAP[nr][nc] || g_wallSlot[nr][nc] != -1) continue;
                g_wallSlot[nr][nc] = slot;
                stack[top][0] = nr; stack[top][1] = nc; top++;
            }
        }
    }
}

// Build one wall mesh containing only cells whose connected-component slot
// matches the requested one. ComputeWallSlots() must be called first.
static Mesh BuildWallMesh(int slot, int slotCount) {
    (void)slotCount;
    // count exposed faces
    int faces = 0;
    for (int r=0;r<ROWS;r++) for (int c=0;c<COLS;c++) {
        if (!MAP[r][c]) continue;
        if (g_wallSlot[r][c] != slot) continue;
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
        if (g_wallSlot[r][c] != slot) continue;
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

// Circle-vs-grid wall check. Any wall cell whose AABB is within `rad` of
// (cx, cz) blocks. Smoother than 3-point shoulder tests — avoids false-block
// at wall corners where the body only grazes the cell diagonally.

// ── PLATFORM COLLISION ──────────────────────────────────────────────────────
// Returns true if the position (x, z) would penetrate a platform whose top is
// more than STEP_H above currentY (i.e. too tall to walk up onto).

// How far inside a too-tall platform's safety margin the point lies — 0 if the
// point is clear, positive if penetrating. Used so an entity that just fell off
// a platform (and so overlaps the edge from the side) can still move AWAY —
// allow moves only if they don't deepen penetration.

// Return the highest platform top the player can stand on at (x, z) given
// their current Y (i.e. any platform whose top is <= currentY + epsilon).
// Default ground is 0.

// Same as PlatGroundAt but with a radius margin — the entity steps onto a
// platform as soon as its body overlaps the platform footprint, not only once
// its centre crosses the strict edge. Used for enemies so they climb stairs
// smoothly when approaching from the side instead of clipping into the mesh.

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

// ── PARTICLES ────────────────────────────────────────────────────────────────

static void SpawnSplat(Vector3 pos) {
    if (!g_bloodSplatOK) return;
    // Round-robin overwrite — oldest splat dies if MAX_SPLATS is full, which
    // matches how the existing particle ring buffer behaves under heavy fire.
    static int next = 0;
    g_splats[next] = (BloodSplat){pos, 0, 0.f, true};
    next = (next + 1) % MAX_SPLATS;
}
static void UpdSplats(float dt) {
    const float frameDur = 1.f / BLOOD_SPLAT_FPS;
    for (int i = 0; i < MAX_SPLATS; i++) {
        BloodSplat *s = &g_splats[i];
        if (!s->alive) continue;
        s->frameT += dt;
        while (s->frameT >= frameDur) {
            s->frameT -= frameDur;
            s->frame++;
            if (s->frame >= BLOOD_SPLAT_FRAMES) { s->alive = false; break; }
        }
    }
}
static void DrawSplats(Camera3D cam) {
    if (!g_bloodSplatOK) return;
    for (int i = 0; i < MAX_SPLATS; i++) {
        BloodSplat *s = &g_splats[i];
        if (!s->alive) continue;
        Texture2D t = g_bloodSplatTex[s->frame];
        if (t.id == 0) continue;
        DrawBillboard(cam, t, s->pos, 1.0f, WHITE);
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
    g_lights[MUZZLE_LIGHT]=(LightDef){p,{1.0f,0.5f,0.1f},14.f,1}; ShaderSetLight(MUZZLE_LIGHT);
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
    // One QUAD + one SPEED at fixed map positions so a fresh run always has
    // a power-up to find. Wave bumps spawn fresh ones at random open cells.
    if (MAP[5][27]==0)  SpawnPick(27*CELL+CELL/2.f, 5*CELL+CELL/2.f, 5);  // QUAD, far NE
    if (MAP[14][2]==0)  SpawnPick( 2*CELL+CELL/2.f,14*CELL+CELL/2.f, 6);  // SPEED, far SW
    if (MAP[2][5]==0)   SpawnPick( 5*CELL+CELL/2.f, 2*CELL+CELL/2.f, 7);  // TESLA, near spawn
}

// Place flaming barrels at random open cells, spaced apart and away from
// the player spawn. Each barrel claims one shader-light slot in the
// BARREL_LIGHT_BASE..BARREL_LIGHT_BASE+NUM_BARRELS range; per-frame light
// updates happen in StepFrame's flicker pass.
static void SpawnBarrels(void) {
    g_barrelCount = 0;
    const float MIN_BARREL_SEP = 6.f;   // visual breathing room between barrels
    const float MIN_PLAYER_SEP = 8.f;   // don't put one in the player's face
    for (int idx = 0; idx < NUM_BARRELS; idx++) {
        bool placed = false;
        for (int tries = 0; tries < 80 && !placed; tries++) {
            int r = 1 + rand()%(ROWS-2), c = 1 + rand()%(COLS-2);
            if (MAP[r][c]) continue;
            float wx = c*CELL + CELL/2.f, wz = r*CELL + CELL/2.f;
            if (hypotf(wx - g_p.pos.x, wz - g_p.pos.z) < MIN_PLAYER_SEP) continue;
            bool tooClose = false;
            for (int k = 0; k < g_barrelCount; k++) {
                if (hypotf(wx - g_barrels[k].pos.x, wz - g_barrels[k].pos.z) < MIN_BARREL_SEP) {
                    tooClose = true; break;
                }
            }
            if (tooClose) continue;
            // Sit on top of any platform covering this cell — without this
            // a roll on the central staircase puts the barrel inside the
            // platform mesh. PlatGroundAt returns 0 if no platform covers
            // the cell, so the floor case is unchanged.
            float platTop = PlatGroundAt(wx, wz, 100.f);
            g_barrels[g_barrelCount].pos    = (Vector3){wx, platTop + 0.5f, wz};
            g_barrels[g_barrelCount].phase  = (float)rand()/RAND_MAX * 6.28318f;
            g_barrels[g_barrelCount].active = true;
            g_barrelCount++;
            placed = true;
        }
    }
    // Disable any unused barrel light slots so leftovers from a prior run
    // don't keep glowing in mid-air.
    for (int i = 0; i < NUM_BARRELS; i++) {
        if (i >= g_barrelCount) {
            g_lights[BARREL_LIGHT_BASE + i].enabled = 0;
            ShaderSetLight(BARREL_LIGHT_BASE + i);
        }
    }
}

// Drop a power-up at a random open cell at least 6m from the player AND not
// stacked on an existing live pickup (overlap reads as one chunky billboard
// and the player can't tell which they're grabbing). Called on wave bumps
// so power-ups refresh through the run instead of running out.
static void SpawnPowerupRandom(int type) {
    const float MIN_PICKUP_SEP = 1.6f;  // ~2x grab radius — clear visual gap
    for (int tries=0; tries<60; tries++) {
        int r=1+rand()%(ROWS-2), c=1+rand()%(COLS-2);
        if (MAP[r][c]) continue;
        float wx=c*CELL+CELL/2.f, wz=r*CELL+CELL/2.f;
        if (hypotf(wx-g_p.pos.x,wz-g_p.pos.z) < 6.f) continue;
        bool tooClose = false;
        for (int i=0; i<g_pkc; i++) {
            if (!g_pk[i].active) continue;
            if (hypotf(wx - g_pk[i].pos.x, wz - g_pk[i].pos.z) < MIN_PICKUP_SEP) {
                tooClose = true; break;
            }
        }
        if (tooClose) continue;
        SpawnPick(wx, wz, type);
        return;
    }
}
static void UpdPicks(void) {
    float t=GetTime();
    for (int i=0;i<g_pkc;i++) {
        Pickup *pk=&g_pk[i]; if (!pk->active) continue;
        // Sit on top of any platform covering this xz, otherwise on the floor.
        // Without this, pickups seeded on the staircase clip into the steps
        // and read as missing.
        float ground = 0.f;
        for (int p = 0; p < g_platCount; p++) {
            Platform *pl = &g_plats[p];
            if (pk->pos.x >= pl->x0 && pk->pos.x <= pl->x1 &&
                pk->pos.z >= pl->z0 && pk->pos.z <= pl->z1 &&
                pl->top > ground) ground = pl->top;
        }
        pk->pos.y=ground+0.5f+sinf(t*2.5f+i)*0.15f;
        float dx=pk->pos.x-g_p.pos.x, dz=pk->pos.z-g_p.pos.z;
        if (sqrtf(dx*dx+dz*dz)<1.2f) {
            // Don't grab a health pickup if we're already at full HP
            if (pk->type == 0 && g_p.hp >= g_p.maxHp) continue;
            pk->active=false;
            g_statPickups++;  // high-score stat
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
                // Power-ups: stack additively so two crates back-to-back is rewarded.
                case 5: g_p.quadT  = fminf(60.f, g_p.quadT  + 25.f);
                        if (g_p.quadT  > g_p.quadPeak)  g_p.quadPeak  = g_p.quadT;
                        strncpy(g_hypeMsg, "QUAD DAMAGE!", 79); g_hypeMsg[79]=0;
                        g_hypeDur = 2.5f; g_hypeT = g_hypeDur; break;
                case 6: g_p.hasteT = fminf(60.f, g_p.hasteT + 20.f);
                        if (g_p.hasteT > g_p.hastePeak) g_p.hastePeak = g_p.hasteT;
                        strncpy(g_hypeMsg, "SPEED BOOST!", 79); g_hypeMsg[79]=0;
                        g_hypeDur = 2.5f; g_hypeT = g_hypeDur; break;
                case 7: g_p.cells = (int)fminf(99, g_p.cells + 30);
                        if (!g_p.hasTesla) {
                            g_p.hasTesla = true;
                            g_p.weapon = 3; g_p.switchAnim = 0.3f;
                            strncpy(g_hypeMsg, "TESLA CANNON!", 79); g_hypeMsg[79]=0;
                            g_hypeDur = 2.5f; g_hypeT = g_hypeDur;
                            snprintf(g_msg,80,"TESLA CANNON +30 CELLS");
                        } else {
                            snprintf(g_msg,80,"+30 CELLS");
                        } break;
            }
        }
    }
}
// Animated flaming-barrel sprites — 3-frame cycle, per-barrel phase offset
// so they don't all pulse together. Drawn before the ceiling-light cones
// so the additive cones layer over the flame's opaque pixels.
static void DrawBarrels(Camera3D cam) {
    if (!g_barrelOK) return;
    float t = (float)GetTime();
    const float spriteH = 1.8f;
    for (int i = 0; i < g_barrelCount; i++) {
        Barrel *b = &g_barrels[i];
        if (!b->active) continue;
        int frame = ((int)((t + b->phase) * 10.f)) % 3;
        if (frame < 0) frame += 3;
        Vector3 pos = {b->pos.x, b->pos.y + spriteH*0.5f - 0.5f, b->pos.z};
        DrawBillboard(cam, g_barrelTex[frame], pos, spriteH, WHITE);
    }
}

// Volumetric-ish ceiling lights: visible fixture + translucent cone beam below.
// Iterates the static scene lights (slots [0..NUM_LAMPS)); the trailing
// MUZZLE_LIGHT / PULSE_LIGHT slots are dynamic and not drawn here.
static void DrawCeilingLights(Camera3D cam) {
    (void)cam;
    // Fixture geometry + light cone — additive blending for the "god ray" glow
    for (int i = 0; i < NUM_LAMPS; i++) {
        LightDef *L = &g_lights[i];
        if (g_lampBroken[i]) {
            // Shot-out fixture — keep the housing visible (charred dark grey)
            // with a tiny dim ember inside, so the spot doesn't read as empty.
            Vector3 fixturePos = L->pos;
            DrawCube(fixturePos, 0.7f, 0.15f, 0.7f, (Color){35, 30, 28, 255});
            DrawSphere((Vector3){fixturePos.x, fixturePos.y - 0.05f, fixturePos.z}, 0.10f, (Color){90,40,20,255});
            continue;
        }
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
    for (int i = 0; i < NUM_LAMPS; i++) {
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
        {200, 60,220,255},  // QUAD damage (magenta)
        { 40,200,255,255},  // SPEED boost (cyan)
        {180, 90,255,255},  // TESLA cells (electric purple)
    };
    // Size per ammo type (the sprites have different native sizes)
    static const float sz[] = {0.9f, 0.7f, 0.65f, 0.45f, 0.85f};
    const float teslaSz = 0.7f;
    float t = (float)GetTime();
    for (int i=0;i<g_pkc;i++) {
        Pickup *pk=&g_pk[i]; if (!pk->active) continue;
        // Power-ups have no sprite assets — draw a pulsing sphere with a
        // larger halo ring so they read as "special" against ammo crates.
        if (pk->type == 5 || pk->type == 6) {
            float pulse = 0.85f + 0.25f*sinf(t*4.f + (float)i);
            Color c = tc[pk->type];
            DrawSphere(pk->pos, 0.34f * pulse, c);
            // Two stacked halo rings rotating in opposite directions for that
            // "this is a power-up" tell. Additive feels too much; semi-transparent.
            DrawCircle3D(pk->pos, 0.65f, (Vector3){1,0,0}, 90.f + t*40.f, Fade(c, 0.55f));
            DrawCircle3D(pk->pos, 0.55f, (Vector3){0,0,1}, 90.f - t*60.f, Fade(c, 0.45f));
            continue;
        }
        Texture2D tex = {0};
        float drawSz = 0.7f;
        if (pk->type == 0 && g_healthTex[pk->variant].id) { tex = g_healthTex[pk->variant]; drawSz = sz[0]; }
        else if (pk->type > 0 && pk->type < 5 && g_ammoTex[pk->type].id) { tex = g_ammoTex[pk->type]; drawSz = sz[pk->type]; }
        else if (pk->type == 7 && g_teslaPickupTex.id) { tex = g_teslaPickupTex; drawSz = teslaSz; }

        if (tex.id) {
            DrawBillboard(cam, tex, pk->pos, drawSz, WHITE);
            // Tesla pickup advertises itself as a special weapon find: two
            // pulsing rings (one above, one below) plus a glowing core sphere
            // behind the sprite so it reads as "powered" against ammo crates.
            if (pk->type == 7) {
                float pulse = 0.85f + 0.25f*sinf(t*4.f + (float)i);
                Color c = tc[7];
                DrawSphere(pk->pos, 0.18f * pulse, c);
                DrawCircle3D((Vector3){pk->pos.x, pk->pos.y - 0.45f, pk->pos.z},
                             0.65f * pulse, (Vector3){1,0,0}, 90.f + t*40.f, Fade(c, 0.55f));
                DrawCircle3D((Vector3){pk->pos.x, pk->pos.y + 0.55f, pk->pos.z},
                             0.50f * pulse, (Vector3){1,0,0}, 90.f - t*60.f, Fade(c, 0.45f));
            }
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
int Alive(void) { int n=0; for(int i=0;i<g_ec;i++) if(g_e[i].active && !g_e[i].dying) n++; return n; }

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
    return t==0 ? "CHEF"  : t==1 ? "HEAVY" : t==2 ? "FAST"  : t==3 ? "BOSS"
         : t==4 ? "CULT"  : t==5 ? "MUTNT" : t==6 ? "MECH"
         : t==7 ? "SOLDR" : t==8 ? "CACO"  : t==9 ? "CYBER"
         : t==10 ? "REVN" : t==11 ? "LSOUL" : t==12 ? "PAINE" : "?";
}

static void DebugLogTick(void) {
    if (!g_dbgLog) return;
    double now = GetTime();
    if (now - g_dbgLastT < 0.2) return;   // 5 Hz
    g_dbgLastT = now;

    fprintf(g_dbgLog,
        "[t=%7.2fs] wave=%d bossMode=%d bossFight=%d  player: pos=(%.2f, %.2f, %.2f) "
        "yaw=%.2frad (%.0fdeg) hp=%.0f/%.0f weapon=%d\n",
        now, g_wave, g_arenaMode ? 1 : 0, g_bossInterlude ? 1 : 0,
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
            "hp=%4.0f/%4.0f pd=(%+.2f, %+.2f) flashT=%.2f bleedT=%.2f cd=%.2f\n",
            i, TypeName(e->type),
            e->dying ? "DYING" : "alive",
            e->pos.x, e->pos.y, e->pos.z,
            fxdir, fzdir, d,
            StateName(e->state),
            e->hp, e->maxHp,
            e->pd.x, e->pd.z,
            e->flashT, e->bleedT, e->cd);
        if (!e->dying) alive++;
    }
    int liveShots = 0;
    for (int k = 0; k < MAX_ESHOTS; k++) if (g_es[k].active) liveShots++;
    fprintf(g_dbgLog, "  (alive=%d, eshots=%d)\n\n", alive, liveShots);
    fflush(g_dbgLog);
}

static void KillEnemy(int i) {
    Enemy *e=&g_e[i];
    // Keep the enemy active=true so the sprite keeps rendering — mark dying so
    // AI/bullets/minimap/alive-count skip it.
    e->dying = true; e->deathT = 0.f; e->hp = 0.f;
    if (e->type == 6) {
        // Mech has no death sprite in the WolfenDoom source — it explodes
        // in two staggered blasts and scatters metal/spark debris, then the
        // actor is removed once deathT crosses MECH_DEATH_T (UpdEnemies).
        Vector3 cen = {e->pos.x, e->pos.y + 1.3f, e->pos.z};
        Vector3 top = {e->pos.x, e->pos.y + 2.3f, e->pos.z};
        Explode(cen);
        Explode(top);
        for (int p = 0; p < 40; p++) {
            Vector3 v = {
                ((float)rand()/RAND_MAX - 0.5f) * 14.f,
                (float)rand()/RAND_MAX * 9.f + 2.f,
                ((float)rand()/RAND_MAX - 0.5f) * 14.f
            };
            Color c = (rand() & 1) ? (Color){90, 100, 160, 255}    // metal
                                   : (Color){170, 90, 30, 255};    // rust/spark
            SpawnPart(top, v, c,
                      0.8f + (float)rand()/RAND_MAX * 0.7f,
                      0.10f + (float)rand()/RAND_MAX * 0.08f, true);
        }
        // No blood — it's a robot.
    } else {
        Vector3 bp={e->pos.x,1.0f,e->pos.z};
        // Massive blood burst on death — waist, chest, head for a gorey finish
        Blood(bp, 50);
        Blood((Vector3){e->pos.x, 1.6f, e->pos.z}, 35);
        Blood((Vector3){e->pos.x, 0.4f, e->pos.z}, 25);
    }
    // Death vocalisation + fatality stinger only for organic enemies — robots
    // (mech) get the explosion as their "death sound" instead.
    if (e->type != 6) {
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
        if (g_sFatalityOK && !g_lastHitHead) {
            SetSoundVolume(g_sFatality, 1.5f);
            PlaySound(g_sFatality);
        }
    }
    g_p.score+=e->score*g_wave; g_p.kills++; g_killsThisShot++;
    if (g_p.kills == 1 && g_sFirstBloodOK) {
        SetSoundVolume(g_sFirstBlood, 1.5f);
        PlaySound(g_sFirstBlood);
    }
    // Echoey first-blood-of-wave stinger — distinct from the per-run
    // first-blood above. Fires on the first kill of EACH wave; resets
    // when the wave advances (in InitGame for wave 1 and in the post-
    // boss "next wave" branch for waves 2+).
    if (!g_firstKillThisWave && g_sFirstBloodWaveOK && !g_bossInterlude) {
        SetSoundVolume(g_sFirstBloodWave, 1.5f);
        PlaySound(g_sFirstBloodWave);
        g_firstKillThisWave = true;
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
    static const char *names[]={"CHEF","HEAVY CHEF","FAST CHEF","BOSS","CULTIST","MUTANT","MECH",
                                "SOLDIER","CACODEMON","CYBER DEMON","REVENANT","LOST SOUL","PAIN ELEMENTAL"};
    int ti = (e->type >= 0 && e->type < (int)(sizeof(names)/sizeof(names[0]))) ? e->type : 0;
    char buf[80]; snprintf(buf,80,"%s DOWN  +%d",names[ti],e->score*g_wave);
    Msg(buf);
    // Hype text when the wave is down to its final chef. Matches the same
    // guard as the sOneLeft stinger above so audio + text always fire on the
    // same transition. Routed through the dedicated hype banner (bigger,
    // longer, flashing) instead of the regular Msg slot.
    if (!g_bossInterlude && Alive() == 1 && e->type != 3) {
        // Find the lone survivor and pick a hype line — one of which embeds
        // the survivor's enemy-type name so the banner correctly reflects
        // what's left (chef vs mutant vs mech etc.).
        int survIdx = -1;
        for (int s = 0; s < g_ec; s++) {
            if (g_e[s].active && !g_e[s].dying) { survIdx = s; break; }
        }
        const char *survName = "TARGET";
        if (survIdx >= 0) {
            int t = g_e[survIdx].type;
            survName = (t == 0) ? "CHEF" : (t == 1) ? "HEAVY CHEF" :
                       (t == 2) ? "FAST CHEF" : (t == 4) ? "CULTIST" :
                       (t == 5) ? "MUTANT" : (t == 6) ? "MECH" :
                       (t == 7) ? "SOLDIER" : (t == 8) ? "CACODEMON" :
                       (t == 9) ? "CYBER DEMON" : "TARGET";
        }
        char dynLine[80];
        snprintf(dynLine, sizeof(dynLine), "FINAL %s - HUNT HIM DOWN!", survName);
        const char *hype[] = {
            "LAST ONE STANDING!",
            "ONE LEFT - FINISH THEM!",
            dynLine,
            "LONE SURVIVOR - END THIS!",
            "ONE REMAINS - NO MERCY!",
        };
        strncpy(g_hypeMsg, hype[rand() % (int)(sizeof(hype)/sizeof(hype[0]))], 79);
        g_hypeMsg[79] = 0;
        g_hypeDur = 4.5f;
        g_hypeT   = g_hypeDur;
    }
    if (Alive()==0) {
        // Arena test mode — generic respawn for any picked enemy type.
        if (g_arenaMode) {
            int t = g_arenaType;
            int count = (t == 3 || t == 9) ? 1 : 8;  // boss + cyber demon — solo respawn
            static const char *names[] = {"CHEFS","HEAVY CHEFS","FAST CHEFS","BOSS","SS GUARDS","MUTANTS","MECHS","SOLDIERS","CACODEMONS","CYBER DEMON","REVENANTS","LOST SOULS","PAIN ELEMENTALS"};
            char buf[64]; snprintf(buf, 64, "%s DOWN - RESPAWN", names[(t>=0&&t<13)?t:0]);
            Msg(buf);
            for (int k = 0; k < count && g_ec < MAX_ENEMIES; k++) {
                for (int tries = 0; tries < 120; tries++) {
                    int r = 1 + rand()%(ROWS-2), c = 1 + rand()%(COLS-2);
                    if (MAP[r][c]) continue;
                    float wx = c*CELL+CELL/2.f, wz = r*CELL+CELL/2.f;
                    float minDist = (t == 3 || t == 6 || t == 9) ? 14.f : 10.f;
                    if (hypotf(wx-g_p.pos.x, wz-g_p.pos.z) < minDist) continue;
                    Enemy *ne = &g_e[g_ec++]; *ne = (Enemy){0};
                    ne->pos = (Vector3){wx, PlatGroundAt(wx,wz,100.f), wz};
                    ne->type = t; ne->state = ES_CHASE;
                    if      (t == 8)  ne->pos.y = 1.5f;  // cacodemon
else if (t == 11) ne->pos.y = 2.2f;  // lost soul
else if (t == 12) ne->pos.y = 2.0f;  // pain elemental
                    ne->hp = ne->maxHp = ET_HP[t];
                    ne->speed = ET_SPD[t];
                    ne->dmg = ET_DMG[t]; ne->rate = ET_RATE[t];
                    ne->cd = (t == 5) ? 0.4f : (t == 6) ? 0.6f : ET_RATE[t];
                    ne->alertR = ET_AR[t]; ne->atkR = ET_ATK[t];
                    ne->score = ET_SC[t]; ne->active = true;
                    float ang = (float)rand()/RAND_MAX * 6.28f;
                    ne->pd = (Vector3){sinf(ang), 0, cosf(ang)};
                    ne->stateT = 1.f + (float)rand()/RAND_MAX * 2.f;
                    break;
                }
            }
            return;
        }

        // Normal flow:
        //   1. Chefs clear → spawn a boss (interlude begins)
        //   2. Boss dies  → wave++, spawn next wave of chefs
        if (!g_bossInterlude) {
            g_bossInterlude = true;
            if (g_sNextWaveOK) { SetSoundVolume(g_sNextWave, 1.5f); PlaySound(g_sNextWave); }
            // Wave 2 boss is the cyber demon — heavier, ranged. Other waves
            // use the regular chef boss (type 3).
            int bt = (g_wave == 2) ? 9 : 3;
            const char *bmsg = (bt == 9) ? "-- CYBER DEMON --" : "-- BOSS FIGHT --";
            Msg(bmsg);
            Enemy *ne = &g_e[g_ec++];
            *ne = (Enemy){0};
            float bx, bz; PickBossSpawn(&bx, &bz);
            ne->pos = (Vector3){bx, PlatGroundAt(bx, bz, 100.f), bz};
            ne->type = bt; ne->state = ES_CHASE;  // hunts on spawn — no wander phase
            float hm = 1.f + g_wave*0.12f;
            ne->hp = ne->maxHp = ET_HP[bt] * hm;
            ne->speed = ET_SPD[bt] + g_wave*0.10f;
            ne->dmg = ET_DMG[bt];
            ne->rate = ne->cd = ET_RATE[bt];
            ne->alertR = ET_AR[bt]; ne->atkR = ET_ATK[bt];
            ne->score = ET_SC[bt]; ne->active = true;
            float ang = (float)rand()/RAND_MAX * 6.28f;
            ne->pd = (Vector3){sinf(ang), 0, cosf(ang)};
            ne->stateT = 1.f + (float)rand()/RAND_MAX * 2.f;
            return;
        }
        // Boss just died — bump wave and kick off the next chef round
        g_bossInterlude = false;
        g_wave++;
        g_firstKillThisWave = false;  // re-arm wave first-blood stinger
        if (g_sNextWaveOK) {
            SetSoundVolume(g_sNextWave, 1.5f);
            PlaySound(g_sNextWave);
        }
        char wbuf[64]; snprintf(wbuf,64,"-- WAVE %d INCOMING --",g_wave);
        Msg(wbuf);
        // Refresh one of each power-up at a random open cell so a long run
        // always has a quad or speed to chase.
        SpawnPowerupRandom(5);
        SpawnPowerupRandom(6);
        // spawn next wave
        int cnt=10+g_wave*4;
        for (int k=0;k<cnt&&g_ec<MAX_ENEMIES;k++) {
            for (int tries=0;tries<120;tries++) {
                int r=1+rand()%(ROWS-2), c=1+rand()%(COLS-2);
                if (MAP[r][c]) continue;
                float wx=c*CELL+CELL/2.f, wz=r*CELL+CELL/2.f;
                if (hypotf(wx-g_p.pos.x,wz-g_p.pos.z)<10.f) continue;
                float rng=(float)rand()/RAND_MAX;
                // Wave roll (ranged enemies always show up). Soldier (7)
                // joins from wave 2; cacodemon (8) joins from wave 2;
                // cyber demon (9) is rare, only from wave 3+.
                int type;
                if (g_wave < 2)            type = (rng < 0.66f) ? 0
                                                : (rng < 0.82f) ? 5
                                                : (rng < 0.92f) ? 6
                                                :                7;   // SOLDIER (no caco/cyber pre-wave 2)
                else if (g_wave < 3)       type = (rng < 0.34f) ? 0
                                                : (rng < 0.50f) ? 2
                                                : (rng < 0.60f) ? 1
                                                : (rng < 0.72f) ? 5
                                                : (rng < 0.80f) ? 6
                                                : (rng < 0.92f) ? 7    // SOLDIER
                                                :                8;   // CACO
                else                       type = (rng < 0.22f) ? 0
                                                : (rng < 0.40f) ? 2
                                                : (rng < 0.50f) ? 1
                                                : (rng < 0.60f) ? 4    // CULTIST
                                                : (rng < 0.72f) ? 5    // MUTANT
                                                : (rng < 0.80f) ? 6    // MECH
                                                : (rng < 0.90f) ? 7    // SOLDIER
                                                : (rng < 0.97f) ? 8    // CACO
                                                :                9;   // CYBER (rare)
                Enemy *ne=&g_e[g_ec++];
                *ne=(Enemy){0};
                ne->pos=(Vector3){wx, PlatGroundAt(wx,wz,100.f), wz};  // snap to highest platform top
                ne->type=type;
                if      (type == 8)  ne->pos.y = 1.5f;  // cacodemon
                else if (type == 11) ne->pos.y = 2.2f;  // lost soul
                else if (type == 12) ne->pos.y = 2.0f;  // pain elemental
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
    // High-score stat: count damage that landed on a live target. Cap at
    // remaining HP so overkill doesn't inflate the number.
    g_statDamage += (d > e->hp) ? e->hp : d;
    e->hp-=d; e->flashT=0.12f; e->state=ES_CHASE;
    if (e->hp<=0) KillEnemy(i);
}

static void UpdEnemies(float dt) {
    for (int i=0;i<g_ec;i++) {
        Enemy *e=&g_e[i]; if (!e->active) continue;
        if (e->dying) {
            e->deathT += dt;
            // Mech has no corpse sprite — once the explosion has played out
            // the actor is removed entirely instead of leaving a static body.
            if (e->type == 6 && e->deathT > 1.0f) e->active = false;
            // Flying enemies (caco / lost soul / pain elemental) fall to the
            // floor when killed — accelerating gravity, capped at y=0.
            if ((e->type == 8 || e->type == 11 || e->type == 12) && e->pos.y > 0.f) {
                float fallSpeed = 2.f + e->deathT * 12.f;
                e->pos.y -= fallSpeed * dt;
                if (e->pos.y < 0.f) e->pos.y = 0.f;
            }
            continue;  // corpse: run death timer, skip AI
        }
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
        // PATROL→CHASE alert. Must be guarded on PATROL — without the guard
        // this overwrites ATTACK back to CHASE every frame, so the ATTACK
        // branch below never executes (cd ticks past 0 forever, mutant
        // never fires, melee chef never damages).
        if (dist<e->alertR && e->state==ES_PATROL) e->state=ES_CHASE;
        e->cd-=dt; e->stateT-=dt; e->legT+=dt*e->speed*2.8f;
        if (e->state==ES_PATROL) {
            EnemyMove(e, e->pd.x*e->speed*dt, e->pd.z*e->speed*dt);
            // Snap enemy Y to highest reachable platform under them (auto-step stairs).
            // Use body-radius margin so the chef steps up as soon as his footprint
            // overlaps a step, not only once his centre crosses the strict edge.
            // Cacodemon (8) is flying — keep its current y, ignore platforms.
            if (e->type != 8 && e->type != 11 && e->type != 12) {
                float er = (e->type == 3) ? 1.2f : 0.42f;
                e->pos.y = PlatGroundAtR(e->pos.x, e->pos.z, e->pos.y + STEP_H, er);
            }
            if (e->stateT<0){float a=(float)rand()/RAND_MAX*6.28f;e->pd=(Vector3){sinf(a),0,cosf(a)};e->stateT=1.f+(float)rand()/RAND_MAX*2.5f;}
        } else if (e->state==ES_CHASE) {
            // Mutants pursue while LOS is blocked even when nominally in
            // atkR — otherwise they'd ping-pong CHASE↔ATTACK without moving
            // (CHASE only moves when dist > atkR; ATTACK kicks back to
            // CHASE on blocked LOS). This lets them step out and shoot.
            bool mutBlocked = false;
            if ((e->type == 5 || e->type == 6 || e->type == 8 || e->type == 9) && dist <= e->atkR) {
                float muzzleY = (e->type == 6) ? 2.6f
                              : (e->type == 9) ? 2.6f
                              : (e->type == 8) ? 0.0f   // caco fires from sprite centre (already at y~1.5)
                              :                  1.55f;
                Vector3 mp = {e->pos.x, e->pos.y + muzzleY, e->pos.z};
                Vector3 pp = {g_p.pos.x, g_p.pos.y + EYE_H - 0.4f, g_p.pos.z};
                mutBlocked = !TeslaLOS(mp, pp);
            }
            if (dist>e->atkR || mutBlocked) {
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
                if (e->type != 8 && e->type != 11 && e->type != 12) {
                    e->pos.y = PlatGroundAtR(e->pos.x, e->pos.z, e->pos.y + STEP_H, r);
                }
            }
            // Only enter ATTACK when the player is on roughly the same level —
            // otherwise a chef on the floor next to a raised platform would
            // melee-loop at the wall while the player hovers above on the deck.
            // Forcing CHASE makes the enemy pathfind around to the stairs.
            // Ranged enemies (mutant/mech/caco/cyber) can shoot up at any height.
            else if (e->type == 5 || e->type == 6 || e->type == 8 || e->type == 9 ||
                     e->type == 11 || e->type == 12 ||
                     fabsf(g_p.pos.y - e->pos.y) <= STEP_H + 0.1f) e->state=ES_ATTACK;
        } else {
            // Same y-reachability check — if the player has jumped or climbed
            // onto a different level, break out of ATTACK and go back to CHASE
            // so the enemy resumes pathfinding. Mutants exempt from the y
            // gate (they're ranged), but they DO check wall LOS — if a wall
            // sits between them and the player they fall back to CHASE so the
            // navigator pathfinds them around the corner instead of standing
            // there shooting projectiles into the wall.
            bool isMutant = (e->type == 5);
            bool isMech   = (e->type == 6);
            bool isCaco   = (e->type == 8);
            bool isCyber  = (e->type == 9);
            bool isRanged = isMutant || isMech || isCaco || isCyber;
            bool losBlocked = false;
            if (isRanged) {
                float muzzleY = (isMech || isCyber) ? 2.6f : isCaco ? 0.0f : 1.55f;
                Vector3 mp = {e->pos.x, e->pos.y + muzzleY, e->pos.z};
                Vector3 pp = {g_p.pos.x, g_p.pos.y + EYE_H - 0.4f, g_p.pos.z};
                losBlocked = !TeslaLOS(mp, pp);
            }
            if (dist>e->atkR*1.5f ||
                (!isRanged && fabsf(g_p.pos.y - e->pos.y) > STEP_H + 0.1f) ||
                losBlocked) e->state=ES_CHASE;
            if (e->cd<=0) {
                if (isMech) {
                    // Heavy mech: lobs a slow rocket with splash damage.
                    Vector3 muzzle = {e->pos.x, e->pos.y + 2.3f, e->pos.z};
                    Vector3 target = {g_p.pos.x, g_p.pos.y + EYE_H - 0.4f, g_p.pos.z};
                    SpawnEShotRocket(muzzle, target, e->dmg);
                    PlayPositionalSound(g_sMechRocketOK ? g_sMechRocket : g_sRocket, e->pos);
                    e->cd = e->rate;
                } else if (isCyber) {
                    // Cyber demon: same heavy rocket as mech but bigger damage
                    // and twin barrel — fire two staggered rockets per cycle
                    // for that "BFG hallway" feel.
                    Vector3 muzzle = {e->pos.x, e->pos.y + 2.3f, e->pos.z};
                    Vector3 target = {g_p.pos.x, g_p.pos.y + EYE_H - 0.4f, g_p.pos.z};
                    SpawnEShotRocket(muzzle, target, e->dmg);
                    Sound cyS = g_sCyberFireOK ? g_sCyberFire : g_sRocket;
                    if (cyS.frameCount) PlayPositionalSound(cyS, e->pos);
                    e->cd = e->rate;
                } else if (isCaco) {
                    // Cacodemon: flying fireball, slower than mutant ball but
                    // hits hard. Muzzle == sprite centre (already y~1.5).
                    Vector3 muzzle = {e->pos.x, e->pos.y + 0.0f, e->pos.z};
                    Vector3 target = {g_p.pos.x, g_p.pos.y + EYE_H - 0.4f, g_p.pos.z};
                    SpawnEShot(muzzle, target, e->dmg);
                    PlayPositionalSound(g_sMutAttackOK ? g_sMutAttack : g_sPistol, e->pos);
                    e->cd = e->rate;
                } else if (isMutant) {
                    // Ranged mutant: spawn an energy-ball projectile aimed at
                    // the player's chest. Fire whenever the cooldown elapses —
                    // if a wall is in the way the projectile dies on impact,
                    // but the player at least sees the shot. (LOS still gates
                    // the ATTACK→CHASE transition above so the mutant pursues
                    // when it can't see the player.)
                    Vector3 muzzle = {e->pos.x, e->pos.y + 1.55f, e->pos.z};
                    Vector3 target = {g_p.pos.x, g_p.pos.y + EYE_H - 0.4f, g_p.pos.z};
                    SpawnEShot(muzzle, target, e->dmg);
                    PlayPositionalSound(g_sMutAttackOK ? g_sMutAttack : g_sPistol, e->pos);
                    e->cd = e->rate;
                } else {
                    // Cultist (4) AND soldier (7) both fire a hitscan tracer:
                    // muzzle-flash sparks at gun height, yellow tracer dots to
                    // the player, shotgun sound. Damage still applies instantly
                    // so the gameplay role of "close-quarters ranged threat" is
                    // unchanged from the cultist.
                    if (e->type == 4 || e->type == 7) {
                        Vector3 muzzle = {e->pos.x, e->pos.y + 1.55f, e->pos.z};
                        Vector3 tgt    = {g_p.pos.x, g_p.pos.y + EYE_H - 0.4f, g_p.pos.z};
                        Sparks(muzzle, 6);
                        // Quick yellow tracer dots from the gun toward the player
                        for (int t = 1; t <= 6; t++) {
                            float u = (float)t / 7.f;
                            Vector3 p = { muzzle.x + (tgt.x - muzzle.x)*u,
                                          muzzle.y + (tgt.y - muzzle.y)*u,
                                          muzzle.z + (tgt.z - muzzle.z)*u };
                            SpawnPart(p, (Vector3){0,0,0},
                                      (Color){255, 220, 80, 255}, 0.08f, 0.06f, false);
                        }
                        // Per-type fire sample dispatch (soldier MG / SS G36
                        // / shotgun fallback). Routed through
                        // PlayPositionalSound so g_warnPan can stereo-pan
                        // the sound based on enemy bearing.
                        Sound fireS = (e->type == 7 && g_sSoldierMGOK) ? g_sSoldierMG
                                    : (e->type == 4 && g_sSSFireOK)    ? g_sSSFire
                                    :                                    g_sShotgun;
                        PlayPositionalSound(fireS, e->pos);
                    }
                    if (!g_god) g_p.hp-=e->dmg;
                    g_p.hurtFlash=0.22f; g_p.shake=fmaxf(g_p.shake,0.16f);
                    if (g_sChefHitOK) PlaySound(g_sChefHit);
                    else              PlaySound(g_sHurt);
                    e->cd=e->rate;
                    if (g_p.hp<=0){g_p.dead=true;g_gs=GS_DEAD;}
                }
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
            // Same windup-telegraph window as chefs: last 0.45s before the
            // melee tick lands shows the E-frame swing pose.
            bool inAtkWindup = (e->state == ES_ATTACK) && (e->cd > 0.f) && (e->cd < 0.45f);
            if (isCorpse) {
                int df = (int)(e->deathT / BOSS_DEATH_FRAME_TIME);
                if (df > 3) df = 3;               // 4 death frames (G/H/I/J)
                tex = g_bossDeathTex[df];
            } else if (e->flashT > 0.f && g_bossPainOK) {
                tex = g_bossPainTex;
            } else if (inAtkWindup && g_bossAtkOK) {
                tex = g_bossAtkTex;
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

        // CULTIST (type 4) — 8-directional sprites. Walk A-D, pain E, death I-M.
        // Sprite slot picked from the angle of the player relative to the
        // cultist's facing direction (forward = pd in PATROL, toward player
        // in CHASE/ATTACK). Mirror pairs (rot 2/8, 3/7, 4/6) drawn via a
        // negative source.width on DrawBillboardPro so we don't carry double
        // the textures in memory.
        if (e->type == 4 && g_cultOK) {
            // Standing pose is ~107 px tall = 2 m. Use that ratio for ALL
            // frames so death sprites — which shrink in pixel height as the
            // corpse collapses — also shrink in world height, sitting on
            // the floor instead of stretched up to fill a fixed 2 m box.
            const float PX_PER_M = 107.f / 2.0f;
            float spriteH = 2.0f;

            Texture2D tex = {0};
            bool flip = false;
            bool isCorpse = e->dying && g_cultDeathOK;
            if (isCorpse) {
                int df = (int)(e->deathT / CULT_DEATH_FRAME_TIME);
                if (df > 4) df = 4;
                tex = g_cultDeathTex[df];
            } else {
                // Cultist's facing direction
                Vector3 facing;
                if (e->state == ES_PATROL) {
                    facing = e->pd;
                } else {
                    float fdx = g_p.pos.x - e->pos.x, fdz = g_p.pos.z - e->pos.z;
                    float l = sqrtf(fdx*fdx + fdz*fdz);
                    facing = (l > 0.001f) ? (Vector3){fdx/l, 0, fdz/l} : (Vector3){0, 0, 1};
                }
                // Player position relative to cultist's forward — relAngle in
                // [-180, 180] degrees, 0 = player straight ahead of cultist.
                float enemyYaw  = atan2f(facing.x, facing.z);
                float pdx = g_p.pos.x - e->pos.x, pdz = g_p.pos.z - e->pos.z;
                float playerYaw = atan2f(pdx, pdz);
                float rel = playerYaw - enemyYaw;
                while (rel >  PI) rel -= 2.f*PI;
                while (rel < -PI) rel += 2.f*PI;
                float relDeg = rel * RAD2DEG;

                // Bin into 8 octants — rot 1 centered on 0°.
                float a = relDeg + 22.5f;
                while (a < 0)    a += 360.f;
                while (a >= 360) a -= 360.f;
                int idx = (int)(a / 45.f) % 8;
                // Doom convention: 0=rot1 front, 4=rot5 back. Rotations 6/7/8
                // mirror 4/3/2 respectively (same image flipped horizontally).
                static const int   slots[8] = {0, 1, 2, 3, 4, 3, 2, 1};
                static const bool  flips[8] = {false,false,false,false,false,true,true,true};
                int slot = slots[idx];
                flip = flips[idx];

                DirFrame *frame;
                // Attack windup: SSCT G fires for the last ~0.45s of the cooldown
                // before the melee tick lands — same telegraph as chefs.
                bool inAtkWindup = (e->state == ES_ATTACK) && (e->cd > 0.f) && (e->cd < 0.45f);
                if (e->flashT > 0.f && g_cultPainOK) {
                    frame = &g_cultPain;
                } else if (inAtkWindup && g_cultAtkOK) {
                    frame = &g_cultAtk;
                } else {
                    int wf = (int)(e->legT * 0.6f) % 4;
                    if (wf < 0) wf += 4;
                    frame = &g_cultWalk[wf];
                }
                if (frame->ok) tex = frame->rot[slot];
            }

            if (tex.id) {
                // Per-frame world-height from pixel-height. Walk frames stay
                // at ~2 m; final corpse (M0, 64 px) ends up ~1.20 m and lies
                // visibly on the ground.
                spriteH = (float)tex.height / PX_PER_M;
                Vector3 pos = {e->pos.x, e->pos.y + spriteH * 0.5f, e->pos.z};
                Rectangle src = {0, 0, (float)tex.width, (float)tex.height};
                if (flip) src.width = -src.width;
                float aspect = (float)tex.width / (float)tex.height;
                Vector2 size   = {spriteH * aspect, spriteH};
                Vector2 origin = {size.x * 0.5f, size.y * 0.5f};
                DrawBillboardPro(cam, tex, src, pos, (Vector3){0,1,0}, size, origin, 0.f, WHITE);
            }
            // HP bar (mirrors the chef path) — only when alive and damaged
            if (!e->dying && e->hp < e->maxHp) {
                float hp = e->hp / e->maxHp;
                float bary = e->pos.y + spriteH + 0.25f;
                Vector3 HF = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
                Vector3 HR = Vector3Normalize(Vector3CrossProduct(HF, cam.up));
                Vector3 HU = Vector3CrossProduct(HR, HF);
                const float W = 0.82f, H = 0.10f;
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

        // RANGED MUTANT (type 5) — same 8-rotation scheme as cultist, but the
        // frame picker also uses e->cd to time attack windup vs fire frames.
        if (e->type == 5 && g_mutOK) {
            const float PX_PER_M = 84.f / 2.0f;  // walk frame ~84 px → 2 m
            float spriteH = 2.0f;

            Texture2D tex = {0};
            bool flip = false;
            bool isCorpse = e->dying && g_mutDeathOK;
            if (isCorpse) {
                int df = (int)(e->deathT / MUT_DEATH_FRAME_TIME);
                if (df > 3) df = 3;
                tex = g_mutDeathTex[df];
            } else {
                Vector3 facing;
                if (e->state == ES_PATROL) {
                    facing = e->pd;
                } else {
                    float fdx = g_p.pos.x - e->pos.x, fdz = g_p.pos.z - e->pos.z;
                    float l = sqrtf(fdx*fdx + fdz*fdz);
                    facing = (l > 0.001f) ? (Vector3){fdx/l, 0, fdz/l} : (Vector3){0, 0, 1};
                }
                float enemyYaw  = atan2f(facing.x, facing.z);
                float pdx = g_p.pos.x - e->pos.x, pdz = g_p.pos.z - e->pos.z;
                float playerYaw = atan2f(pdx, pdz);
                float rel = playerYaw - enemyYaw;
                while (rel >  PI) rel -= 2.f*PI;
                while (rel < -PI) rel += 2.f*PI;
                float relDeg = rel * RAD2DEG;
                float a = relDeg + 22.5f;
                while (a < 0)    a += 360.f;
                while (a >= 360) a -= 360.f;
                int idx = (int)(a / 45.f) % 8;
                static const int   slots[8] = {0, 1, 2, 3, 4, 3, 2, 1};
                static const bool  flips[8] = {false,false,false,false,false,true,true,true};
                int slot = slots[idx];
                flip = flips[idx];

                DirFrame *frame;
                // Attack-state frame timing using e->cd:
                //   cd in (rate-0.25 .. rate]  → just-fired G frame
                //   cd in [0 .. 0.40]          → charging E frame (windup)
                //   otherwise                  → walk cycle (or pain on flashT)
                if (e->flashT > 0.f && g_mutPainOK) {
                    frame = &g_mutPain;
                } else if (e->state == ES_ATTACK && g_mutAtkOK && e->cd > e->rate - 0.25f) {
                    frame = &g_mutAtkFire;
                } else if (e->state == ES_ATTACK && g_mutAtkOK && e->cd > 0.f && e->cd < 0.40f) {
                    frame = &g_mutAtkCharge;
                } else {
                    int wf = (int)(e->legT * 0.5f) % 4;
                    if (wf < 0) wf += 4;
                    frame = &g_mutWalk[wf];
                }
                if (frame->ok) tex = frame->rot[slot];
            }

            if (tex.id) {
                spriteH = (float)tex.height / PX_PER_M;
                // Live frames anchor at feet; corpse frames in this sprite set
                // have the body in the lower-middle of a square canvas with
                // empty space above, so a 0.5*H centre lifts the corpse ~half a
                // metre off the floor. Pull it down to sit flat on the ground.
                float yc = isCorpse ? (e->pos.y + spriteH * 0.18f)
                                    : (e->pos.y + spriteH * 0.5f);
                Vector3 pos = {e->pos.x, yc, e->pos.z};
                Rectangle src = {0, 0, (float)tex.width, (float)tex.height};
                if (flip) src.width = -src.width;
                float aspect = (float)tex.width / (float)tex.height;
                Vector2 size   = {spriteH * aspect, spriteH};
                Vector2 origin = {size.x * 0.5f, size.y * 0.5f};
                DrawBillboardPro(cam, tex, src, pos, (Vector3){0,1,0}, size, origin, 0.f, WHITE);
            }
            // HP bar (mirrors cultist path) — only when alive and damaged
            if (!e->dying && e->hp < e->maxHp) {
                float hp = e->hp / e->maxHp;
                float bary = e->pos.y + spriteH + 0.25f;
                Vector3 HF = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
                Vector3 HR = Vector3Normalize(Vector3CrossProduct(HF, cam.up));
                Vector3 HU = Vector3CrossProduct(HR, HF);
                const float W = 0.82f, H = 0.10f;
                Vector3 bgTL = {e->pos.x - HR.x*W*0.5f + HU.x*H*0.5f,
                                bary       - HR.y*W*0.5f + HU.y*H*0.5f,
                                e->pos.z - HR.z*W*0.5f + HU.z*H*0.5f};
                Vector3 bgTR = Vector3Add(bgTL, Vector3Scale(HR, W));
                Vector3 bgBL = Vector3Subtract(bgTL, Vector3Scale(HU, H));
                Vector3 bgBR = Vector3Subtract(bgTR, Vector3Scale(HU, H));
                rlBegin(RL_TRIANGLES);
                rlColor4ub(0,0,0,180);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBL.x,bgBL.y,bgBL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z); rlVertex3f(bgTR.x,bgTR.y,bgTR.z);
                Vector3 fTR = Vector3Add(bgTL, Vector3Scale(HR, W*hp));
                Vector3 fBR = Vector3Add(bgBL, Vector3Scale(HR, W*hp));
                rlColor4ub(80,200,90,255);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBL.x,bgBL.y,bgBL.z); rlVertex3f(fBR.x,fBR.y,fBR.z);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(fBR.x,fBR.y,fBR.z); rlVertex3f(fTR.x,fTR.y,fTR.z);
                rlEnd();
            }
            continue;  // skip chef/fallback drawing for this enemy
        }

        // HEAVY MECH (type 6) — uses N (idle) when standing/patrolling, A-D
        // for walking, alternating F/G arms during attack windows. No corpse
        // sprite — KillEnemy triggers an explosion and the actor is removed
        // by UpdEnemies once deathT crosses 1s, so we just stop drawing the
        // billboard the moment death begins.
        if (e->type == 6 && e->dying) continue;
        if (e->type == 6 && g_mechOK) {
            const float PX_PER_M = 105.f / 2.6f;  // ~105 px tall mech sprite → 2.6 m
            float spriteH = 2.6f;
            Texture2D tex = {0};
            bool flip = false;
            // Pick rotation slot from facing-vs-player angle (same code as cultist)
            Vector3 facing;
            if (e->state == ES_PATROL) {
                facing = e->pd;
            } else {
                float fdx = g_p.pos.x - e->pos.x, fdz = g_p.pos.z - e->pos.z;
                float l = sqrtf(fdx*fdx + fdz*fdz);
                facing = (l > 0.001f) ? (Vector3){fdx/l, 0, fdz/l} : (Vector3){0, 0, 1};
            }
            float enemyYaw  = atan2f(facing.x, facing.z);
            float pdx = g_p.pos.x - e->pos.x, pdz = g_p.pos.z - e->pos.z;
            float playerYaw = atan2f(pdx, pdz);
            float rel = playerYaw - enemyYaw;
            while (rel >  PI) rel -= 2.f*PI;
            while (rel < -PI) rel += 2.f*PI;
            float relDeg = rel * RAD2DEG;
            float a = relDeg + 22.5f;
            while (a < 0)    a += 360.f;
            while (a >= 360) a -= 360.f;
            int idx = (int)(a / 45.f) % 8;
            static const int   slots[8] = {0, 1, 2, 3, 4, 3, 2, 1};
            static const bool  flips[8] = {false,false,false,false,false,true,true,true};
            int slot = slots[idx];
            flip = flips[idx];

            DirFrame *frame = NULL;
            // Fire frame timing: last 0.40s after firing (cd > rate-0.40),
            // alternating arm based on a per-shot counter via cd modulo.
            if (e->state == ES_ATTACK && g_mechFireOK && e->cd > e->rate - 0.40f) {
                // Pick L or R arm — flip per shot using stateT parity
                bool leftArm = (((int)(e->stateT * 100.f)) & 1);
                frame = leftArm ? &g_mechFireL : &g_mechFireR;
            } else if (e->state == ES_PATROL || e->state == ES_ATTACK) {
                frame = g_mechIdleOK ? &g_mechIdle : &g_mechWalk[0];
            } else {
                int wf = (int)(e->legT * 0.5f) % 4;
                if (wf < 0) wf += 4;
                frame = &g_mechWalk[wf];
            }
            if (frame && frame->ok) tex = frame->rot[slot];

            if (tex.id) {
                spriteH = (float)tex.height / PX_PER_M;
                Vector3 pos = {e->pos.x, e->pos.y + spriteH * 0.5f, e->pos.z};
                Rectangle src = {0, 0, (float)tex.width, (float)tex.height};
                if (flip) src.width = -src.width;
                float aspect = (float)tex.width / (float)tex.height;
                Vector2 size   = {spriteH * aspect, spriteH};
                Vector2 origin = {size.x * 0.5f, size.y * 0.5f};
                DrawBillboardPro(cam, tex, src, pos, (Vector3){0,1,0}, size, origin, 0.f, WHITE);
            }
            // HP bar above the head — fatter to match the mech's bulk
            if (!e->dying && e->hp < e->maxHp) {
                float hp = e->hp / e->maxHp;
                float bary = e->pos.y + spriteH + 0.30f;
                Vector3 HF = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
                Vector3 HR = Vector3Normalize(Vector3CrossProduct(HF, cam.up));
                Vector3 HU = Vector3CrossProduct(HR, HF);
                const float W = 1.10f, H = 0.13f;
                Vector3 bgTL = {e->pos.x - HR.x*W*0.5f + HU.x*H*0.5f,
                                bary       - HR.y*W*0.5f + HU.y*H*0.5f,
                                e->pos.z - HR.z*W*0.5f + HU.z*H*0.5f};
                Vector3 bgTR = Vector3Add(bgTL, Vector3Scale(HR, W));
                Vector3 bgBL = Vector3Subtract(bgTL, Vector3Scale(HU, H));
                Vector3 bgBR = Vector3Subtract(bgTR, Vector3Scale(HU, H));
                rlBegin(RL_TRIANGLES);
                rlColor4ub(0,0,0,180);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBL.x,bgBL.y,bgBL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z); rlVertex3f(bgTR.x,bgTR.y,bgTR.z);
                Vector3 fTR = Vector3Add(bgTL, Vector3Scale(HR, W*hp));
                Vector3 fBR = Vector3Add(bgBL, Vector3Scale(HR, W*hp));
                rlColor4ub(255,140,40,255);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBL.x,bgBL.y,bgBL.z); rlVertex3f(fBR.x,fBR.y,fBR.z);
                rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(fBR.x,fBR.y,fBR.z); rlVertex3f(fTR.x,fTR.y,fTR.z);
                rlEnd();
            }
            continue;
        }

        // PreviewEnemy types 7..12 — DOOM-style + Beautiful-Doom imports.
        // The walk_N frames are 8-direction rotation VIEWS (Doom convention,
        // mirror pairs collapsed to 5 unique), so the standing/walking pose
        // is picked from the enemy-facing-vs-player angle — without this
        // they appear to spin in place. atk/pain/death are real animation
        // sequences and DO cycle on time. Caco (8) and Lost Soul (11) are
        // flying; Pain Elemental (12) is also a floater.
        if (e->type >= 7 && e->type <= 12) {
            PreviewEnemy *pe = (e->type == 7)  ? &g_prevSoldier
                              : (e->type == 8)  ? &g_prevCaco
                              : (e->type == 9)  ? &g_prevCyber
                              : (e->type == 10) ? &g_prevRevenant
                              : (e->type == 11) ? &g_prevLostSoul
                                                : &g_prevPainElem;
            if (pe->ok) {
                // Reference (live walking) sprite height per type. Each
                // frame's actual world height is scaled by tex.height /
                // walk[0].height so death corpses (smaller pixel sprites)
                // shrink correctly toward the floor instead of being
                // stretched up to fill the live size.
                float baseSpriteH = (e->type == 7)  ? 2.0f
                                  : (e->type == 8)  ? 2.2f
                                  : (e->type == 9)  ? 3.4f
                                  : (e->type == 10) ? 2.4f
                                  : (e->type == 11) ? 1.0f
                                                    : 2.6f;
                Texture2D tex = {0};
                bool isCorpse = e->dying && pe->deathCount > 0;
                bool inAtkWindup = (e->state == ES_ATTACK) && (e->cd > 0.f) && (e->cd < 0.45f);
                if (isCorpse) {
                    int df = (int)(e->deathT / DGAME_DEATH_FRAME_TIME);
                    if (df >= pe->deathCount) df = pe->deathCount - 1;
                    tex = pe->death[df];
                } else if (e->flashT > 0.f && pe->painCount > 0) {
                    tex = pe->pain[0];
                } else if (inAtkWindup && pe->atkCount > 0) {
                    int ai = (int)((0.45f - e->cd) / DGAME_ATK_FRAME_TIME);
                    if (ai < 0) ai = 0;
                    if (ai >= pe->atkCount) ai = pe->atkCount - 1;
                    tex = pe->atk[ai];
                } else if (e->type == 10) {
                    // Revenant — walk_0..walk_7 are the 8 RSKE animation
                    // frames at FRONT rotation only (Beautiful-Doom doesn't
                    // give us a clean "static idle" view, so we cycle the
                    // walk anim instead). Always faces the camera. Cycle
                    // speed is tied to legT so the cadence scales with the
                    // enemy's base speed.
                    int af = (int)(e->legT * 0.45f) % pe->walkCount;
                    if (af < 0) af += pe->walkCount;
                    tex = pe->walk[af];
                } else {
                    // Pick rotation slot from enemy facing vs player angle.
                    Vector3 facing;
                    if (e->state == ES_PATROL) {
                        facing = e->pd;
                    } else {
                        float fdx = g_p.pos.x - e->pos.x, fdz = g_p.pos.z - e->pos.z;
                        float l = sqrtf(fdx*fdx + fdz*fdz);
                        facing = (l > 0.001f) ? (Vector3){fdx/l, 0, fdz/l} : (Vector3){0, 0, 1};
                    }
                    float enemyYaw  = atan2f(facing.x, facing.z);
                    float pdxv = g_p.pos.x - e->pos.x, pdzv = g_p.pos.z - e->pos.z;
                    float playerYaw = atan2f(pdxv, pdzv);
                    float rel = playerYaw - enemyYaw;
                    while (rel >  PI) rel -= 2.f*PI;
                    while (rel < -PI) rel += 2.f*PI;
                    float relDeg = rel * RAD2DEG;
                    float a = relDeg + 22.5f;
                    while (a < 0)    a += 360.f;
                    while (a >= 360) a -= 360.f;
                    int slot = (int)(a / 45.f) % 8;
                    if (slot >= pe->walkCount) slot %= pe->walkCount;
                    tex = pe->walk[slot];
                }
                // Per-frame world height tracks the actual pixel height of
                // the chosen frame, using walk[0] as the "px-per-metre"
                // reference. A short corpse sprite renders short.
                float spriteH = baseSpriteH;
                if (tex.id && pe->walk[0].id && pe->walk[0].height > 0) {
                    spriteH = baseSpriteH * (float)tex.height / (float)pe->walk[0].height;
                }
                // Flying enemies (caco / lost soul / pain elemental) collapse
                // to a flat pile once they hit the floor — their death sprite
                // is a top-down gore view, so rendering it as a tall vertical
                // billboard puts blood splatter in mid-air. Once landed,
                // squash spriteH so it hugs the ground.
                if (isCorpse && (e->type == 8 || e->type == 11 || e->type == 12)
                    && e->pos.y <= 0.05f) {
                    spriteH *= 0.60f;
                }
                Vector3 pos = {e->pos.x, e->pos.y + spriteH * 0.5f, e->pos.z};
                if (tex.id) {
                    float aspect = (float)tex.width / (float)tex.height;
                    Rectangle src = {0, 0, (float)tex.width, (float)tex.height};
                    Vector2 size   = {spriteH * aspect, spriteH};
                    Vector2 origin = {size.x * 0.5f, size.y * 0.5f};
                    DrawBillboardPro(cam, tex, src, pos, (Vector3){0,1,0}, size, origin, 0.f, WHITE);
                }
                if (!e->dying && e->hp < e->maxHp) {
                    float hp = e->hp / e->maxHp;
                    float bary = e->pos.y + spriteH + 0.30f;
                    Vector3 HF = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
                    Vector3 HR = Vector3Normalize(Vector3CrossProduct(HF, cam.up));
                    Vector3 HU = Vector3CrossProduct(HR, HF);
                    float W = (e->type == 9) ? 2.0f : 1.0f;
                    float H = 0.13f;
                    Vector3 center = {e->pos.x, bary, e->pos.z};
                    Vector3 bgTL = Vector3Add(center, Vector3Add(Vector3Scale(HR,-W*0.5f), Vector3Scale(HU, H*0.5f)));
                    Vector3 bgTR = Vector3Add(center, Vector3Add(Vector3Scale(HR, W*0.5f), Vector3Scale(HU, H*0.5f)));
                    Vector3 bgBL = Vector3Add(center, Vector3Add(Vector3Scale(HR,-W*0.5f), Vector3Scale(HU,-H*0.5f)));
                    Vector3 bgBR = Vector3Add(center, Vector3Add(Vector3Scale(HR, W*0.5f), Vector3Scale(HU,-H*0.5f)));
                    rlBegin(RL_TRIANGLES);
                    rlColor4ub(0,0,0,180);
                    rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBL.x,bgBL.y,bgBL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z);
                    rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBR.x,bgBR.y,bgBR.z); rlVertex3f(bgTR.x,bgTR.y,bgTR.z);
                    Vector3 fTR = Vector3Add(bgTL, Vector3Scale(HR, W*hp));
                    Vector3 fBR = Vector3Add(bgBL, Vector3Scale(HR, W*hp));
                    rlColor4ub(220,40,40,255);
                    rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(bgBL.x,bgBL.y,bgBL.z); rlVertex3f(fBR.x,fBR.y,fBR.z);
                    rlVertex3f(bgTL.x,bgTL.y,bgTL.z); rlVertex3f(fBR.x,fBR.y,fBR.z); rlVertex3f(fTR.x,fTR.y,fTR.z);
                    rlEnd();
                }
                continue;
            }
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
            Texture2D atkTex     = g_chefAtkTex;
            bool  walkOK = g_chefOK;
            bool  deathOK = g_chefDeathOK;
            bool  painOK = g_chefPainOK;
            bool  atkOK  = g_chefAtkOK;
            if (e->type == 1 && g_tormOK) {
                walkTex  = g_tormTex;
                deathTex = g_tormDeathTex;
                painTex  = g_tormPainTex;
                atkTex   = g_tormAtkTex;
                walkOK   = g_tormOK;
                deathOK  = g_tormDeathOK;
                painOK   = g_tormPainOK;
                atkOK    = g_tormAtkOK;
            } else if (e->type == 2 && g_schOK) {
                walkTex  = g_schTex;
                deathTex = g_schDeathTex;
                painTex  = g_schPainTex;
                atkTex   = g_schAtkTex;
                walkOK   = g_schOK;
                deathOK  = g_schDeathOK;
                painOK   = g_schPainOK;
                atkOK    = g_schAtkOK;
            }
            (void)walkOK;

            Texture2D tex;
            bool isCorpse = e->dying && deathOK;
            // Show the E-frame attack pose during the last ~0.45s of cooldown
            // before the melee tick lands. Read like a swing windup; the actual
            // damage still applies the moment cd<=0 (in UpdEnemies).
            bool inAtkWindup = (e->state == ES_ATTACK) && (e->cd > 0.f) && (e->cd < 0.45f);
            if (isCorpse) {
                int df = (int)(e->deathT / CHEF_DEATH_FRAME_TIME);
                if (df > 3) df = 3;
                tex = deathTex[df];
            } else if (e->flashT > 0.f && painOK) {
                tex = painTex;
            } else if (inAtkWindup && atkOK) {
                tex = atkTex;
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

// Destroy a ceiling lamp: shader light off, broken-flag on, fireball + glass
// shards. Idempotent — safe to call repeatedly (a rocket splash that catches
// an already-broken lamp does nothing).
static void DestroyLamp(int idx) {
    if (idx < 0 || idx >= NUM_LAMPS) return;
    if (g_lampBroken[idx]) return;
    g_lampBroken[idx] = true;
    g_lights[idx].enabled = 0;
    ShaderSetLight(idx);
    Vector3 p = g_lights[idx].pos;
    Explode(p);
    // Glass shards — small, tumbling, white-grey. Fall under gravity.
    for (int i = 0; i < 18; i++) {
        Vector3 v = {
            ((float)rand()/RAND_MAX - 0.5f) * 6.f,
            (float)rand()/RAND_MAX * 4.f + 1.f,
            ((float)rand()/RAND_MAX - 0.5f) * 6.f
        };
        unsigned char br = 200 + rand() % 55;
        SpawnPart(p, v, (Color){br, br, br, 255},
                  0.6f + (float)rand()/RAND_MAX * 0.5f,
                  0.04f + (float)rand()/RAND_MAX * 0.04f, true);
    }
}

// Detonate a flaming barrel: 5m splash to enemies and player, chains to
// nearby lamps and other barrels. Idempotent — second call on the same
// barrel does nothing, so the chain reaction terminates cleanly even if
// several barrels are within each other's blast radius. Recursion is
// bounded by NUM_BARRELS (each barrel deactivates before chaining).
static void DetonateBarrel(int idx) {
    if (idx < 0 || idx >= g_barrelCount) return;
    Barrel *b = &g_barrels[idx];
    if (!b->active) return;
    b->active = false;
    g_lights[BARREL_LIGHT_BASE + idx].enabled = 0;
    g_lights[BARREL_LIGHT_BASE + idx].radius  = 0.f;
    ShaderSetLight(BARREL_LIGHT_BASE + idx);
    Vector3 p = b->pos;
    Explode(p);
    // Splash damage to live enemies — falls off linearly to 0 at 5m.
    for (int j = 0; j < g_ec; j++) {
        if (!g_e[j].active || g_e[j].dying) continue;
        float d = Vector3Distance(p, g_e[j].pos);
        if (d < 5.f) DmgEnemy(j, 150.f * (1.f - d/5.f));
    }
    // Player splash — caps at 30 HP self-damage at point-blank, like a
    // rocket-on-wall. Walking up to a barrel and shooting it should hurt.
    float pd = Vector3Distance(p, g_p.pos);
    if (pd < 5.f) {
        if (!g_god) g_p.hp -= 30.f * (1.f - pd/5.f);
        g_p.hurtFlash = 0.3f;
        if (g_p.hp <= 0) { g_p.dead = true; g_gs = GS_DEAD; }
    }
    // Hot enough to break nearby ceiling lamps.
    for (int li = 0; li < NUM_LAMPS; li++) {
        if (!g_lampBroken[li] && Vector3Distance(p, g_lights[li].pos) < 5.f) {
            DestroyLamp(li);
        }
    }
    // Chain reaction — any other live barrel inside 4.5m goes up too.
    // Slightly tighter than the splash damage radius so chains don't
    // automatically cascade through the entire map every shot.
    for (int k = 0; k < g_barrelCount; k++) {
        if (k == idx) continue;
        if (!g_barrels[k].active) continue;
        if (Vector3Distance(p, g_barrels[k].pos) < 4.5f) DetonateBarrel(k);
    }
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
                    // b->dmg is the at-fire damage (200 baseline, 800 under quad).
                    if (d<5.f) DmgEnemy(j,b->dmg*(1.f-d/5.f));
                }
                // Any nearby lamp pops too — splash respects line of sight
                // by distance only (good enough for a 5m blast).
                for (int li=0; li<NUM_LAMPS; li++)
                    if (!g_lampBroken[li] && Vector3Distance(b->pos, g_lights[li].pos) < 5.f)
                        DestroyLamp(li);
                // Barrels in range chain-detonate (DetonateBarrel itself
                // recurses for further chains).
                for (int bk=0; bk<g_barrelCount; bk++)
                    if (g_barrels[bk].active && Vector3Distance(b->pos, g_barrels[bk].pos) < 5.f)
                        DetonateBarrel(bk);
                TriggerMultiKill();
                float pd=Vector3Distance(b->pos,g_p.pos);
                if (pd<5.f){if(!g_god)g_p.hp-=30.f*(1.f-pd/5.f);g_p.hurtFlash=0.3f;if(g_p.hp<=0){g_p.dead=true;g_gs=GS_DEAD;}}
            } else Sparks(prev,22);
            b->active=false; continue;
        }
        for (int j=0;j<g_ec;j++) {
            Enemy *e=&g_e[j]; if (!e->active || e->dying) continue;
            float _ex=b->pos.x-e->pos.x, _ez=b->pos.z-e->pos.z;
            float _xzdist=sqrtf(_ex*_ex+_ez*_ez);
            // Per-type hit volume. _bodyY is now an OFFSET from the enemy's
            // feet (e->pos.y), so flying enemies (caco, lost soul, pain
            // elemental) get a hit cylinder that tracks their actual y.
            // Without the offset model, a caco at e->pos.y=1.5 with bodyY=1.5
            // had its hit cylinder centred at world y=1.5 (its feet) instead
            // of y=2.6 (its body), and most rockets passed under the sprite.
            float _bodyY   = (e->type==3)  ? 2.0f
                           : (e->type==9)  ? 1.7f
                           : (e->type==8)  ? 1.1f   // caco — center of 2.2m sprite above feet
                           : (e->type==11) ? 0.5f   // lost soul — small head
                           : (e->type==12) ? 1.3f   // pain elemental — big floating ball
                           :                 1.0f;
            float _bodyR   = (e->type==3)  ? 1.5f
                           : (e->type==9)  ? 1.3f
                           : (e->type==8)  ? 1.0f
                           : (e->type==11) ? 0.45f
                           : (e->type==12) ? 1.10f
                           :                 0.9f;
            float _bodyH   = (e->type==3)  ? 2.3f
                           : (e->type==9)  ? 1.9f
                           : (e->type==8)  ? 1.1f
                           : (e->type==11) ? 0.55f
                           : (e->type==12) ? 1.30f
                           :                 1.2f;
            float _bodyWorldY = e->pos.y + _bodyY;
            float _ydist=fabsf(b->pos.y - _bodyWorldY);
            if ((_xzdist<_bodyR && _ydist<_bodyH) || Vector3Distance(b->pos,(Vector3){e->pos.x,_bodyWorldY,e->pos.z})<_bodyR) {
                if (b->rocket) {
                    Explode(b->pos);
                    g_killsThisShot = 0;
                    for (int k=0;k<g_ec;k++){if(!g_e[k].active||g_e[k].dying)continue;float d=Vector3Distance(b->pos,g_e[k].pos);if(d<5.f)DmgEnemy(k,b->dmg*(1.f-d/5.f));}
                    for (int li=0; li<NUM_LAMPS; li++)
                        if (!g_lampBroken[li] && Vector3Distance(b->pos, g_lights[li].pos) < 5.f)
                            DestroyLamp(li);
                    for (int bk=0; bk<g_barrelCount; bk++)
                        if (g_barrels[bk].active && Vector3Distance(b->pos, g_barrels[bk].pos) < 5.f)
                            DetonateBarrel(bk);
                    TriggerMultiKill();
                    float pd=Vector3Distance(b->pos,g_p.pos);
                    if (pd<5.f){if(!g_god)g_p.hp-=30.f*(1.f-pd/5.f);g_p.hurtFlash=0.3f;if(g_p.hp<=0){g_p.dead=true;g_gs=GS_DEAD;}}
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

// Multi-kill announcement — picks the right tier and plays it.
//   4+ kills → MONSTER KILL!! routed through the flashing hype banner so it
//              dominates the screen, plus the new monster-kill stinger.
//   2-3      → existing MULTI KILL / "Holy Shit!" stinger via the regular
//              Msg() slot.
static void TriggerMultiKill(void) {
    if (g_killsThisShot >= 4) {
        if (g_sMonsterOK) { SetSoundVolume(g_sMonster, 1.5f); PlaySound(g_sMonster); }
        strncpy(g_hypeMsg, "MONSTER KILL!!", 79); g_hypeMsg[79] = 0;
        g_hypeDur = 3.0f;
        g_hypeT   = g_hypeDur;
    } else if (g_killsThisShot >= 2 && g_sMultiOK) {
        SetSoundVolume(g_sMulti, 8.0f);
        PlaySound(g_sMulti);
        Msg("MULTI KILL!");
    }
}

// ── TESLA BOLTS ──────────────────────────────────────────────────────────────
static void SpawnBolt(const Vector3 *pts, int n) {
    if (n < 2 || n > BOLT_MAX_PTS) return;
    for (int i = 0; i < MAX_BOLTS; i++) {
        if (!g_bolts[i].active) {
            g_bolts[i].active = true;
            g_bolts[i].n      = n;
            g_bolts[i].life   = BOLT_LIFE;
            for (int k = 0; k < n; k++) g_bolts[i].pts[k] = pts[k];
            return;
        }
    }
}
static void UpdBolts(float dt) {
    for (int i = 0; i < MAX_BOLTS; i++) {
        if (!g_bolts[i].active) continue;
        g_bolts[i].life -= dt;
        if (g_bolts[i].life <= 0.f) g_bolts[i].active = false;
    }
}
// Each chain edge becomes 6 jagged sub-segments with random perpendicular
// jitter — re-rolled every frame so the bolt visibly crackles for its life.
static void DrawBolts(void) {
    // Fast-out: flushing the render batch every frame is expensive; skip
    // the whole pass when there's nothing to draw.
    int any = 0;
    for (int i = 0; i < MAX_BOLTS; i++) if (g_bolts[i].active) { any = 1; break; }
    if (!any) return;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    for (int i = 0; i < MAX_BOLTS; i++) {
        Bolt *b = &g_bolts[i];
        if (!b->active) continue;
        float a   = fminf(1.f, b->life / BOLT_LIFE);
        Color glow = (Color){180, 90, 255, (unsigned char)(220 * a)};
        Color core = (Color){255, 230, 255, (unsigned char)(255 * a)};
        for (int s = 0; s < b->n - 1; s++) {
            Vector3 p0 = b->pts[s], p1 = b->pts[s+1];
            int sub = 6;
            Vector3 prev = p0;
            for (int k = 1; k <= sub; k++) {
                float t = (float)k / sub;
                Vector3 mid;
                if (k == sub) {
                    mid = p1;
                } else {
                    float jx = ((float)rand()/RAND_MAX - 0.5f) * 0.30f;
                    float jy = ((float)rand()/RAND_MAX - 0.5f) * 0.30f;
                    float jz = ((float)rand()/RAND_MAX - 0.5f) * 0.30f;
                    mid = (Vector3){
                        p0.x + (p1.x-p0.x)*t + jx,
                        p0.y + (p1.y-p0.y)*t + jy,
                        p0.z + (p1.z-p0.z)*t + jz,
                    };
                }
                DrawLine3D(prev, mid, glow);
                DrawLine3D((Vector3){prev.x, prev.y+0.02f, prev.z},
                           (Vector3){mid.x,  mid.y +0.02f, mid.z}, core);
                DrawLine3D((Vector3){prev.x+0.02f, prev.y, prev.z},
                           (Vector3){mid.x +0.02f, mid.y,  mid.z}, glow);
                prev = mid;
            }
        }
    }
    rlEnableDepthMask();
    rlDrawRenderBatchActive();
}

// Tesla deferred-fire state: shot is locked at click, executed when the
// windup expires (or cancelled by weapon-switch / death).
static bool    g_teslaPending = false;
static Vector3 g_teslaPendOrigin;
static Vector3 g_teslaPendDir;
static float   g_teslaPendQuad = 1.f;
// When >0, the tesla zap sound is forcibly stopped once GetTime() reaches
// it. Set after firing the LAST cell so the buzz dies with the cooldown
// instead of trailing on past the empty viewmodel for ~1s of sample tail.
static float   g_teslaSfxStopT = 0.f;

// Forward decl — referenced in Shoot()'s tesla branch and from the per-frame
// timer block where the pending shot fires.
static void FireTeslaShot(Vector3 origin, Vector3 dir, float quadMul);

// ── ENEMY PROJECTILES ────────────────────────────────────────────────────────
// Mutant energy balls. SpawnEShot fires from origin toward target with a fixed
// speed; UpdEShots advances them, deals damage on player contact, and expires
// them on wall hit or after maxLife seconds.
static void SpawnEShot(Vector3 origin, Vector3 target, float dmg) {
    for (int i = 0; i < MAX_ESHOTS; i++) {
        if (g_es[i].active) continue;
        Vector3 d = Vector3Subtract(target, origin);
        float len = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z);
        if (len < 0.01f) return;
        Vector3 v = {d.x/len * 14.f, d.y/len * 14.f, d.z/len * 14.f};
        g_es[i].pos = origin;
        g_es[i].vel = v;
        g_es[i].life = 3.0f;
        g_es[i].dmg = dmg;
        g_es[i].isRocket = false;
        g_es[i].active = true;
        return;
    }
}
// Mech rocket: slower, splash damage on impact, orange smoke trail.
static void SpawnEShotRocket(Vector3 origin, Vector3 target, float dmg) {
    for (int i = 0; i < MAX_ESHOTS; i++) {
        if (g_es[i].active) continue;
        Vector3 d = Vector3Subtract(target, origin);
        float len = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z);
        if (len < 0.01f) return;
        Vector3 v = {d.x/len * 9.f, d.y/len * 9.f, d.z/len * 9.f};
        g_es[i].pos = origin;
        g_es[i].vel = v;
        g_es[i].life = 4.0f;
        g_es[i].dmg = dmg;
        g_es[i].isRocket = true;
        g_es[i].active = true;
        return;
    }
}
static void UpdEShots(float dt) {
    for (int i = 0; i < MAX_ESHOTS; i++) {
        EShot *s = &g_es[i];
        if (!s->active) continue;
        s->pos.x += s->vel.x * dt;
        s->pos.y += s->vel.y * dt;
        s->pos.z += s->vel.z * dt;
        s->life -= dt;
        // Trail particle: purple sparkles for energy ball, orange smoke for rocket.
        if (s->isRocket) {
            SpawnPart(s->pos,
                      (Vector3){((float)rand()/RAND_MAX - 0.5f)*0.4f,
                                ((float)rand()/RAND_MAX - 0.5f)*0.4f,
                                ((float)rand()/RAND_MAX - 0.5f)*0.4f},
                      (Color){255, 140, 50, 220}, 0.45f, 0.18f, false);
            SpawnPart(s->pos,
                      (Vector3){((float)rand()/RAND_MAX - 0.5f)*0.6f,
                                ((float)rand()/RAND_MAX - 0.5f)*0.6f,
                                ((float)rand()/RAND_MAX - 0.5f)*0.6f},
                      (Color){80, 80, 80, 200}, 0.7f, 0.25f, false);
        } else {
            SpawnPart(s->pos,
                      (Vector3){((float)rand()/RAND_MAX - 0.5f)*0.6f,
                                ((float)rand()/RAND_MAX - 0.5f)*0.6f,
                                ((float)rand()/RAND_MAX - 0.5f)*0.6f},
                      (Color){180, 80, 255, 220}, 0.35f, 0.10f, false);
        }
        bool wallHit = IsWall(s->pos.x, s->pos.z) ||
                       s->pos.y > WALL_H || s->pos.y < 0.f;
        bool dead = s->life <= 0.f || wallHit;
        // Player collision check
        float px = g_p.pos.x, pz = g_p.pos.z, py = g_p.pos.y + EYE_H - 0.4f;
        float dx = s->pos.x - px, dy = s->pos.y - py, dz = s->pos.z - pz;
        bool playerHit = (dx*dx + dy*dy + dz*dz < 0.65f*0.65f);
        // Enemy collision (infighting). The shot spawns inside the shooter's
        // own body sphere, so we skip enemy-collision for the first 0.15s
        // after spawn — by then the projectile (14 m/s ball or 9 m/s rocket)
        // has cleared any reasonable enemy body radius.
        const float maxLife    = s->isRocket ? 4.0f : 3.0f;
        const float spawnGrace = 0.15f;
        bool postGrace = (maxLife - s->life) > spawnGrace;
        int enemyHitIdx = -1;
        if (postGrace && !playerHit && !dead) {
            for (int j = 0; j < g_ec; j++) {
                Enemy *e = &g_e[j];
                if (!e->active || e->dying) continue;
                float ex = s->pos.x - e->pos.x;
                float ez = s->pos.z - e->pos.z;
                float byOff = (e->type == 3) ? 1.4f : 1.0f;
                float br    = (e->type == 3) ? 1.5f : (e->type == 6) ? 0.85f : 0.65f;
                float ey    = s->pos.y - (e->pos.y + byOff);
                if (ex*ex + ey*ey + ez*ez < br*br) {
                    enemyHitIdx = j;
                    break;
                }
            }
        }
        bool inflictHit = playerHit || enemyHitIdx >= 0;
        if (dead || inflictHit) {
            if (s->isRocket) {
                // Rocket: explosion + AOE damage out to 3.5m. Hits player AND
                // any enemies in the blast radius (enemy infighting).
                Explode(s->pos);
                if (playerHit || enemyHitIdx >= 0 || dead) {
                    float pdist = sqrtf(dx*dx + dy*dy + dz*dz);
                    if (pdist < 3.5f) {
                        float fall = 1.f - pdist / 3.5f;
                        if (!g_god) g_p.hp -= s->dmg * fall;
                        g_p.hurtFlash = 0.3f;
                        g_p.shake = fmaxf(g_p.shake, 0.18f);
                        if (g_sMechHitOK) PlaySound(g_sMechHit);
                        else              PlaySound(g_sHurt);
                        if (g_p.hp <= 0) { g_p.dead = true; g_gs = GS_DEAD; }
                    }
                    // Splash damage to other enemies — gives infighting some
                    // teeth. Each enemy in range takes falloff damage via the
                    // same DmgEnemy path the player's rocket uses.
                    for (int j = 0; j < g_ec; j++) {
                        Enemy *e = &g_e[j];
                        if (!e->active || e->dying) continue;
                        float exd = s->pos.x - e->pos.x;
                        float eyd = s->pos.y - (e->pos.y + 1.0f);
                        float ezd = s->pos.z - e->pos.z;
                        float ed  = sqrtf(exd*exd + eyd*eyd + ezd*ezd);
                        if (ed < 3.5f) DmgEnemy(j, s->dmg * (1.f - ed/3.5f));
                    }
                }
            } else if (playerHit) {
                if (!g_god) g_p.hp -= s->dmg;
                g_p.hurtFlash = 0.22f;
                g_p.shake = fmaxf(g_p.shake, 0.13f);
                PlaySound(g_sHurt);
                if (g_p.hp <= 0) { g_p.dead = true; g_gs = GS_DEAD; }
            } else if (enemyHitIdx >= 0) {
                // Mutant energy ball: single-target enemy hit. Spawn a small
                // blood burst for visual feedback; damage applied via DmgEnemy.
                Blood(s->pos, 6);
                DmgEnemy(enemyHitIdx, s->dmg);
            }
            s->active = false;
        }
    }
}
static void DrawEShots(void) {
    int any = 0;
    for (int i = 0; i < MAX_ESHOTS; i++) if (g_es[i].active) { any = 1; break; }
    if (!any) return;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < MAX_ESHOTS; i++) {
        if (!g_es[i].active) continue;
        if (g_es[i].isRocket) {
            // Rocket: bright orange core with a hot-yellow inner spark
            DrawSphere(g_es[i].pos, 0.35f, (Color){255, 200,  60, 255});
            DrawSphere(g_es[i].pos, 0.55f, (Color){255, 130,  40, 180});
            DrawSphere(g_es[i].pos, 0.85f, (Color){200,  80,  20,  80});
        } else {
            DrawSphere(g_es[i].pos, 0.32f, (Color){220, 130, 255, 240});
            DrawSphere(g_es[i].pos, 0.55f, (Color){150,  60, 220, 120});
            DrawSphere(g_es[i].pos, 0.85f, (Color){100,  40, 180,  50});
        }
    }
    EndBlendMode();
    rlEnableDepthMask();
    rlDrawRenderBatchActive();
}

// ── PLAYER SHOOT ─────────────────────────────────────────────────────────────
static void Shoot(void) {
    if (g_needMouseRelease) return;  // swallow held click from menu/death screen
    if (g_p.shootCD>0) return;
    int w=g_p.weapon;
    if (w==0&&g_p.shells<=0){PlaySound(g_sEmpty);return;}
    if (w==1&&g_p.mgAmmo<=0){PlaySound(g_sEmpty);return;}
    if (w==2&&g_p.rockets<=0){PlaySound(g_sEmpty);return;}
    if (w==3&&g_p.cells<=0){PlaySound(g_sEmpty);return;}
    // High-score stat: count one shot per actual fire (shotgun pellets and
    // tesla chain bolts are still one user-perceived "shot").
    g_statShots++;
    // Shotgun sound is deferred until after the pellet loop so we can make it
    // LOUDER when the shot didn't actually kill an enemy (no kill stinger plays).
    if (w==0){g_p.shells--;}
    else if (w==1){PlaySound(g_sMGOK ? g_sMG : g_sPistol); g_p.mgAmmo--;}
    else if (w==2){PlaySound(g_sRocket);g_p.rockets--;}
    else {g_p.cells--;}  // tesla: sound + muzzle light deferred to FireTeslaShot
    g_p.shootCD=WR[w]; g_p.kickAnim=0.18f; g_p.shake=fmaxf(g_p.shake,0.07f);
    // muzzle flash light
    float cy=cosf(g_p.pitch), sy=sinf(g_p.pitch);
    float syw=sinf(g_p.yaw+3.14159f), cyw=cosf(g_p.yaw+3.14159f);
    Vector3 fwd={syw*cy, sy, cyw*cy};
    Vector3 eyePos={g_p.pos.x,g_p.pos.y+EYE_H,g_p.pos.z};
    if (w != 3) {
        g_lights[MUZZLE_LIGHT]=(LightDef){Vector3Add(eyePos,Vector3Scale(fwd,1.5f)),{1.f,0.85f,0.4f},8.f,1};
        ShaderSetLight(MUZZLE_LIGHT);
    }
    float quadMul = (g_p.quadT > 0.f) ? 4.f : 1.f;
    if (w==2){FireBullet(eyePos,fwd,200.f*quadMul,true);return;}
    if (w==3){
        // Lock origin/dir at click; the actual hitscan fires when the windup
        // expires (FireTeslaShot, called from the per-frame timer block).
        g_teslaPending    = true;
        g_teslaPendOrigin = eyePos;
        g_teslaPendDir    = fwd;
        g_teslaPendQuad   = quadMul;
        return;
    }
    g_killsThisShot = 0;          // reset per-shot counter for multi-kill detection
    int pels=WPEL[w]; float sprd=(w==1)?0.10f:0.012f;
    for (int p=0;p<pels;p++) {
        Vector3 rd=fwd;
        if (pels>1){rd.x+=((float)rand()/RAND_MAX-.5f)*sprd*2;rd.y+=((float)rand()/RAND_MAX-.5f)*sprd;rd.z+=((float)rand()/RAND_MAX-.5f)*sprd*2;rd=Vector3Normalize(rd);}
        float best=1e9f; int bi=-1; bool headshot=false;
        // Closest-hit-wins ray test against lamps, barrels, and enemies.
        // Whenever a closer surface is found we update `best` AND clear
        // the previous winners' indices, so exactly one of {bi, lampHit,
        // barrelHit} is set after all three passes.
        int lampHit = -1, barrelHit = -1;
        for (int li=0; li<NUM_LAMPS; li++) {
            if (g_lampBroken[li]) continue;
            float lt = RSphere(eyePos, rd, g_lights[li].pos, 0.55f);
            if (lt > 0 && lt < best) { best = lt; lampHit = li; barrelHit = -1; }
        }
        for (int bk=0; bk<g_barrelCount; bk++) {
            if (!g_barrels[bk].active) continue;
            // Barrel hit-sphere centred slightly above the floor on the can,
            // 0.55m radius so it reads as a chunky target.
            Vector3 bcen = {g_barrels[bk].pos.x, g_barrels[bk].pos.y + 0.6f, g_barrels[bk].pos.z};
            float bt = RSphere(eyePos, rd, bcen, 0.55f);
            if (bt > 0 && bt < best) { best = bt; barrelHit = bk; lampHit = -1; }
        }
        for (int j=0;j<g_ec;j++){
            if (!g_e[j].active || g_e[j].dying) continue;
            // Per-type head position + hitbox sizes
            //   chef variants: head ~1.57m, body ~0.85m
            //   boss:          head 2.5m,   body 1.4m   (much bigger)
            //   mutant:        head 2.0m,   body 1.1m   (taller WolfenDoom sprite)
            float headY, headR, bodyY, bodyR;
            if (g_e[j].type == 3) {          // BOSS
                headY = 2.5f; headR = 0.45f;
                bodyY = 1.4f; bodyR = 0.95f;
            } else if (g_e[j].type == 5) {   // MUTANT (sprite ~2.26m tall)
                headY = 2.00f; headR = 0.30f;
                bodyY = 1.10f; bodyR = 0.55f;
            } else if (g_e[j].type == 6) {   // MECH (sprite ~2.6m tall, bulky)
                headY = 2.40f; headR = 0.40f;
                bodyY = 1.30f; bodyR = 0.85f;
            } else if (g_e[j].type == 9) {   // CYBER DEMON (sprite ~3.4m tall)
                headY = 2.90f; headR = 0.50f;
                bodyY = 1.70f; bodyR = 1.20f;
            } else if (g_e[j].type == 8) {   // CACODEMON (flying — feet at e->pos.y=1.5)
                headY = 1.50f; headR = 0.45f;
                bodyY = 1.10f; bodyR = 1.00f;
            } else if (g_e[j].type == 11) {  // LOST SOUL (small flying head, feet at e->pos.y=2.2)
                headY = 0.70f; headR = 0.30f;
                bodyY = 0.40f; bodyR = 0.45f;
            } else if (g_e[j].type == 12) {  // PAIN ELEMENTAL (big floater, feet at e->pos.y=2.0)
                headY = 1.80f; headR = 0.50f;
                bodyY = 1.30f; bodyR = 1.05f;
            } else {
                float bh=(g_e[j].type==1)?1.1f:(g_e[j].type==2)?0.72f:0.9f;
                headY = bh + 0.67f; headR = 0.26f;
                bodyY = 0.85f;      bodyR = 0.68f;
            }
            // headY/bodyY are offsets above the enemy's FEET, so they track
            // the sprite when it rides up onto a platform (e->pos.y > 0).
            Vector3 hc={g_e[j].pos.x, g_e[j].pos.y + headY, g_e[j].pos.z};
            float ht=RSphere(eyePos,rd,hc,headR);
            if (ht>0&&ht<best){best=ht;bi=j;headshot=true; lampHit=-1; barrelHit=-1;}
            Vector3 bc={g_e[j].pos.x, g_e[j].pos.y + bodyY, g_e[j].pos.z};
            float bt=RSphere(eyePos,rd,bc,bodyR);
            if (bt>0&&bt<best){best=bt;bi=j;headshot=false; lampHit=-1; barrelHit=-1;}
        }
        if (bi>=0){
            Vector3 hp=Vector3Add(eyePos,Vector3Scale(rd,best));
            // Boss bleeds 4x as much as a chef per pellet — it's a big target
            int bloodCount = headshot ? 25 : 14;
            if (g_e[bi].type == 3) bloodCount *= 4;
            Blood(hp, bloodCount);
            SpawnSplat(hp);
            float dmg=(float)WD[w]*(headshot?2.5f:1.0f)*quadMul;
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
        else if (barrelHit >= 0) {
            // Pellet hit a barrel — full detonation with chain reaction.
            DetonateBarrel(barrelHit);
        }
        else if (lampHit >= 0) {
            // Lamp is the closest thing the pellet hit. One pellet/bullet
            // is enough — lamps are one-shot for satisfying punch.
            DestroyLamp(lampHit);
        }
        else{for(float t=0.5f;t<50.f;t+=0.5f){Vector3 pt=Vector3Add(eyePos,Vector3Scale(rd,t));if(IsWall(pt.x,pt.z)){Sparks(pt,18);break;}}}
    }
    // Shotgun blast SFX — always use the shotgun-kill stinger for every shot
    if (g_p.weapon == 0 && g_sShotgunKillOK) {
        SetSoundVolume(g_sShotgunKill, 1.0f);
        PlaySound(g_sShotgunKill);
    }
    // Multi-kill announcement (2+ enemies dropped by this shot)
    TriggerMultiKill();
}

// Cheap 2D wall LOS used by tesla to stop lightning arcing through geometry.
// Steps at 0.3m so a corner-cut on a CELL=4 grid can't tunnel.
static bool TeslaLOS(Vector3 a, Vector3 b) {
    float dx = b.x - a.x, dz = b.z - a.z;
    float dist = sqrtf(dx*dx + dz*dz);
    if (dist < 0.01f) return true;
    int steps = (int)(dist / 0.3f) + 1;
    for (int s = 1; s < steps; s++) {
        float t = (float)s / steps;
        if (IsWall(a.x + dx*t, a.z + dz*t)) return false;
    }
    return true;
}

// Tesla discharge: wide forward-cone auto-target (so the player doesn't have
// to be pin-precise), then chain to nearest live enemies within 15m. Falloff
// 85/70/58/48/40% across 5 hops (6 victims max). Called from the per-frame
// timer block once the windup expires.
static void FireTeslaShot(Vector3 origin, Vector3 dir, float quadMul) {
    // Purple muzzle flash light at the gun tip
    Vector3 lightAt = Vector3Add(origin, Vector3Scale(dir, 1.5f));
    g_lights[MUZZLE_LIGHT] = (LightDef){lightAt, {0.6f, 0.35f, 1.f}, 9.f, 1};
    ShaderSetLight(MUZZLE_LIGHT);
    // Cut any still-playing buzz tail from the previous shot so rapid fire
    // doesn't pile copies of the sample on top of each other.
    if (g_sTeslaOK) {
        // Only kick the sample if it's not already playing — held-down fire
        // keeps the buzz running continuously instead of clicking/restarting
        // every shot. The scheduled stop below kills it when the player
        // actually releases (deferred while still holding).
        if (!IsSoundPlaying(g_sTesla)) PlaySound(g_sTesla);
        g_teslaSfxStopT = GetTime() + (WR[3] - TESLA_WINDUP);
    } else if (g_sMGOK) PlaySound(g_sMG);

    g_killsThisShot = 0;
    Vector3 muzzle = {origin.x + dir.x*0.6f, origin.y - 0.15f + dir.y*0.6f, origin.z + dir.z*0.6f};
    Vector3 pts[BOLT_MAX_PTS]; int np = 0;
    pts[np++] = muzzle;

    // Wide-cone auto-target: pick the closest live enemy OR explosive barrel
    // within ~50° of aim and 30m, with wall LOS. No headshot mechanic —
    // tesla rewards positioning (chain count) rather than precision.
    const float TESLA_RANGE = 6.f;     // doubled from 3m — broader depth reach
    const float TESLA_COS   = 0.34f;   // cos(70°) — much wider cone (was cos(50°))
    float bestD2 = TESLA_RANGE * TESLA_RANGE;
    int bi = -1, bbk = -1;
    for (int j = 0; j < g_ec; j++) {
        if (!g_e[j].active || g_e[j].dying) continue;
        float dx = g_e[j].pos.x - origin.x;
        float coneChestY = (g_e[j].type == 9) ? 1.7f
                         : (g_e[j].type == 8) ? 1.1f
                         : (g_e[j].type == 3) ? 1.4f
                         :                      0.95f;
        float dy = (g_e[j].pos.y + coneChestY) - origin.y;
        float dz = g_e[j].pos.z - origin.z;
        float d2 = dx*dx + dy*dy + dz*dz;
        // Distance is measured to the enemy SURFACE (subtract body radius)
        // not the centre — without this a 1.5 m boss had to be touched to
        // be within tesla's 6 m range. Big enemies (boss, cyber, mech)
        // can now be tagged from further away.
        float bodyR = (g_e[j].type == 3) ? 1.5f
                    : (g_e[j].type == 9) ? 1.3f
                    : (g_e[j].type == 6) ? 0.85f
                    :                      0.55f;
        float d = sqrtf(d2);
        float surf = d - bodyR;
        if (surf < 0.f) surf = 0.f;
        if (surf * surf >= bestD2) continue;
        if (d > 0.01f) {
            float dotAim = (dx*dir.x + dy*dir.y + dz*dir.z) / d;
            if (dotAim < TESLA_COS) continue;
        }
        Vector3 ec = {g_e[j].pos.x, g_e[j].pos.y + coneChestY, g_e[j].pos.z};
        if (!TeslaLOS(origin, ec)) continue;
        bestD2 = surf * surf; bi = j; bbk = -1;
    }
    for (int k = 0; k < g_barrelCount; k++) {
        if (!g_barrels[k].active) continue;
        float dx = g_barrels[k].pos.x - origin.x;
        float dy = (g_barrels[k].pos.y + 0.6f) - origin.y;
        float dz = g_barrels[k].pos.z - origin.z;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 >= bestD2) continue;
        float d = sqrtf(d2);
        if (d > 0.01f) {
            float dotAim = (dx*dir.x + dy*dir.y + dz*dir.z) / d;
            if (dotAim < TESLA_COS) continue;
        }
        if (!TeslaLOS(origin, g_barrels[k].pos)) continue;
        bestD2 = d2; bbk = k; bi = -1;
    }
    if (bi >= 0) {
        int chained[BOLT_MAX_PTS]; int chainN = 0;
        chained[chainN++] = bi;
        float biChest = (g_e[bi].type == 9) ? 1.7f
                      : (g_e[bi].type == 8) ? 1.1f
                      : (g_e[bi].type == 3) ? 1.4f
                      :                       0.95f;
        Vector3 anchor = {g_e[bi].pos.x, g_e[bi].pos.y + biChest, g_e[bi].pos.z};
        pts[np++] = anchor;
        Blood(anchor, 12);
        DmgEnemy(bi, (float)WD[3] * quadMul);
        for (int hop = 0; hop < 5 && np < BOLT_MAX_PTS; hop++) {
            float bestD = 6.f; int target = -1;
            for (int j = 0; j < g_ec; j++) {
                if (!g_e[j].active || g_e[j].dying) continue;
                bool already = false;
                for (int k = 0; k < chainN; k++) if (chained[k] == j) { already = true; break; }
                if (already) continue;
                float jChest = (g_e[j].type == 9) ? 1.7f
                             : (g_e[j].type == 8) ? 1.1f
                             : (g_e[j].type == 3) ? 1.4f
                             :                      0.95f;
                Vector3 ec = {g_e[j].pos.x, g_e[j].pos.y + jChest, g_e[j].pos.z};
                // Same surface-distance fix as the cone target above — boss
                // is wide enough that centre distance falsely excluded him
                // from chains.
                float jBodyR = (g_e[j].type == 3) ? 1.5f
                             : (g_e[j].type == 9) ? 1.3f
                             : (g_e[j].type == 6) ? 0.85f
                             :                      0.55f;
                float dRaw = Vector3Distance(anchor, ec);
                float d = dRaw - jBodyR;
                if (d < 0.f) d = 0.f;
                if (d >= bestD) continue;
                if (!TeslaLOS(anchor, ec)) continue;
                bestD = d; target = j;
            }
            if (target < 0) break;
            float tChest = (g_e[target].type == 9) ? 1.7f
                         : (g_e[target].type == 8) ? 1.1f
                         : (g_e[target].type == 3) ? 1.4f
                         :                           0.95f;
            anchor = (Vector3){g_e[target].pos.x, g_e[target].pos.y + tChest, g_e[target].pos.z};
            pts[np++] = anchor;
            chained[chainN++] = target;
            static const float fall[5] = {0.85f, 0.70f, 0.58f, 0.48f, 0.40f};
            DmgEnemy(target, (float)WD[3] * fall[hop] * quadMul);
            Blood(anchor, 6);
        }
        // Barrel cascade: any explosive within 5m of any chain victim
        // detonates. DetonateBarrel chain-blasts neighbours itself.
        for (int p = 1; p < np; p++) {
            for (int k = 0; k < g_barrelCount; k++) {
                if (!g_barrels[k].active) continue;
                if (Vector3Distance(pts[p], g_barrels[k].pos) < 5.f) DetonateBarrel(k);
            }
        }
        TriggerMultiKill();
    } else if (bbk >= 0) {
        // Primary target was an explosive barrel — bolt to it and detonate.
        Vector3 anchor = {g_barrels[bbk].pos.x, g_barrels[bbk].pos.y + 0.6f, g_barrels[bbk].pos.z};
        pts[np++] = anchor;
        DetonateBarrel(bbk);
    } else {
        for (float t=0.5f; t<50.f; t+=0.5f) {
            Vector3 pt = Vector3Add(origin, Vector3Scale(dir, t));
            if (IsWall(pt.x, pt.z)) { Sparks(pt, 12); pts[np++] = pt; break; }
        }
        if (np < 2) pts[np++] = Vector3Add(origin, Vector3Scale(dir, 30.f));
    }
    SpawnBolt(pts, np);
}

// ── WEAPON VIEWMODEL ─────────────────────────────────────────────────────────
// 2D screen-space weapon — always correct, no 3D rotation issues
// Draw an oriented box with world-space vertex positions computed from camera axes.
// bx/by/bz: offset from center along right/up/fwd.  hw/hh/hl: half-extents.
// Draw the current weapon's sprite over the 3D scene (2D overlay, post-EndMode3D).
// Frame[0] = idle.  Frame[1..count-1] = fire animation, played back during shootCD.
static void DrawSpriteWeapon(void) {
    int w = g_p.weapon;
    if (w < 0 || w >= 4) return;
    WepSprite *ws = &g_wep[w];
    if (!ws->loaded) return;

    // Pick frame index
    int frame = 0;
    if (g_p.shootCD > 0.f && ws->count > 1) {
        if (w == 3 && ws->count >= 7) {
            // Tesla two-phase animation:
            //   - first TESLA_WINDUP seconds: charging frames B/C/D (1..3)
            //   - then through discharge frames E/F/G (4..6) until cooldown ends
            if (g_p.shootCD > WR[3] - TESLA_WINDUP) {
                float prog = (WR[3] - g_p.shootCD) / TESLA_WINDUP;   // 0→1 across windup
                if (prog < 0.f) prog = 0.f; if (prog > 1.f) prog = 1.f;
                frame = 1 + (int)(prog * 3);
                if (frame > 3) frame = 3;
            } else {
                float discharge = WR[3] - TESLA_WINDUP;
                float prog = 1.f - (g_p.shootCD / discharge);        // 0→1 across discharge
                if (prog < 0.f) prog = 0.f; if (prog > 1.f) prog = 1.f;
                frame = 4 + (int)(prog * 3);
                if (frame > 6) frame = 6;
            }
        } else {
            float prog = 1.f - (g_p.shootCD / WR[w]);    // 0→1 across the fire window
            if (prog < 0.f) prog = 0.f; if (prog > 1.f) prog = 1.f;
            int fireFrames = ws->count - 1;
            frame = 1 + (int)(prog * fireFrames);
            if (frame >= ws->count) frame = ws->count - 1;
        }
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
    // Drop spurious one-frame jumps. After window resize, focus change, or
    // the cursor warp landing slightly off, mp can read tens or hundreds of
    // pixels off-centre on a single frame and snap the view. No human moves
    // a mouse 200 px in one 16 ms frame, so treat that as a glitch and skip
    // the delta entirely (one black-hole frame is invisible; a 200 px jerk
    // is not).
    if (fabsf(mdx) > 200.f || fabsf(mdy) > 200.f) { mdx = 0; mdy = 0; }
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
    float hasteMul = (g_p.hasteT > 0.f) ? 1.6f : 1.f;
    float spd=SPEED*(sprint?1.65f:1.f)*hasteMul*dt;
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

    // Push player out of flaming barrels — same circle-vs-circle resolve as
    // enemy collision. Barrel radius is tuned to roughly match the visible
    // can footprint on the floor.
    const float BARREL_RADIUS = 0.40f;
    for (int i = 0; i < g_barrelCount; i++) {
        Barrel *b = &g_barrels[i];
        if (!b->active) continue;
        float bdx = g_p.pos.x - b->pos.x;
        float bdz = g_p.pos.z - b->pos.z;
        float d2  = bdx*bdx + bdz*bdz;
        float minD = PRAD + BARREL_RADIUS;
        if (d2 > 0.0001f && d2 < minD*minD) {
            float d = sqrtf(d2);
            float push = minD - d;
            g_p.pos.x += (bdx/d) * push;
            g_p.pos.z += (bdz/d) * push;
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
    if (IsKeyPressed(KEY_FOUR) &&g_p.hasTesla&&g_p.weapon!=3){g_p.weapon=3;g_p.switchAnim=0.3f;}
    // Tesla pending shot is bound to weapon 3 — switching cancels the bolt
    // AND kills any leftover buzz so the sample doesn't trail across weapons.
    if (g_p.weapon != 3) {
        g_teslaPending = false;
        if (g_sTeslaOK) StopSound(g_sTesla);
        g_teslaSfxStopT = 0.f;
    }

    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_needMouseRelease = false;
    else if (!g_needMouseRelease) Shoot();

    // timers
    if (g_p.shootCD>0)   g_p.shootCD-=dt;
    // Tesla deferred fire — bolt releases when the windup phase ends.
    if (g_teslaPending && g_p.weapon == 3 && g_p.shootCD <= WR[3] - TESLA_WINDUP) {
        FireTeslaShot(g_teslaPendOrigin, g_teslaPendDir, g_teslaPendQuad);
        g_teslaPending = false;
    }
    // Scheduled buzz cutoff: at end of discharge, stop the sample UNLESS the
    // user is still holding fire and we have ammo for another shot — in that
    // case keep the buzz alive across the windup of the next shot for a
    // continuous hold-fire feel. (FireTeslaShot resets stopT each shot.)
    if (g_teslaSfxStopT > 0.f && GetTime() >= g_teslaSfxStopT) {
        bool willFireAgain = IsMouseButtonDown(MOUSE_BUTTON_LEFT)
                          && g_p.cells > 0
                          && g_p.weapon == 3
                          && !g_needMouseRelease;
        if (!willFireAgain) {
            if (g_sTeslaOK) StopSound(g_sTesla);
            g_teslaSfxStopT = 0.f;
        }
    }
    if (g_p.kickAnim>0)  g_p.kickAnim=fmaxf(0,g_p.kickAnim-dt*6.f);
    if (g_p.hurtFlash>0) g_p.hurtFlash-=dt;
    if (g_p.switchAnim>0)g_p.switchAnim=fmaxf(0,g_p.switchAnim-dt*5.f);
    if (g_p.shake>0)     g_p.shake=fmaxf(0,g_p.shake-dt*4.f);
    if (g_msgT>0)        g_msgT-=dt;
    if (g_hypeT>0)       g_hypeT-=dt;
    if (g_p.quadT>0)     g_p.quadT  = fmaxf(0.f, g_p.quadT  - dt);
    if (g_p.hasteT>0)    g_p.hasteT = fmaxf(0.f, g_p.hasteT - dt);
    if (g_p.quadT  <= 0.f) g_p.quadPeak  = 0.f;  // reset so next pickup starts full
    if (g_p.hasteT <= 0.f) g_p.hastePeak = 0.f;
    bool moving=(mlen>0)&&g_p.onGround;
    if (moving) g_p.bobT+=dt*(sprint?10.f:7.f);

    // fade muzzle flash light
    if (g_lights[MUZZLE_LIGHT].enabled) {
        g_lights[MUZZLE_LIGHT].radius-=dt*40.f;
        if (g_lights[MUZZLE_LIGHT].radius<=0){g_lights[MUZZLE_LIGHT].enabled=0;g_lights[MUZZLE_LIGHT].radius=0;}
        ShaderSetLight(MUZZLE_LIGHT);
    }
    if (g_lights[PULSE_LIGHT].enabled) {
        g_lights[PULSE_LIGHT].radius-=dt*30.f;
        if (g_lights[PULSE_LIGHT].radius<=0){g_lights[PULSE_LIGHT].enabled=0;g_lights[PULSE_LIGHT].radius=0;}
        ShaderSetLight(PULSE_LIGHT);
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
    for (int i=0;i<NUM_LAMPS;i++) {
        Vector3 fc={g_lights[i].color.x*flicker,g_lights[i].color.y*flicker,g_lights[i].color.z*flicker};
        SetShaderValue(g_shader,u_lColor[i],&fc,SHADER_UNIFORM_VEC3);
    }
    // Barrel point-lights — much louder flicker than the ceiling lamps so
    // the fire feels alive. Per-barrel phase decorrelates them so they
    // don't all pulse on the same beat.
    float ft = (float)GetTime();
    for (int i=0; i<g_barrelCount; i++) {
        Barrel *b = &g_barrels[i];
        if (!b->active) continue;
        float ph = b->phase;
        // Multi-octave flicker: fast crackle + slow breathing. Range ~0.7..1.15.
        float fast = sinf(ft*22.f + ph)*0.10f + sinf(ft*53.f + ph*1.7f)*0.07f;
        float slow = sinf(ft*3.1f  + ph)*0.08f;
        float fl   = 0.92f + fast + slow;
        if (fl < 0.55f) fl = 0.55f;
        // Light just above the rim of the barrel so the flame casts down
        // onto the floor as well as outward.
        Vector3 lpos = {b->pos.x, 1.4f, b->pos.z};
        // Warm orange — slightly varying yellow-channel adds licks of colour.
        float yellowKick = 0.05f * sinf(ft*9.f + ph*2.3f);
        Vector3 col = {1.0f * fl, (0.55f + yellowKick) * fl, 0.18f * fl};
        g_lights[BARREL_LIGHT_BASE + i].pos     = lpos;
        g_lights[BARREL_LIGHT_BASE + i].color   = col;
        g_lights[BARREL_LIGHT_BASE + i].radius  = 11.f * (0.85f + 0.15f*sinf(ft*4.f + ph));
        g_lights[BARREL_LIGHT_BASE + i].enabled = 1;
        ShaderSetLight(BARREL_LIGHT_BASE + i);
    }
}

// ── INIT ─────────────────────────────────────────────────────────────────────
static void InitGame(void) {
    srand((unsigned)time(NULL));
    // Reset high-score submission stats for the new run.
    g_statShots = 0;
    g_statDamage = 0.f;
    g_statPickups = 0;
    g_statTime = 0.f;
    g_initialsPos = 0;
    g_initialsDone = false;
    g_initialsSubmitted = false;
    g_lastRank = 0;
    g_lowHealthFired = false;  // re-arm critical-health stinger for new run
    g_firstKillThisWave = false;
    g_quietTime = 0.f;
    g_quietSighFired = false;
    // Cheat flags reset per run — a fresh game can earn a leaderboard entry
    // again. g_god turns OFF too so previously-toggled invulnerability
    // doesn't leak across restarts.
    g_cheated = false;
    g_god = false;
    // Preserve initials across runs so a recurring player doesn't have to
    // retype "AAA" every time. Defaults to "AAA" on first run.
    // Preserve facing direction across runs — without this, every restart
    // snaps yaw back to 0 (which faces the corner wall from the spawn point)
    // regardless of where the player was looking. First-run default: face
    // the map centre so the spawn isn't aimed into the corner wall.
    float prevYaw = g_p.yaw;
    bool  hadYaw  = (prevYaw != 0.f);
    memset(&g_p,0,sizeof(g_p));
    g_p.pos=(Vector3){1.5f*CELL,0,1.5f*CELL};
    if (hadYaw) {
        g_p.yaw = prevYaw;
    } else {
        // Forward direction in this engine is (sin(yaw+π), cos(yaw+π)),
        // so to face the map centre at (60, 40) from spawn (6, 6) we need
        // atan2(-(centerX - px), -(centerZ - pz)) — naive atan2 of the
        // centre delta points the player AWAY from the action.
        g_p.yaw = atan2f(g_p.pos.x - 60.f, g_p.pos.z - 40.f);
    }
    g_p.hp=g_p.maxHp=100; g_p.shells=32; g_p.rockets=8; g_p.mgAmmo=120; g_p.cells=0; g_p.weapon=0;
    g_p.hasTesla=false;
    g_wave=1; g_ec=0; g_pkc=0;
    memset(g_e,0,sizeof(g_e)); memset(g_b,0,sizeof(g_b));
    ResetParts(); memset(g_pk,0,sizeof(g_pk));
    memset(g_bolts,0,sizeof(g_bolts));
    memset(g_es,0,sizeof(g_es));
    memset(g_splats,0,sizeof(g_splats));
    g_teslaPending = false; g_teslaSfxStopT = 0.f;
    // Restore ceiling lights — anything shot off in the previous run comes
    // back lit. Re-pushes each light's enabled state to the GPU.
    for (int i = 0; i < NUM_LAMPS; i++) {
        g_lampBroken[i] = false;
        g_lights[i].enabled = 1;
        ShaderSetLight(i);
    }
    SeedPicks();
    SpawnBarrels();

    if (g_arenaMode) {
        // ARENA TEST: 8 enemies of g_arenaType. Boss arena uses fewer
        // (just 1) since they're tanky. KillEnemy respawns more.
        int t = g_arenaType;
        if (t < 0) t = 0; if (t > 12) t = 12;
        int count = (t == 3 || t == 9) ? 1 : 8;  // boss + cyber demon are solo by default
        for (int k = 0; k < count && g_ec < MAX_ENEMIES; k++) {
            for (int tries = 0; tries < 120; tries++) {
                int r = 1 + rand()%(ROWS-2), c = 1 + rand()%(COLS-2);
                if (MAP[r][c]) continue;
                float wx = c*CELL+CELL/2.f, wz = r*CELL+CELL/2.f;
                float minDist = (t == 3 || t == 6 || t == 9) ? 14.f : 10.f;
                if (hypotf(wx-g_p.pos.x, wz-g_p.pos.z) < minDist) continue;
                Enemy *ne = &g_e[g_ec++]; *ne = (Enemy){0};
                ne->pos = (Vector3){wx, PlatGroundAt(wx,wz,100.f), wz};
                ne->type = t; ne->state = ES_PATROL;
                if      (t == 8)  ne->pos.y = 1.5f;  // cacodemon
else if (t == 11) ne->pos.y = 2.2f;  // lost soul
else if (t == 12) ne->pos.y = 2.0f;  // pain elemental
                ne->hp = ne->maxHp = ET_HP[t];
                ne->speed = ET_SPD[t];
                ne->dmg = ET_DMG[t]; ne->rate = ET_RATE[t];
                ne->cd = (t == 5) ? 0.4f : (t == 6) ? 0.6f : ET_RATE[t];
                ne->alertR = ET_AR[t]; ne->atkR = ET_ATK[t];
                ne->score = ET_SC[t]; ne->active = true;
                float ang = (float)rand()/RAND_MAX * 6.28f;
                ne->pd = (Vector3){sinf(ang), 0, cosf(ang)};
                ne->stateT = 1.f + (float)rand()/RAND_MAX * 2.f;
                break;
            }
        }
    } else {
        // Normal mode: spawn wave 1 (17 enemies). First 7 spawns are
        // GUARANTEED — one of each non-boss type — so the player encounters
        // every enemy on wave 1 instead of being at the mercy of percentages.
        // Cacodemon (8) joins from wave 2; cyber demon (9) is wave 3+ only
        // (boss-tier, kept rare).
        static const int guaranteed[] = {0, 2, 1, 4, 5, 6, 7};
        const int guaranteedCount = (int)(sizeof(guaranteed)/sizeof(guaranteed[0]));
        for (int k=0;k<17&&g_ec<MAX_ENEMIES;k++) {
            for (int tries=0;tries<120;tries++) {
                int r=1+rand()%(ROWS-2), c=1+rand()%(COLS-2);
                if (MAP[r][c]) continue;
                float wx=c*CELL+CELL/2.f, wz=r*CELL+CELL/2.f;
                if (hypotf(wx-g_p.pos.x,wz-g_p.pos.z)<10.f) continue;
                int type;
                if (k < guaranteedCount) {
                    type = guaranteed[k];
                } else {
                    // Filler mix: lean on chefs but keep variety. Wave 1 only
                    // — caco/cyber not yet unlocked here.
                    float rng = (float)rand()/RAND_MAX;
                    type = rng < 0.42f ? 0
                         : rng < 0.58f ? 2
                         : rng < 0.66f ? 1
                         : rng < 0.76f ? 4
                         : rng < 0.86f ? 5
                         : rng < 0.92f ? 6
                         :              7;   // soldier
                }
                Enemy *ne=&g_e[g_ec++]; *ne=(Enemy){0};
                ne->pos=(Vector3){wx, PlatGroundAt(wx,wz,100.f), wz};
                ne->type=type; ne->state=ES_PATROL;
                if      (type == 8)  ne->pos.y = 1.5f;  // cacodemon
                else if (type == 11) ne->pos.y = 2.2f;  // lost soul
                else if (type == 12) ne->pos.y = 2.0f;  // pain elemental
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
    DisableCursor();   // engage cursor capture once — NOT every frame in DrawHUD
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

// ── HIGH-SCORE SUBMISSION ───────────────────────────────────────────────────
// Build the per-run JSON payload for the leaderboard API and POST it.
// Web build: emscripten-side fetch via EM_JS (no curl, no native lib).
// macOS native: NSURLSession via the Objective-C bridge in score_post.m.
// Windows: no-op for now (could add WinHTTP later if it matters).
//
// Both backends call back into IronFistRankReceived() once the server's
// {"rank": N, ...} response is parsed, so the death screen can show
// "PLACED #N" next to the SUBMITTED status. Asynchronous — the rank
// arrives a frame or two after the POST and the death screen updates
// when it does.
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
EMSCRIPTEN_KEEPALIVE
void IronFistRankReceived(int rank) {
    if (rank > 0) g_lastRank = rank;
}
EM_JS(void, IronFistPostScoreWeb, (const char *json), {
    fetch('https://ironfist.ximg.app/api/scores', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: UTF8ToString(json)
    })
    .then(r => r.ok ? r.json() : null)
    .then(j => {
        if (j && typeof j.rank === 'number') {
            // Module._IronFistRankReceived is the WASM-exported symbol
            // emscripten generates from the EMSCRIPTEN_KEEPALIVE function
            // above. The leading underscore is the C-symbol mangling.
            Module._IronFistRankReceived(j.rank);
        }
    })
    .catch(()=>{});
});
#elif defined(__APPLE__)
extern void IronFistPostScoreMacOS(const char *json);  // src/score_post.m
// Called from score_post.m's NSURLSession completion handler once the
// JSON response arrives (see corresponding extern declaration there).
void IronFistRankReceived(int rank) {
    if (rank > 0) g_lastRank = rank;
}
#endif

static const char *ScoreWeaponName(int w) {
    return (w == 0) ? "shotgun"
         : (w == 1) ? "machinegun"
         : (w == 2) ? "launcher"
         : (w == 3) ? "tesla"
         :            "unknown";
}

static void SubmitScore(void) {
    char json[512];
    snprintf(json, sizeof(json),
        "{\"initials\":\"%c%c%c\",\"score\":%d,\"kills\":%d,\"wave\":%d,"
        "\"weapon\":\"%s\",\"time\":%.1f,\"pickups\":%d,\"shots\":%d,\"damage\":%d}",
        g_initials[0], g_initials[1], g_initials[2],
        g_p.score, g_p.kills, g_wave,
        ScoreWeaponName(g_p.weapon),
        (double)g_statTime, g_statPickups, g_statShots, (int)g_statDamage);
#ifdef __EMSCRIPTEN__
    IronFistPostScoreWeb(json);
#elif defined(__APPLE__)
    IronFistPostScoreMacOS(json);
#else
    (void)json;  // Windows: not yet wired
#endif
}

// ── SPRITE BROWSER (debug, F8) ──────────────────────────────────────────────
// Flip through every PNG in a curated list of source-asset folders so we
// can see what each Beautiful-Doom / DOOM-style-Game prefix actually looks
// like. The Beautiful-Doom revenant alone has 12+ sprite prefixes (REVP,
// REVI, REVN, REVM, REVX, RSKE, RMIL, RMIR, RMIS, SKEB, SSKE, FBXP) and
// guessing which is "walk" vs "pain" vs "missile-tracer" from filenames is
// painful — this lets you eyeball each one and update the copy script
// accordingly. Absolute paths because this is dev-only and only ever runs
// out of the repo on the developer's Mac.
static const char *SB_FOLDERS[] = {
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/Revenant",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/LostSoul",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/PainElemental",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/Cacodemon",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/Cyberdemon",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/Arachnotron",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/ArchVile",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/BaronOfHell",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/HellKnight",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/Mancubus",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/DoomImp",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/PinkyDemon",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/SpiderMastermind",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/IconOfSin",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/Cleaner",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/ShotgunGuy",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/ChaingunGuy",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/RifleGuy",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/Zombieman",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/Beautiful-Doom/Sprites/MONSTERS/WolfensteinSS",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/DOOM-style-Game/resources/sprites/npc/soldier",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/DOOM-style-Game/resources/sprites/npc/caco_demon",
    "/Users/richardblundell/Desktop/repos/Iron-Fist/third_party/DOOM-style-Game/resources/sprites/npc/cyber_demon",
};
#define SB_FOLDER_COUNT (int)(sizeof(SB_FOLDERS)/sizeof(SB_FOLDERS[0]))

static bool          g_sbActive  = false;
static int           g_sbFolder  = 0;
static int           g_sbFile    = 0;
static int           g_sbZoom    = 4;
static FilePathList  g_sbList;
static bool          g_sbHasList = false;
static Texture2D     g_sbTex;
static bool          g_sbHasTex  = false;

static void SBLoadFile(void) {
    if (g_sbHasTex) { UnloadTexture(g_sbTex); g_sbHasTex = false; }
    if (!g_sbHasList || g_sbList.count == 0) return;
    if (g_sbFile < 0) g_sbFile = (int)g_sbList.count - 1;
    if (g_sbFile >= (int)g_sbList.count) g_sbFile = 0;
    g_sbTex = LoadTexture(g_sbList.paths[g_sbFile]);
    if (g_sbTex.id) {
        SetTextureFilter(g_sbTex, TEXTURE_FILTER_POINT);
        g_sbHasTex = true;
    }
}

static void SBLoadFolder(void) {
    if (g_sbHasList) { UnloadDirectoryFiles(g_sbList); g_sbHasList = false; }
    g_sbList = LoadDirectoryFilesEx(SB_FOLDERS[g_sbFolder], ".png", false);
    g_sbHasList = true;
    g_sbFile = 0;
    SBLoadFile();
}

static void SBOpen(void) {
    g_sbActive = true;
    if (g_sbFolder < 0 || g_sbFolder >= SB_FOLDER_COUNT) g_sbFolder = 0;
    SBLoadFolder();
}

static void SBClose(void) {
    g_sbActive = false;
    if (g_sbHasTex)  { UnloadTexture(g_sbTex);          g_sbHasTex  = false; }
    if (g_sbHasList) { UnloadDirectoryFiles(g_sbList);  g_sbHasList = false; }
}

static void SBStep(void) {
    if (IsKeyPressed(KEY_F8) || IsKeyPressed(KEY_ESCAPE)) {
        SBClose();
        g_gs = GS_MENU;  // ESC always lands on the main menu
        return;
    }
    if (IsKeyPressed(KEY_LEFT))  { g_sbFile--; SBLoadFile(); }
    if (IsKeyPressed(KEY_RIGHT)) { g_sbFile++; SBLoadFile(); }
    if (IsKeyPressed(KEY_LEFT_BRACKET))  {
        g_sbFolder = (g_sbFolder + SB_FOLDER_COUNT - 1) % SB_FOLDER_COUNT;
        SBLoadFolder();
    }
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
        g_sbFolder = (g_sbFolder + 1) % SB_FOLDER_COUNT;
        SBLoadFolder();
    }
    if (IsKeyPressed(KEY_EQUAL) && g_sbZoom < 12) g_sbZoom++;
    if (IsKeyPressed(KEY_MINUS) && g_sbZoom > 1)  g_sbZoom--;

    BeginDrawing();
    ClearBackground((Color){25, 25, 35, 255});
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    // Centred sprite at current zoom.
    if (g_sbHasTex && g_sbTex.id) {
        int dw = g_sbTex.width  * g_sbZoom;
        int dh = g_sbTex.height * g_sbZoom;
        DrawTextureEx(g_sbTex, (Vector2){(sw - dw) * 0.5f, (sh - dh) * 0.5f},
                      0.f, (float)g_sbZoom, WHITE);
    }

    // Top header — folder name + index counts.
    const char *folder = SB_FOLDERS[g_sbFolder];
    const char *folderTail = strrchr(folder, '/');
    folderTail = folderTail ? folderTail + 1 : folder;
    char hdr[256];
    int fileCount = g_sbHasList ? (int)g_sbList.count : 0;
    int fileNum   = (fileCount > 0) ? g_sbFile + 1 : 0;
    snprintf(hdr, sizeof(hdr), "FOLDER [%d/%d] %s   FILE %d/%d",
             g_sbFolder + 1, SB_FOLDER_COUNT, folderTail, fileNum, fileCount);
    DrawText(hdr, 20, 20, 22, (Color){240, 200, 80, 255});

    // Big filename label so the prefix is easy to read.
    if (g_sbHasList && fileCount > 0 && g_sbFile < fileCount) {
        const char *path = g_sbList.paths[g_sbFile];
        const char *fn = strrchr(path, '/');
        fn = fn ? fn + 1 : path;
        DrawText(fn, 20, 50, 32, WHITE);
    } else {
        DrawText("(no PNGs in folder)", 20, 50, 28, RED);
    }

    DrawText("LEFT/RIGHT file   [ ] folder   - / + zoom   ESC/F8 exit",
             20, sh - 28, 16, (Color){180, 180, 200, 255});
    EndDrawing();
}

// Find the closest live enemy in the player's rear 180° arc within `range`
// metres. Returns the enemy index or -1 if none. Used by the rear-warning
// arc / stinger / minimap-tint indicators.
static int FindClosestRearEnemy(float range) {
    float syw = sinf(g_p.yaw + 3.14159f), cyw = cosf(g_p.yaw + 3.14159f);
    float fx = syw, fz = cyw;
    int best = -1;
    float bestD = range;
    for (int i = 0; i < g_ec; i++) {
        Enemy *e = &g_e[i];
        if (!e->active || e->dying) continue;
        float dx = e->pos.x - g_p.pos.x;
        float dz = e->pos.z - g_p.pos.z;
        float d = sqrtf(dx*dx + dz*dz);
        if (d < 0.01f || d > range) continue;
        float fdot = (dx*fx + dz*fz) / d;
        if (fdot > -0.1f) continue;     // dot < -0.1 ~= behind 96°+ arc
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// PlaySound wrapper that pans by enemy bearing relative to the player when
// g_warnPan is on. raylib pan: 0 = full right, 1 = full left, 0.5 = centre.
static void PlayPositionalSound(Sound s, Vector3 pos) {
    if (g_warnPan) {
        float dx = pos.x - g_p.pos.x;
        float dz = pos.z - g_p.pos.z;
        float len = sqrtf(dx*dx + dz*dz);
        if (len > 0.001f) { dx /= len; dz /= len; }
        float syw = sinf(g_p.yaw + 3.14159f), cyw = cosf(g_p.yaw + 3.14159f);
        // Player's RIGHT axis in xz: perpendicular to forward (sy, cy)
        float rx = cyw, rz = -syw;
        float dot = dx*rx + dz*rz;          // [-1,1], +1 = directly right
        float pan = 0.5f - 0.5f * dot;      // raylib: 0=R, 1=L
        if (pan < 0.f) pan = 0.f; if (pan > 1.f) pan = 1.f;
        SetSoundPan(s, pan);
    } else {
        SetSoundPan(s, 0.5f);  // centre — undoes any previous pan
    }
    PlaySound(s);
}

// ── DEV CONSOLE ( ` toggles ) ───────────────────────────────────────────────
// Quake-style drop-down debug console. Backtick (or shift+~) toggles. While
// open, the player input (movement, mouse-look, fire) freezes but enemies +
// bullets + particles keep ticking behind the panel. Commands run against
// the live game state — give ammo, toggle god mode, kill all live enemies,
// teleport, advance wave, etc. Type `help` for the list.
//
// Active in GS_PLAY only — opening it from the menu / picker / death screen
// has nowhere useful to act, so the toggle is gated to in-play state.
#define CON_INPUT_MAX  256
#define CON_HIST_LINES 96
#define CON_CMD_HIST   16

static bool   g_conOpen      = false;
static char   g_conInput[CON_INPUT_MAX] = {0};
static int    g_conInputLen  = 0;
static char   g_conLines[CON_HIST_LINES][CON_INPUT_MAX] = {{0}};
static int    g_conLineCount = 0;     // how many slots are populated (caps at HIST_LINES)
static int    g_conLineHead  = 0;     // next slot to overwrite (ring buffer)
static char   g_conCmdHist[CON_CMD_HIST][CON_INPUT_MAX] = {{0}};
static int    g_conCmdCount  = 0;
static int    g_conCmdHead   = 0;
static int    g_conCmdNav    = -1;    // -1 = current input; else steps back from head
// Slide-down animation. 0 = fully retracted (panel invisible), 1 = fully
// deployed (panel at half-screen). Eases toward whatever g_conOpen wants;
// ConDraw is called as long as g_conAnim > 0 so the close animation still
// plays after g_conOpen flips back to false.
static float  g_conAnim      = 0.f;
#define CON_ANIM_SPEED 8.0f   // 1/8 sec from closed to fully open

// g_god and g_cheated declared at the top of the file alongside g_paused
// so the damage-application sites in UpdEnemies / UpdBullets can see them
// without forward decls.

static void ConPrintLine(const char *s) {
    strncpy(g_conLines[g_conLineHead], s, CON_INPUT_MAX-1);
    g_conLines[g_conLineHead][CON_INPUT_MAX-1] = 0;
    g_conLineHead = (g_conLineHead + 1) % CON_HIST_LINES;
    if (g_conLineCount < CON_HIST_LINES) g_conLineCount++;
}

static void ConPrintf(const char *fmt, ...) {
    char buf[CON_INPUT_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ConPrintLine(buf);
}

static void ConClearScrollback(void) {
    memset(g_conLines, 0, sizeof(g_conLines));
    g_conLineCount = 0;
    g_conLineHead  = 0;
}

static void ConOpenPanel(void) {
    g_conOpen = true;
    g_conCmdNav = -1;
    EnableCursor();  // free OS cursor while typing — same trick as pause
}

static void ConClosePanel(void) {
    g_conOpen = false;
    g_conInputLen = 0;
    g_conInput[0] = 0;
    g_conCmdNav = -1;
    if (g_gs == GS_PLAY) {
        DisableCursor();
        SetMousePosition(GetScreenWidth()/2, GetScreenHeight()/2);
        (void)GetMouseDelta();  // drain accumulated delta so camera doesn't snap
    }
}

static void ConPushCmd(const char *line) {
    strncpy(g_conCmdHist[g_conCmdHead], line, CON_INPUT_MAX-1);
    g_conCmdHist[g_conCmdHead][CON_INPUT_MAX-1] = 0;
    g_conCmdHead = (g_conCmdHead + 1) % CON_CMD_HIST;
    if (g_conCmdCount < CON_CMD_HIST) g_conCmdCount++;
}

static void ConRecallCmd(void) {
    if (g_conCmdNav < 0 || g_conCmdCount == 0) {
        g_conInput[0] = 0; g_conInputLen = 0; return;
    }
    int idx = (g_conCmdHead - 1 - g_conCmdNav + CON_CMD_HIST) % CON_CMD_HIST;
    strncpy(g_conInput, g_conCmdHist[idx], CON_INPUT_MAX-1);
    g_conInput[CON_INPUT_MAX-1] = 0;
    g_conInputLen = (int)strlen(g_conInput);
}

// ── command dispatch ────────────────────────────────────────────────────────
// Tokenise a line in-place by terminating tokens with NULs and stashing
// pointers in argv[]. Returns argc.
static int ConTokenise(char *line, char *argv[], int maxArgs) {
    int argc = 0;
    char *p = line;
    while (*p && argc < maxArgs) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = 0; p++; }
    }
    return argc;
}

// Alive() and DmgEnemy() are defined elsewhere in game.c; both are non-
// static at file scope, so no forward decl is needed here.

static void ConExecute(const char *line) {
    char buf[CON_INPUT_MAX];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf)-1] = 0;
    char *argv[8] = {0};
    int argc = ConTokenise(buf, argv, 8);
    if (argc == 0) return;
    const char *cmd = argv[0];

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        ConPrintf("commands:");
        ConPrintf("  give all|health|shells|rockets|bullets|cells|mg|tesla [N]");
        ConPrintf("  god             toggle invulnerability");
        ConPrintf("  kill            damage all live enemies to 0 HP");
        ConPrintf("  wave N          set wave counter (next wave kicks in on clear)");
        ConPrintf("  pos             print player x,y,z + yaw");
        ConPrintf("  tp X Z          teleport to world X,Z (y auto-snaps to floor)");
        ConPrintf("  clear           empty the scrollback");
        ConPrintf("  quit / exit     exit the game");
        ConPrintf("  warn [target]   toggle rear-enemy warning indicators");
    } else if (!strcmp(cmd, "give")) {
        if (argc < 2) { ConPrintf("usage: give <kind> [N]"); return; }
        g_cheated = true;  // any `give` is a cheat
        int n = (argc >= 3) ? atoi(argv[2]) : 0;
        if (!strcmp(argv[1], "all")) {
            g_p.hp = g_p.maxHp;
            g_p.shells  = 99;
            g_p.rockets = 30;
            g_p.bullets = 200;
            g_p.mgAmmo  = 300;
            g_p.cells   = 99;
            g_p.hasTesla = true;
            ConPrintf("filled all ammo + tesla unlocked");
        } else if (!strcmp(argv[1], "health"))  { g_p.hp = fminf(g_p.maxHp, g_p.hp + (n>0?n:35));    ConPrintf("hp = %.0f / %.0f", (double)g_p.hp, (double)g_p.maxHp); }
        else if   (!strcmp(argv[1], "shells"))  { g_p.shells  = (int)fminf(99,  g_p.shells  + (n>0?n:32));  ConPrintf("shells = %d",  g_p.shells); }
        else if   (!strcmp(argv[1], "rockets")) { g_p.rockets = (int)fminf(30,  g_p.rockets + (n>0?n:8));   ConPrintf("rockets = %d", g_p.rockets); }
        else if   (!strcmp(argv[1], "bullets")) { g_p.bullets = (int)fminf(200, g_p.bullets + (n>0?n:50));  ConPrintf("bullets = %d", g_p.bullets); }
        else if   (!strcmp(argv[1], "mg"))      { g_p.mgAmmo  = (int)fminf(300, g_p.mgAmmo  + (n>0?n:120)); ConPrintf("mg = %d",      g_p.mgAmmo); }
        else if   (!strcmp(argv[1], "cells"))   { g_p.cells   = (int)fminf(99,  g_p.cells   + (n>0?n:30));  ConPrintf("cells = %d",   g_p.cells); }
        else if   (!strcmp(argv[1], "tesla"))   { g_p.hasTesla = true; g_p.cells = (int)fminf(99, g_p.cells + 30); ConPrintf("tesla cannon unlocked"); }
        else      { ConPrintf("unknown give: %s", argv[1]); }
    } else if (!strcmp(cmd, "god")) {
        g_god = !g_god;
        if (g_god) g_cheated = true;  // toggling god ON poisons this run
        ConPrintf("god mode %s", g_god ? "ON" : "OFF");
    } else if (!strcmp(cmd, "kill")) {
        g_cheated = true;
        int n = 0;
        for (int i = 0; i < g_ec; i++) {
            if (g_e[i].active && !g_e[i].dying) {
                DmgEnemy(i, g_e[i].hp + 1.f);
                n++;
            }
        }
        ConPrintf("killed %d enemies", n);
    } else if (!strcmp(cmd, "wave")) {
        if (argc < 2) { ConPrintf("usage: wave N"); return; }
        g_cheated = true;
        int w = atoi(argv[1]);
        if (w < 1) w = 1;
        // Force the new wave to start RIGHT NOW: kill any live enemies so
        // Alive() returns 0, set g_wave = N-1 and g_bossInterlude = true
        // so the next UpdEnemies tick takes the "boss just died -> wave++"
        // branch and spawns wave N's enemies via the normal scaling
        // (cnt = 10 + g_wave * 4, full random type roll).
        int killed = 0;
        for (int i = 0; i < g_ec; i++) {
            if (g_e[i].active && !g_e[i].dying) {
                DmgEnemy(i, g_e[i].hp + 1.f);
                killed++;
            }
        }
        g_wave = w - 1;
        g_bossInterlude = true;
        ConPrintf("wave -> %d  (killed %d, spawning new wave)", w, killed);
    } else if (!strcmp(cmd, "pos")) {
        ConPrintf("pos = (%.2f, %.2f, %.2f)  yaw = %.2f rad",
                  (double)g_p.pos.x, (double)g_p.pos.y, (double)g_p.pos.z, (double)g_p.yaw);
    } else if (!strcmp(cmd, "tp")) {
        if (argc < 3) { ConPrintf("usage: tp X Z"); return; }
        g_cheated = true;
        float x = (float)atof(argv[1]);
        float z = (float)atof(argv[2]);
        g_p.pos.x = x;
        g_p.pos.z = z;
        g_p.pos.y = PlatGroundAt(x, z, 100.f);
        ConPrintf("tp -> (%.2f, %.2f, %.2f)", (double)g_p.pos.x, (double)g_p.pos.y, (double)g_p.pos.z);
    } else if (!strcmp(cmd, "clear")) {
        ConClearScrollback();
    } else if (!strcmp(cmd, "warn")) {
        // Toggle the rear-enemy warning indicators independently. Pure UX,
        // not a cheat — does NOT set g_cheated.
        if (argc < 2) {
            ConPrintf("rear-warning toggles:");
            ConPrintf("  arc     %s   (red arrow at screen edge)",
                      g_warnArc ? "ON " : "off");
            ConPrintf("  pan     %s   (stereo-pan enemy sounds)",
                      g_warnPan ? "ON " : "off");
            ConPrintf("  stinger %s   (vocal when enemy lingers behind)",
                      g_warnStinger ? "ON " : "off");
            ConPrintf("  minimap %s   (red tint on rear minimap half)",
                      g_warnMinimap ? "ON " : "off");
            ConPrintf("usage: warn <arc|pan|stinger|minimap|all|off>");
            return;
        }
        if      (!strcmp(argv[1], "arc"))     { g_warnArc     = !g_warnArc;     ConPrintf("arc indicator %s",     g_warnArc?"ON":"OFF"); }
        else if (!strcmp(argv[1], "pan"))     { g_warnPan     = !g_warnPan;     ConPrintf("audio panning %s",     g_warnPan?"ON":"OFF"); }
        else if (!strcmp(argv[1], "stinger")) { g_warnStinger = !g_warnStinger; ConPrintf("rear stinger %s",      g_warnStinger?"ON":"OFF"); }
        else if (!strcmp(argv[1], "minimap")) { g_warnMinimap = !g_warnMinimap; ConPrintf("minimap rear-tint %s", g_warnMinimap?"ON":"OFF"); }
        else if (!strcmp(argv[1], "all"))     { g_warnArc = g_warnPan = g_warnStinger = g_warnMinimap = true;  ConPrintf("all rear warnings ON"); }
        else if (!strcmp(argv[1], "off"))     { g_warnArc = g_warnPan = g_warnStinger = g_warnMinimap = false; ConPrintf("all rear warnings OFF"); }
        else ConPrintf("unknown warn target: %s", argv[1]);
    } else if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) {
        g_quit = true;
    } else {
        ConPrintf("unknown command: %s  (try 'help')", cmd);
    }
}

static void ConHandleInput(void) {
    // Printable chars typed via GetCharPressed (handles shift, layout, etc.)
    int c;
    while ((c = GetCharPressed()) > 0) {
        if (c < 32 || c >= 127) continue;
        if (c == '`' || c == '~') continue;  // those toggle the panel
        if (g_conInputLen < CON_INPUT_MAX-1) {
            g_conInput[g_conInputLen++] = (char)c;
            g_conInput[g_conInputLen] = 0;
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && g_conInputLen > 0) {
        g_conInput[--g_conInputLen] = 0;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        if (g_conInputLen > 0) {
            ConPrintf("> %s", g_conInput);
            ConPushCmd(g_conInput);
            ConExecute(g_conInput);
        }
        g_conInput[0] = 0;
        g_conInputLen = 0;
        g_conCmdNav = -1;
    }
    if (IsKeyPressed(KEY_UP)) {
        if (g_conCmdNav + 1 < g_conCmdCount) {
            g_conCmdNav++;
            ConRecallCmd();
        }
    }
    if (IsKeyPressed(KEY_DOWN)) {
        if (g_conCmdNav > 0) {
            g_conCmdNav--;
            ConRecallCmd();
        } else if (g_conCmdNav == 0) {
            g_conCmdNav = -1;
            g_conInput[0] = 0;
            g_conInputLen = 0;
        }
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        ConClosePanel();
    }
}

static void ConDraw(void) {
    // Tick the slide animation toward the current open/closed target. Run
    // here rather than in StepFrame so a single guard ("draw if animating
    // OR open") keeps things simple.
    float target = g_conOpen ? 1.f : 0.f;
    float dt = GetFrameTime();
    if (g_conAnim < target) g_conAnim = fminf(target, g_conAnim + dt * CON_ANIM_SPEED);
    else if (g_conAnim > target) g_conAnim = fmaxf(target, g_conAnim - dt * CON_ANIM_SPEED);
    if (g_conAnim <= 0.f) return;  // fully retracted — nothing to draw

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int fullH = sh / 2;
    int panelH = (int)(fullH * g_conAnim);
    if (panelH < 1) return;

    DrawRectangle(0, 0, sw, panelH, (Color){10, 10, 20, 220});
    DrawRectangle(0, panelH, sw, 2, (Color){80, 200, 120, 200});
    // Scrollback above the prompt, bottom-up. Clipped naturally as panelH shrinks.
    const int lineH = 18;
    int y = panelH - 30 - lineH;
    int idx = g_conLineHead;
    int n   = g_conLineCount;
    while (n > 0 && y >= 4) {
        idx = (idx - 1 + CON_HIST_LINES) % CON_HIST_LINES;
        DrawText(g_conLines[idx], 12, y, 16, (Color){200, 220, 230, 255});
        y -= lineH;
        n--;
    }
    // Prompt at the bottom of the panel — only show once the panel is
    // mostly extended, otherwise it reads as a flicker during the slide.
    if (g_conAnim > 0.6f) {
        char prompt[CON_INPUT_MAX + 4];
        snprintf(prompt, sizeof(prompt), "> %s", g_conInput);
        DrawText(prompt, 12, panelH - 26, 18, (Color){240, 240, 120, 255});
        if (((int)(GetTime() * 2.f)) & 1) {
            int textW = MeasureText(prompt, 18);
            DrawRectangle(12 + textW + 2, panelH - 26, 8, 18, (Color){240, 240, 120, 200});
        }
    }
}

static void StepFrame(void) {
    float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;
    DebugLogTick();
    // F8 — toggle the sprite browser (debug). When active, swallows the
    // entire frame and renders the single-sprite viewer instead of the game.
    // macOS-only because it reads absolute /Users/... source paths.
#if !defined(PLATFORM_WEB) && !defined(_WIN32)
    if (IsKeyPressed(KEY_F8) && !g_sbActive) { SBOpen(); }
    if (g_sbActive) { SBStep(); return; }
#endif
    // Backtick (or shift+~) toggles the dev console — only meaningful in
    // GS_PLAY where there's live game state to act on. ConHandleInput()
    // captures keys ENTER/UP/DOWN/BACKSPACE/ESC and printable chars while
    // open; the GS_PLAY input + tick blocks below check g_conOpen and gate
    // accordingly so the player freezes but enemies/bullets keep moving.
    if (g_gs == GS_PLAY) {
        if (IsKeyPressed(KEY_GRAVE)) {
            if (g_conOpen) ConClosePanel();
            else           ConOpenPanel();
        }
        if (g_conOpen) ConHandleInput();
    }
    // Alt+Enter — fullscreen toggle. Borderless-windowed flavour: no
    // resolution change, no swoop animation, instant. Skipped on web — the
    // browser shell owns canvas sizing and the Fullscreen API needs its own
    // gesture handling. On the menu, Enter alone starts a game; the modifier
    // check below ensures Alt+Enter doesn't double-fire as "start game".
#ifndef PLATFORM_WEB
    if ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER)) {
        ToggleBorderlessWindowed();
    }
#endif
    // ── Music: title track on menu, alternating in-game playlist otherwise ──
    // State-transition handler swaps which stream is active when entering /
    // leaving GS_MENU. Both streams are kept loaded so the swap is just a
    // Stop + Play call, no disk hit.
    {
        static GameState s_prevGs = GS_MENU;
        static bool s_initialized = false;
        if (!s_initialized) { s_prevGs = g_gs; s_initialized = true; }
        if (g_gs != s_prevGs) {
            bool toMenu   = (g_gs == GS_MENU);
            bool fromMenu = (s_prevGs == GS_MENU);
            if (toMenu && !fromMenu) {
                if (g_musicOK)      StopMusicStream(g_musicTracks[g_musicIdx]);
                if (g_titleMusicOK) PlayMusicStream(g_titleMusic);
            } else if (!toMenu && fromMenu) {
                if (g_titleMusicOK) StopMusicStream(g_titleMusic);
                if (g_musicOK) {
                    // Randomise which track plays first this game so the
                    // order isn't always hell-march -> funeral-march. Pick
                    // a uniformly random LOADED track.
                    int cand[MUSIC_TRACK_COUNT]; int nc = 0;
                    for (int t = 0; t < MUSIC_TRACK_COUNT; t++)
                        if (g_musicTracksOK[t]) cand[nc++] = t;
                    if (nc > 0) g_musicIdx = cand[rand() % nc];
                    SetMusicVolume(g_musicTracks[g_musicIdx], g_musicVol);
                    PlayMusicStream(g_musicTracks[g_musicIdx]);
                }
            }
            s_prevGs = g_gs;
        }
    }
    if (g_gs == GS_MENU && g_titleMusicOK) {
        UpdateMusicStream(g_titleMusic);
    } else if (g_musicOK) {
        UpdateMusicStream(g_musicTracks[g_musicIdx]);
        // Track alternation: when the current track plays out (and we're
        // not paused), advance to the next loaded track in the playlist.
        if (!g_paused && !IsMusicStreamPlaying(g_musicTracks[g_musicIdx])) {
            // Pick a uniformly random LOADED track that isn't the current
            // one — proper shuffle behaviour. With 2 loaded tracks this
            // collapses to "always switch", with 3+ it actually shuffles.
            int cand[MUSIC_TRACK_COUNT]; int nc = 0;
            for (int t = 0; t < MUSIC_TRACK_COUNT; t++)
                if (g_musicTracksOK[t] && t != g_musicIdx) cand[nc++] = t;
            int next;
            if (nc > 0) next = cand[rand() % nc];
            else        next = g_musicIdx;  // only one track loaded — replay it
            if (g_musicTracksOK[next]) {
                StopMusicStream(g_musicTracks[g_musicIdx]);
                g_musicIdx = next;
                SetMusicVolume(g_musicTracks[g_musicIdx], g_musicVol);
                PlayMusicStream(g_musicTracks[g_musicIdx]);
            }
        }
    }
    // Music volume: - / + applies to title + every in-game track so the
    // active stream and any future swap come in at the same level.
    if (g_musicOK || g_titleMusicOK) {
        bool vDown = IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT);
        bool vUp   = IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD);
        if (vDown) g_musicVol = fmaxf(0.f,  g_musicVol - 0.1f);
        if (vUp)   g_musicVol = fminf(1.5f, g_musicVol + 0.1f);
        if (vDown || vUp) {
            for (int i = 0; i < MUSIC_TRACK_COUNT; i++)
                if (g_musicTracksOK[i]) SetMusicVolume(g_musicTracks[i], g_musicVol);
            if (g_titleMusicOK) SetMusicVolume(g_titleMusic, g_musicVol);
            SaveMusicVol();
            char buf[48]; snprintf(buf, 48, "MUSIC VOL %d%%", (int)(g_musicVol * 100.f + 0.5f));
            Msg(buf);
        }
    }

    if (g_gs==GS_MENU) {
        bool altHeld = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        if (!altHeld && (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)||IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
            g_arenaMode = false;
            InitGame();
        }
        if (IsKeyPressed(KEY_A)) {
            g_pickerT = 0.f;
            g_gs = GS_PICK_ENEMY;
        }
        // ESC on the main menu is the only place that quits the game —
        // every other state's ESC handler bounces back here first.
        if (IsKeyPressed(KEY_ESCAPE)) g_quit = true;
        // S — open the sprite browser (debug). Same effect as F8 but
        // doesn't need the fn-modifier on a Mac laptop. macOS-only —
        // the browser reads absolute /Users/... paths that don't exist
        // in the WASM sandbox or a Windows build.
#if !defined(PLATFORM_WEB) && !defined(_WIN32)
        if (IsKeyPressed(KEY_S) && !g_sbActive) { SBOpen(); }
#endif
    } else if (g_gs == GS_PICK_ENEMY) {
        // Enemy picker — navigate 13 picker slots: 10 in-game enemies (0..9)
        // plus 3 preview-only Beautiful-Doom enemies (10/11/12 — revenant,
        // lostsoul, painelem) you can spawn in the arena to look at, but
        // they aren't yet in waves. Arrow keys cycle all 13; 1-9 + 0 jump
        // to slots 0..9; slots 10/11/12 reachable only via arrows.
        g_pickerT += dt;
        if (IsKeyPressed(KEY_ESCAPE)) g_gs = GS_MENU;
        if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A)) g_pickerIdx = (g_pickerIdx + 12) % 13;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) g_pickerIdx = (g_pickerIdx + 1) % 13;
        for (int k = 0; k < 10; k++) {
            // Number keys 1-9 + 0 (for slot 9) so all are reachable
            int key = (k == 9) ? KEY_ZERO : (KEY_ONE + k);
            if (IsKeyPressed(key)) g_pickerIdx = k;
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            g_arenaMode = true; g_arenaType = g_pickerIdx;
            InitGame();
        }
    } else if (g_gs==GS_PLAY) {
        // Time-played stat — accumulate while actually in-play (not paused).
        if (!g_paused) g_statTime += dt;
        // While the dev console is open, swallow all gameplay-key handling
        // so ESC/P/etc don't double-fire alongside the console's own input.
        if (!g_conOpen && IsKeyPressed(KEY_ESCAPE)){g_gs=GS_MENU;}
        // P toggles a hard pause — Upd* calls and the visibility-stinger
        // logic are gated below so enemies freeze, projectiles hang, and
        // sound timers don't tick. Music keeps playing for ambience.
        if (!g_conOpen && IsKeyPressed(KEY_P)) {
            g_paused = !g_paused;
            // Mute the streaming music while paused — raylib's pause/resume
            // properly halts the decoder so the buffer doesn't drift.
            if (g_musicOK) {
                if (g_paused) PauseMusicStream(g_musicTracks[g_musicIdx]);
                else          ResumeMusicStream(g_musicTracks[g_musicIdx]);
            }
            // SetMasterVolume(0) silences ALL one-shot Sounds too — without
            // this, in-flight SFX (chef-die, mech-rocket, tesla buzz, etc.)
            // keep audibly playing through the pause overlay.
            SetMasterVolume(g_paused ? 0.f : 1.f);
            // Cursor / pointer-lock handling. On web the browser can drop
            // pointer lock for many reasons during pause (ESC keypress,
            // alt-tab, idle timeout, click outside the canvas). If we don't
            // explicitly re-engage it on unpause AND drain any accumulated
            // mouse delta, the next frame the camera reads a giant delta
            // and spins wildly. EnableCursor on pause + DisableCursor +
            // SetMousePosition on unpause keeps the lock state in sync.
            if (g_paused) {
                EnableCursor();
            } else {
                DisableCursor();
                SetMousePosition(GetScreenWidth()/2, GetScreenHeight()/2);
                // Drain whatever delta accumulated between pause and now
                // so the first post-unpause frame doesn't see a huge jump.
                (void)GetMouseDelta();
                g_needMouseRelease = true;
            }
        }
        if (!g_paused) {
        // Player input + physics frozen while the console panel is open
        // (so typing W/A/S/D doesn't move the player). Enemies / bullets /
        // particles / pickups still tick — game world keeps running behind
        // the console, Quake-style.
        if (!g_conOpen) UpdPlayer(dt,&g_cam);
        // Critical-health stinger — fires once when HP crosses below 10%,
        // re-arms once HP climbs back to 20%. Skip while dead so the
        // sample doesn't fire on the death frame itself (the death scream
        // already plays).
        if (!g_p.dead && g_p.maxHp > 0.f) {
            float frac = g_p.hp / g_p.maxHp;
            if (frac < 0.10f && !g_lowHealthFired) {
                if (g_sLowHealthOK) {
                    SetSoundVolume(g_sLowHealth, 1.5f);
                    PlaySound(g_sLowHealth);
                }
                g_lowHealthFired = true;
            } else if (frac >= 0.20f) {
                g_lowHealthFired = false;
            }
        }
        UpdEnemies(dt);
        UpdBullets(dt);
        UpdParts(dt);
        UpdBolts(dt);
        UpdEShots(dt);
        UpdSplats(dt);
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
            // Rear-warning stinger (g_warnStinger) — vocal sample fires
            // once when an enemy stays in the player's rear arc within
            // 6m for >1.5s. Re-arms when the arc clears. The stinger
            // sample reuses the alien-scream alert (slot 3 in the
            // existing alert pool) since it's already loaded and reads
            // appropriately as "behind you".
            {
                static float s_rearTimer = 0.f;
                static bool  s_rearFired = false;
                int rear = g_warnStinger ? FindClosestRearEnemy(6.f) : -1;
                if (rear >= 0) {
                    s_rearTimer += dt;
                    if (s_rearTimer > 1.5f && !s_rearFired) {
                        if (g_sEnemyAlertCount > 3 && g_sEnemyAlertOK[3]) {
                            SetSoundVolume(g_sEnemyAlert[3], 1.6f);
                            PlaySound(g_sEnemyAlert[3]);
                        }
                        s_rearFired = true;
                    }
                } else {
                    s_rearTimer = 0.f;
                    s_rearFired = false;
                }
            }
            // Idle-sigh stinger — when no enemy is visible for >5s the
            // player gets a "deep breath" sample once. Re-arms when an
            // enemy becomes visible again (which would also trigger the
            // alert stinger above on the same frame).
            if (anyVisible) {
                g_quietTime = 0.f;
                g_quietSighFired = false;
            } else {
                g_quietTime += dt;
                if (g_quietTime > 5.f && !g_quietSighFired) {
                    if (g_sIdleSighOK) {
                        SetSoundVolume(g_sIdleSigh, 1.2f);
                        PlaySound(g_sIdleSigh);
                    }
                    g_quietSighFired = true;
                }
            }
        }
        }  // end if (!g_paused) — pause gate from above
    } else if (g_gs==GS_DEAD) {
        // If the player used any cheat-class console command this run
        // (god / give / kill / wave / tp), suppress the leaderboard entry
        // entirely — jump straight to the post-initials restart prompt.
        if (g_cheated && !g_initialsDone) {
            g_initialsDone = true;
            g_initialsSubmitted = false;
        }
        if (!g_initialsDone) {
            // Initials entry. Up/down cycle the highlighted slot through
            // A-Z then 0-9; left/right move between slots; letter and
            // digit keys type directly and auto-advance; backspace steps
            // back; ENTER submits the score; ESC skips submission.
            if (IsKeyPressed(KEY_LEFT))  g_initialsPos = (g_initialsPos + 2) % 3;
            if (IsKeyPressed(KEY_RIGHT)) g_initialsPos = (g_initialsPos + 1) % 3;
            if (IsKeyPressed(KEY_UP)) {
                char c = g_initials[g_initialsPos];
                c = (c == 'Z') ? '0' : (c == '9') ? 'A' : c + 1;
                g_initials[g_initialsPos] = c;
            }
            if (IsKeyPressed(KEY_DOWN)) {
                char c = g_initials[g_initialsPos];
                c = (c == '0') ? 'Z' : (c == 'A') ? '9' : c - 1;
                g_initials[g_initialsPos] = c;
            }
            for (int k = KEY_A; k <= KEY_Z; k++) {
                if (IsKeyPressed(k)) {
                    g_initials[g_initialsPos] = (char)('A' + (k - KEY_A));
                    if (g_initialsPos < 2) g_initialsPos++;
                }
            }
            for (int k = KEY_ZERO; k <= KEY_NINE; k++) {
                if (IsKeyPressed(k)) {
                    g_initials[g_initialsPos] = (char)('0' + (k - KEY_ZERO));
                    if (g_initialsPos < 2) g_initialsPos++;
                }
            }
            if (IsKeyPressed(KEY_BACKSPACE)) {
                if (g_initialsPos > 0) g_initialsPos--;
                g_initials[g_initialsPos] = 'A';
            }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                SubmitScore();
                g_initialsSubmitted = true;
                g_initialsDone = true;
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                // Skip submission — go straight to the restart prompt.
                g_initialsDone = true;
            }
        } else {
            if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)) InitGame();
            // ESC on the (post-initials) death screen quits the app —
            // same exit ramp as ESC on the main menu.
            if (IsKeyPressed(KEY_ESCAPE)) g_quit = true;
        }
    }

    BeginDrawing();
    ClearBackground((Color){4,3,6,255});

    if (g_gs==GS_PLAY||g_gs==GS_DEAD) {
        BeginMode3D(g_cam);
        for (int wmi = 0; wmi < WALL_TEX_COUNT; wmi++)
            DrawModel(g_wallModels[wmi], Vector3Zero(), 1.f, WHITE);
        DrawModel(g_floorModel,Vector3Zero(),1.f,(Color){110,110,110,255});
        DrawModel(g_ceilModel,Vector3Zero(),1.f,(Color){70,70,90,255});
        // Platforms (stairs + raised decks) — unit cube mesh scaled per-platform.
        //
        // Three z-fight defences applied to the visual draw only; collision /
        // step-up / AI all keep the original p->top, p->x0/x1, p->z0/z1:
        //
        // 1. SUB pushes the bottom 0.5m below the floor (was coplanar at y=0).
        // 2. EXP expands x/z bounds outward 0.01m so adjacent platforms with
        //    different heights overlap slightly — the taller volume hides the
        //    shorter neighbour's coplanar side face, killing the fight there.
        // 3. yBias (per-platform pi*0.0005) breaks coplanar TOP faces between
        //    same-height neighbours (stair 3 vs big plat at top=1.35). A
        //    sub-millimetre stagger is invisible to the player but enough for
        //    the depth test to pick a consistent winner.
        const float SUB = 0.5f;
        const float EXP = 0.01f;
        for (int pi = 0; pi < g_platCount; pi++) {
            Platform *p = &g_plats[pi];
            float yBias = pi * 0.0005f;
            float sx = (p->x1 - p->x0) + 2.f*EXP;
            float sy = (p->top + yBias) + SUB;
            float sz = (p->z1 - p->z0) + 2.f*EXP;
            Vector3 center = {
                (p->x0+p->x1)*0.5f,
                ((p->top + yBias) - SUB)*0.5f,
                (p->z0+p->z1)*0.5f
            };
            DrawModelEx(g_platModel, center, (Vector3){0,1,0}, 0.f,
                        (Vector3){sx, sy, sz}, (Color){170,140,110,255});
        }
        // World billboards (pickups, barrels) must be drawn BEFORE enemies:
        // chef billboards depth-test but don't write depth (transparent edges
        // would otherwise clip neighbours), so anything drawn afterwards has
        // no enemy z-value to be occluded by. New world-space sprite types
        // (props, decor, etc.) should follow the same rule — see CLAUDE.md.
        DrawPicks(g_cam);
        // Alpha-test the barrel sprites so the transparent fringe around the
        // flame doesn't write depth and clip enemies passing behind. See the
        // ALPHA-TEST BILLBOARD SHADER block at the top of game.c for why.
        BeginShaderMode(g_alphaShader);
        DrawBarrels(g_cam);
        EndShaderMode();
        DrawEnemies(g_cam);
        DrawBullets();
        DrawParts();
        DrawBolts();
        DrawEShots();
        DrawSplats(g_cam);
        // Ceiling lights drawn LAST so the additive glow shines over enemies,
        // pickups, and bullets — otherwise opaque billboards cover the light cones.
        DrawCeilingLights(g_cam);
        DrawWeapon3D(g_cam);
        EndMode3D();
        DrawSpriteWeapon();
        DrawHUD();
        // Rear-warning arc indicator (g_warnArc) — red arrow at the bottom
        // of the screen pointing toward the closest rear enemy within 12m,
        // alpha scaled by proximity. Draws on top of the HUD; under any
        // pause / death overlays.
        if (g_warnArc) {
            int rear = FindClosestRearEnemy(12.f);
            if (rear >= 0) {
                Enemy *e = &g_e[rear];
                float dx = e->pos.x - g_p.pos.x;
                float dz = e->pos.z - g_p.pos.z;
                float d = sqrtf(dx*dx + dz*dz);
                float syw = sinf(g_p.yaw + 3.14159f), cyw = cosf(g_p.yaw + 3.14159f);
                float rx = cyw, rz = -syw;
                float rdot = (dx*rx + dz*rz) / d;  // [-1,1] right-component
                int sw3 = GetScreenWidth(), sh3 = GetScreenHeight();
                int ax = sw3/2 + (int)(rdot * sw3 / 3);
                int ay = sh3 - 70;
                unsigned char alpha = (unsigned char)(120 + 110 * (1.f - d/12.f));
                Color arrowCol = {220, 60, 60, alpha};
                DrawTriangle(
                    (Vector2){(float)ax, (float)(ay + 22)},
                    (Vector2){(float)(ax - 16), (float)(ay - 12)},
                    (Vector2){(float)(ax + 16), (float)(ay - 12)},
                    arrowCol);
                DrawText("BEHIND", ax - MeasureText("BEHIND", 16)/2,
                         ay - 32, 16, arrowCol);
            }
        }
        if (g_paused) {
            int sw3 = GetScreenWidth(), sh3 = GetScreenHeight();
            DrawRectangle(0, 0, sw3, sh3, (Color){0, 0, 0, 140});
            const char *p = "PAUSED";
            DrawText(p, sw3/2 - MeasureText(p, 80)/2 + 4, sh3/3 + 4, 80, (Color){0,0,0,200});
            DrawText(p, sw3/2 - MeasureText(p, 80)/2,     sh3/3,     80, (Color){255,220,80,255});
            const char *h1 = "PRESS P  -  RESUME GAME";
            const char *h2 = "PRESS ESC  -  EXIT TO MAIN MENU";
            DrawText(h1, sw3/2 - MeasureText(h1, 22)/2, sh3/3 + 110, 22, (Color){120,255,120,240});
            DrawText(h2, sw3/2 - MeasureText(h2, 22)/2, sh3/3 + 140, 22, (Color){255,140,140,240});
        }
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
                         "1 - SHOTGUN   2 - MACHINE GUN   3 - LAUNCHER   4 - TESLA";
        DrawText(ctrl,sw2/2-MeasureText("WASD / ARROWS - MOVE     MOUSE - LOOK     LMB - FIRE",15)/2,sh2/2+20,15,(Color){90,90,90,255});
        const char *st="[ ENTER  /  CLICK  TO  START ]";
        if (sinf(GetTime()*3.f)>0)
            DrawText(st,sw2/2-MeasureText(st,22)/2,sh2*3/4,22,RED);
        const char *at="[ A FOR ARENA - PICK YOUR ENEMY ]";
        DrawText(at,sw2/2-MeasureText(at,18)/2,sh2*3/4+36,18,(Color){240,200,80,255});
#if !defined(PLATFORM_WEB) && !defined(_WIN32)
        const char *sb="[ S FOR SPRITE BROWSER ]";
        DrawText(sb,sw2/2-MeasureText(sb,16)/2,sh2*3/4+62,16,(Color){180,180,200,255});
#endif
        DrawFPS(10,10);
    } else if (g_gs == GS_PICK_ENEMY) {
        // Enemy picker screen — show a big preview of the currently-selected
        // enemy with their walk cycle animating, and the attack frame in a
        // side panel. Player navigates with ← → / 1-7 and confirms with
        // ENTER / SPACE / click.
        int sw2=GetScreenWidth(), sh2=GetScreenHeight();
        ClearBackground((Color){8, 6, 12, 255});
        // Title
        const char *title = "ARENA - PICK YOUR ENEMY";
        DrawText(title, sw2/2 - MeasureText(title, 36)/2, 48, 36, (Color){240,200,80,255});

        // Enemy metadata table — slots 0..9 are full in-game; 10..12 are
        // preview-only (sprites loaded, arena spawn works, but melee AI as a
        // placeholder until proper behaviours are wired in).
        static const char *enemyBlurb[] = {
            "Standard cleaver-swinging chef.",
            "Beefy melee threat. Tanky but slow.",
            "Glass cannon. Closes the gap fast.",
            "Boss tier. Massive HP, huge hitbox.",
            "Wafen-SS officer with MP40. Fast hitscan.",
            "Lobs purple energy balls at you.",
            "Heavy mech, splash-damage rockets.",
            "Doom shotgunner. Hitscan tracer fire.",
            "Floating fireball spitter. Wave 2+.",
            "Cyber Demon - boss-tier rocket spammer.",
            "[PREVIEW] Revenant - skeleton, melee placeholder.",
            "[PREVIEW] Lost Soul - fast charging head.",
            "[PREVIEW] Pain Elemental - floating mob spawner."
        };
        static const char *enemyNamesExt[] = {
            "CHEF", "HEAVY CHEF", "FAST CHEF", "BOSS CHEF",
            "SS GUARD", "MUTANT", "MECH",
            "SOLDIER", "CACODEMON", "CYBER DEMON",
            "REVENANT", "LOST SOUL", "PAIN ELEMENTAL"
        };

        // Preview animation: walk frames cycle at ~5 fps. For 8-rotation
        // enemies (SS, mutant, mech) the rotation slot also cycles 0..7
        // every ~1.6s so the model "spins" through every facing — single-
        // rotation chef variants keep the walk-only animation.
        //
        // Doom rotation indexing: slots 0..4 are unique sprites (front, ¾,
        // side, ¾-back, back); 5..7 are slots 3,2,1 mirrored (so a full
        // 360° rotation cycles 0→1→2→3→4→3'→2'→1'→0).
        int t = g_pickerIdx;
        if (t < 0) t = 0; if (t > 12) t = 12;
        int walkFrame = (int)(g_pickerT * 5.f) % 4;
        int rotIdx    = (int)(g_pickerT * 5.f) % 8;
        static const int   rotSlot[8]  = {0, 1, 2, 3, 4, 3, 2, 1};
        static const bool  rotFlip[8]  = {false,false,false,false,false,true,true,true};
        int slot = rotSlot[rotIdx];
        bool flip = rotFlip[rotIdx];

        // The attack-frame panel cycles too — single-frame enemies alternate
        // between the attack pose and a walk frame so the swing reads as a
        // windup-strike loop. Mutant and mech have 2 native attack frames
        // (charge/fire and L/R-arm fire respectively), so they cycle between
        // those two poses instead. ~2.5 Hz / 0.4s per phase.
        bool atkPhase = ((int)(g_pickerT * 2.5f)) & 1;
        Texture2D walkTex = {0}, atkTex = {0};
        bool walkFlip = false;
        if (t == 0 && g_chefOK) {
            walkTex = g_chefTex[walkFrame];
            atkTex  = (atkPhase && g_chefAtkOK) ? g_chefAtkTex : g_chefTex[walkFrame];
        }
        else if (t == 1 && g_tormOK) {
            walkTex = g_tormTex[walkFrame];
            atkTex  = (atkPhase && g_tormAtkOK) ? g_tormAtkTex : g_tormTex[walkFrame];
        }
        else if (t == 2 && g_schOK) {
            walkTex = g_schTex[walkFrame];
            atkTex  = (atkPhase && g_schAtkOK) ? g_schAtkTex : g_schTex[walkFrame];
        }
        else if (t == 3 && g_bossOK) {
            walkTex = g_bossTex[walkFrame];
            atkTex  = (atkPhase && g_bossAtkOK) ? g_bossAtkTex : g_bossTex[walkFrame];
        }
        else if (t == 4 && g_cultOK) {
            walkTex  = g_cultWalk[walkFrame].rot[slot];
            walkFlip = flip;
            atkTex   = (atkPhase && g_cultAtkOK) ? g_cultAtk.rot[0] : g_cultWalk[walkFrame].rot[0];
        }
        else if (t == 5 && g_mutOK) {
            walkTex  = g_mutWalk[walkFrame].rot[slot];
            walkFlip = flip;
            // Mutant: cycle E (charge) ↔ G (fire) — both real attack frames.
            atkTex   = g_mutAtkOK
                       ? (atkPhase ? g_mutAtkFire.rot[0] : g_mutAtkCharge.rot[0])
                       : g_mutWalk[walkFrame].rot[0];
        }
        else if (t == 6 && g_mechOK) {
            walkTex  = g_mechWalk[walkFrame].rot[slot];
            walkFlip = flip;
            // Mech: cycle F (left arm fire) ↔ G (right arm fire) so it reads
            // as a sustained barrage, matching its in-game attack pattern.
            atkTex   = g_mechFireOK
                       ? (atkPhase ? g_mechFireR.rot[0] : g_mechFireL.rot[0])
                       : g_mechWalk[walkFrame].rot[0];
        }
        // PreviewEnemy slots 7..12 — DOOM-style + Beautiful-Doom imports.
        // Walk frame count is per-enemy (whatever walk_N.png files exist),
        // so the modulo is dynamic.
        else if (t >= 7 && t <= 12) {
            PreviewEnemy *pe = (t == 7)  ? &g_prevSoldier
                              : (t == 8)  ? &g_prevCaco
                              : (t == 9)  ? &g_prevCyber
                              : (t == 10) ? &g_prevRevenant
                              : (t == 11) ? &g_prevLostSoul
                                          : &g_prevPainElem;
            if (pe->ok) {
                int idx = (int)(g_pickerT * 5.f) % pe->walkCount;
                walkTex = pe->walk[idx];
                if (atkPhase && pe->atkCount > 0) {
                    int ai = (int)(g_pickerT * 8.f) % pe->atkCount;
                    atkTex = pe->atk[ai];
                } else {
                    atkTex = pe->walk[idx];
                }
            }
        }

        // Big walk preview centred-left — flip horizontally when showing
        // a mirrored rotation so the spin reads correctly all the way round.
        const float previewH = 320.f;
        if (walkTex.id) {
            float aspect = (float)walkTex.width / (float)walkTex.height;
            float pw = previewH * aspect, ph = previewH;
            float px = sw2*0.30f - pw*0.5f;
            float py = sh2*0.45f - ph*0.5f;
            Rectangle src = {0, 0, (float)walkTex.width, (float)walkTex.height};
            if (walkFlip) src.width = -src.width;
            DrawTexturePro(walkTex, src,
                (Rectangle){px, py, pw, ph},
                (Vector2){0,0}, 0.f, WHITE);
        }
        // Attack frame on the right
        const float atkH = 240.f;
        if (atkTex.id) {
            float aspect = (float)atkTex.width / (float)atkTex.height;
            float aw = atkH * aspect, ah = atkH;
            float ax = sw2*0.70f - aw*0.5f;
            float ay = sh2*0.45f - ah*0.5f;
            DrawText("ATTACK", (int)(sw2*0.70f) - MeasureText("ATTACK",16)/2, (int)(ay - 22), 16, (Color){180,180,180,255});
            DrawRectangle((int)(ax-6), (int)(ay-6), (int)(aw+12), (int)(ah+12), (Color){20,16,28,200});
            DrawTexturePro(atkTex,
                (Rectangle){0,0,(float)atkTex.width,(float)atkTex.height},
                (Rectangle){ax, ay, aw, ah},
                (Vector2){0,0}, 0.f, WHITE);
        } else {
            DrawText("(no attack frame)", (int)(sw2*0.70f) - 70, sh2/2 - 8, 14, (Color){120,120,120,255});
        }

        // Selected name + blurb
        const char *name = enemyNamesExt[t];
        DrawText(name, sw2/2 - MeasureText(name, 56)/2, (int)(sh2*0.45f + previewH*0.5f + 24), 56, (Color){255,220,90,255});
        const char *blurb = enemyBlurb[t];
        DrawText(blurb, sw2/2 - MeasureText(blurb, 18)/2, (int)(sh2*0.45f + previewH*0.5f + 90), 18, (Color){200,200,200,220});

        // Selector dots — 7 markers along bottom, current one highlighted
        for (int i = 0; i < 10; i++) {
            int cx = sw2/2 - (10*22)/2 + i*22 + 11;
            int cy = sh2 - 80;
            Color c = (i == t) ? (Color){255,220,90,255} : (Color){90,90,100,255};
            DrawCircle(cx, cy, (i == t) ? 8 : 5, c);
        }

        // Hints
        const char *hint1 = "<- / ->  OR  1-7 TO PICK";
        const char *hint2 = "ENTER / SPACE / CLICK TO START   |   ESC TO BACK";
        DrawText(hint1, sw2/2 - MeasureText(hint1,16)/2, sh2 - 56, 16, (Color){180,180,180,255});
        DrawText(hint2, sw2/2 - MeasureText(hint2,14)/2, sh2 - 30, 14, (Color){140,140,140,255});
        DrawFPS(10,10);
    } else if (g_gs==GS_DEAD) {
        int sw2=GetScreenWidth(),sh2=GetScreenHeight();
        DrawRectangle(0,0,sw2,sh2,(Color){100,0,0,130});
        const char *d="YOU DIED";
        DrawText(d,sw2/2-MeasureText(d,88)/2+4,sh2/3+4,88,(Color){80,0,0,255});
        DrawText(d,sw2/2-MeasureText(d,88)/2,sh2/3,88,RED);
        char sc2[80];
        int mins = (int)(g_statTime / 60.f);
        int secs = (int)g_statTime % 60;
        snprintf(sc2,80,"SCORE %d   WAVE %d   KILLS %d   %d:%02d",
                 g_p.score, g_wave, g_p.kills, mins, secs);
        DrawText(sc2,sw2/2-MeasureText(sc2,22)/2,sh2/3+100,22,YELLOW);

        if (!g_initialsDone) {
            const char *prompt = "ENTER YOUR INITIALS";
            DrawText(prompt, sw2/2 - MeasureText(prompt, 28)/2, sh2/2 + 10, 28, WHITE);
            // Three big char boxes; highlighted slot has a yellow border.
            const int cellW = 64, cellH = 84, gap = 24;
            const int total = 3*cellW + 2*gap;
            int sx = sw2/2 - total/2;
            int by = sh2/2 + 50;
            for (int i = 0; i < 3; i++) {
                int bx = sx + i*(cellW+gap);
                Color box = (i == g_initialsPos) ? (Color){255,220,80,255}
                                                 : (Color){120,120,130,255};
                DrawRectangleLines(bx, by, cellW, cellH, box);
                DrawRectangleLines(bx-1, by-1, cellW+2, cellH+2, box);
                char ch[2] = {g_initials[i], 0};
                int tw = MeasureText(ch, 64);
                DrawText(ch, bx + cellW/2 - tw/2, by + 8, 64, WHITE);
            }
            const char *h1 = "UP/DOWN cycle    LEFT/RIGHT pick slot    A-Z 0-9 to type";
            DrawText(h1, sw2/2 - MeasureText(h1, 16)/2, by + cellH + 14, 16, (Color){200,200,210,230});
            const char *h2 = "ENTER submit    ESC skip";
            DrawText(h2, sw2/2 - MeasureText(h2, 18)/2, by + cellH + 40, 18, (Color){180,255,180,240});
        } else {
            const char *status;
            Color statusCol;
            if (g_cheated) {
                status = "CHEATS USED  -  NO LEADERBOARD ENTRY";
                statusCol = (Color){255, 100, 100, 255};
            } else if (g_initialsSubmitted) {
                status = "SCORE SUBMITTED";
                statusCol = (Color){80, 255, 120, 255};
            } else {
                status = "SCORE NOT SUBMITTED";
                statusCol = (Color){200, 200, 200, 200};
            }
            DrawText(status, sw2/2 - MeasureText(status, 22)/2, sh2/2 + 30, 22, statusCol);
            if (g_initialsSubmitted) {
                // Rank arrives a frame or two after the POST — show a
                // placeholder while we wait, then the actual rank when
                // the server response lands.
                char rankStr[32];
                if (g_lastRank > 0) snprintf(rankStr, sizeof(rankStr), "PLACED #%d", g_lastRank);
                else                snprintf(rankStr, sizeof(rankStr), "...");
                Color rankCol = (g_lastRank > 0) ? (Color){255, 220, 80, 255}
                                                 : (Color){160, 160, 160, 200};
                DrawText(rankStr, sw2/2 - MeasureText(rankStr, 32)/2, sh2/2 + 60, 32, rankCol);
                const char *board = "view leaderboard at  ironfist.ximg.app/scores.html";
                DrawText(board, sw2/2 - MeasureText(board, 18)/2, sh2/2 + 100, 18,
                         (Color){180, 220, 255, 230});
            }
            if (sinf(GetTime()*3.f)>0)
                DrawText("[ ENTER / SPACE  TO  PLAY  AGAIN ]",
                         sw2/2 - MeasureText("[ ENTER / SPACE  TO  PLAY  AGAIN ]", 20)/2,
                         sh2*2/3, 20, WHITE);
        }
    }
    if (g_gs==GS_PLAY) DrawFPS(GetScreenWidth()-90, 10);
    // God-mode + cheat overlay strip — corner indicator while cheats are
    // active so the player always knows their run won't earn a leaderboard
    // entry. Drawn over the HUD; under the console panel.
    if (g_gs == GS_PLAY && (g_god || g_cheated)) {
        char ind[64];
        snprintf(ind, sizeof(ind), "%s%s%s",
                 g_god ? "GOD" : "",
                 (g_god && g_cheated) ? " | " : "",
                 (!g_god && g_cheated) ? "CHEAT" : (g_god && g_cheated) ? "CHEAT" : "");
        DrawText(ind, 10, GetScreenHeight() - 26, 18, (Color){255, 100, 100, 230});
    }
    // Dev console panel goes ON TOP of the HUD so it's always readable.
    // Draw while open OR while the close animation is still playing out.
    if (g_conOpen || g_conAnim > 0.f) ConDraw();
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
    // Disable raylib's default "ESC closes window" behaviour. We want ESC
    // to navigate back through screens (in-game / picker / browser → main
    // menu) and only quit when pressed FROM the main menu. Routing all
    // ESC handling per-state in StepFrame.
    SetExitKey(KEY_NULL);
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

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/monster-kill.mp3", AppDir());
        g_sMonster = LoadSound(fp);
        g_sMonsterOK = (g_sMonster.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/first-blood.mp3", AppDir());
        g_sFirstBlood = LoadSound(fp);
        g_sFirstBloodOK = (g_sFirstBlood.frameCount > 0);

        static const char *ENEMY_ALERT_FILES[] = {
            "sounds/distant-enemy.mp3",
            "sounds/bombin-alert.mp3",
            "sounds/scary-alert.mp3",
            "sounds/alien-scream.mp3",
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

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/tesla.ogg", AppDir());
        g_sTesla = LoadSound(fp);
        g_sTeslaOK = (g_sTesla.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/mech-rocket.mp3", AppDir());
        g_sMechRocket = LoadSound(fp);
        g_sMechRocketOK = (g_sMechRocket.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/mutant-attack.mp3", AppDir());
        g_sMutAttack = LoadSound(fp);
        g_sMutAttackOK = (g_sMutAttack.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/player-ouch.mp3", AppDir());
        g_sChefHit = LoadSound(fp);
        g_sChefHitOK = (g_sChefHit.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/low-health.mp3", AppDir());
        g_sLowHealth = LoadSound(fp);
        g_sLowHealthOK = (g_sLowHealth.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/soldier-mg.mp3", AppDir());
        g_sSoldierMG = LoadSound(fp);
        g_sSoldierMGOK = (g_sSoldierMG.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/ss-fire.mp3", AppDir());
        g_sSSFire = LoadSound(fp);
        g_sSSFireOK = (g_sSSFire.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/cyber-fire.mp3", AppDir());
        g_sCyberFire = LoadSound(fp);
        g_sCyberFireOK = (g_sCyberFire.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/first-blood-wave.mp3", AppDir());
        g_sFirstBloodWave = LoadSound(fp);
        g_sFirstBloodWaveOK = (g_sFirstBloodWave.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/idle-sigh.mp3", AppDir());
        g_sIdleSigh = LoadSound(fp);
        g_sIdleSighOK = (g_sIdleSigh.frameCount > 0);

        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/player-ough.mp3", AppDir());
        g_sMechHit = LoadSound(fp);
        g_sMechHitOK = (g_sMechHit.frameCount > 0);

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

        // Real player-hurt grunt overrides the procedural g_sHurt
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/player-hurt.mp3", AppDir());
        Sound realHurt = LoadSound(fp);
        if (realHurt.frameCount > 0) {
            UnloadSound(g_sHurt);
            g_sHurt = realHurt;
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

        // Streamed background music — alternating playlist (Hell March,
        // then Funeral March of Queen Mary, then back). Both tracks load
        // at startup so swapping between them at end-of-track is just a
        // PlayMusicStream call, no disk hit mid-game.
        //
        // Title music is a separate looping stream that plays only on
        // GS_MENU; the state-transition handler in StepFrame swaps which
        // stream is active. We only PLAY the title track here at startup
        // since the game launches in GS_MENU; the in-game playlist starts
        // when the player kicks off a run.
        LoadMusicVol();  // restore user's last-saved volume from ~/.ironfist3d.cfg
        g_musicOK = false;
        for (int t = 0; t < MUSIC_TRACK_COUNT; t++) {
            snprintf(fp, sizeof(fp), "%s" RES_PREFIX "%s", AppDir(), g_musicFiles[t]);
            g_musicTracks[t] = LoadMusicStream(fp);
            g_musicTracksOK[t] = (g_musicTracks[t].frameCount > 0);
            if (g_musicTracksOK[t]) {
                g_musicTracks[t].looping = false;
                SetMusicVolume(g_musicTracks[t], g_musicVol);
                g_musicOK = true;
            }
        }
        g_musicIdx = 0;
        for (int t = 0; t < MUSIC_TRACK_COUNT; t++) {
            if (g_musicTracksOK[t]) { g_musicIdx = t; break; }
        }
        // Title music — looping atmospheric track for the menu.
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sounds/title-music.mp3", AppDir());
        g_titleMusic = LoadMusicStream(fp);
        g_titleMusicOK = (g_titleMusic.frameCount > 0);
        if (g_titleMusicOK) {
            g_titleMusic.looping = true;
            SetMusicVolume(g_titleMusic, g_musicVol);
        }
        // Game launches in GS_MENU — start title music if available, else
        // fall back to the in-game playlist so something is always playing.
        if (g_titleMusicOK)      PlayMusicStream(g_titleMusic);
        else if (g_musicOK)      PlayMusicStream(g_musicTracks[g_musicIdx]);
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
            // 3 TESLA: H idle (dormant), B/C/D charge-up, E/F/G discharge.
            // Frame layout matches the windup phases in DrawSpriteWeapon:
            //   [0]=H (no fire)  [1..3]=B,C,D (charging)  [4..6]=E,F,G (discharging)
            { 3, "tesla",        {"PULSH0.png","PULSB0.png","PULSC0.png","PULSD0.png","PULSE0.png","PULSF0.png","PULSG0.png"}, 7, 3.5f },
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
        g_wep[3].xShift =  0.0f;   // TESLA:  centred (sprite is symmetric)
        g_wep[3].yShift = -0.02f;

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
            // Tesla world pickup (PULSA0): the small purple-cannon icon
            snprintf(fp, sizeof(fp), "%spickups/PULSA0.png", appBase);
            g_teslaPickupTex = LoadTexture(fp);
            if (g_teslaPickupTex.id) {
                SetTextureFilter(g_teslaPickupTex, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_teslaPickupTex, TEXTURE_WRAP_CLAMP);
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

        InitHUD(appBase);  // Doom mugshot etc — implemented in hud.c

        // DOOM-style + Beautiful-Doom enemies — types 7..12. Each folder has
        // walk_N / atk_N / pain_N / death_N PNG sequences; load as many as
        // exist (capped per array). Same sprites back the arena picker
        // preview AND the in-game render.
        //   7  soldier      8  caco        9  cyber
        //   10 revenant    11 lostsoul   12 painelem
        {
            char fp[700];
            const char *folders[6] = {"soldier", "caco", "cyber", "revenant", "lostsoul", "painelem"};
            PreviewEnemy *previews[6] = {&g_prevSoldier, &g_prevCaco, &g_prevCyber,
                                         &g_prevRevenant, &g_prevLostSoul, &g_prevPainElem};
            for (int p = 0; p < 6; p++) {
                PreviewEnemy *pe = previews[p];
                pe->walkCount = pe->atkCount = pe->painCount = pe->deathCount = 0;
                pe->ok = false;
                struct { Texture2D *arr; int *count; int max; const char *prefix; } sets[4] = {
                    { pe->walk,  &pe->walkCount,  PREV_WALK_MAX,  "walk"  },
                    { pe->atk,   &pe->atkCount,   PREV_ATK_MAX,   "atk"   },
                    { pe->pain,  &pe->painCount,  PREV_PAIN_MAX,  "pain"  },
                    { pe->death, &pe->deathCount, PREV_DEATH_MAX, "death" },
                };
                for (int s = 0; s < 4; s++) {
                    for (int i = 0; i < sets[s].max; i++) {
                        snprintf(fp, sizeof(fp), "%smonsters/preview/%s/%s_%d.png",
                                 appBase, folders[p], sets[s].prefix, i);
                        Texture2D tx = LoadTexture(fp);
                        if (tx.id == 0) break;
                        SetTextureFilter(tx, TEXTURE_FILTER_POINT);
                        SetTextureWrap  (tx, TEXTURE_WRAP_CLAMP);
                        sets[s].arr[(*sets[s].count)++] = tx;
                    }
                }
                pe->ok = (pe->walkCount > 0);
            }
        }

        // Beautiful-Doom YBL7 animated blood-splat (19 frames A..S, ~15 fps).
        {
            char fp[700];
            g_bloodSplatOK = true;
            for (int i = 0; i < BLOOD_SPLAT_FRAMES; i++) {
                snprintf(fp, sizeof(fp), "%sblood/YBL7%c0.png", appBase, 'A' + i);
                g_bloodSplatTex[i] = LoadTexture(fp);
                if (g_bloodSplatTex[i].id == 0) { g_bloodSplatOK = false; continue; }
                SetTextureFilter(g_bloodSplatTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_bloodSplatTex[i], TEXTURE_WRAP_CLAMP);
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
            snprintf(fp, sizeof(fp), "%smonsters/AFABE0.png", appBase);
            g_chefAtkTex = LoadTexture(fp);
            if (g_chefAtkTex.id) {
                SetTextureFilter(g_chefAtkTex, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_chefAtkTex, TEXTURE_WRAP_CLAMP);
                g_chefAtkOK = true;
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
            snprintf(fp, sizeof(fp), "%smonsters/TORME0.png", appBase);
            g_tormAtkTex = LoadTexture(fp);
            if (g_tormAtkTex.id) {
                SetTextureFilter(g_tormAtkTex, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_tormAtkTex, TEXTURE_WRAP_CLAMP);
                g_tormAtkOK = true;
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
            snprintf(fp, sizeof(fp), "%smonsters/SCH2E0.png", appBase);
            g_schAtkTex = LoadTexture(fp);
            if (g_schAtkTex.id) {
                SetTextureFilter(g_schAtkTex, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_schAtkTex, TEXTURE_WRAP_CLAMP);
                g_schAtkOK = true;
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
            // Attack frame (cleaver mid-swing) — flashes during ATTACK windup
            snprintf(fp, sizeof(fp), "%smonsters/BTCNE0.png", appBase);
            g_bossAtkTex = LoadTexture(fp);
            if (g_bossAtkTex.id) {
                SetTextureFilter(g_bossAtkTex, TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_bossAtkTex, TEXTURE_WRAP_CLAMP);
                g_bossAtkOK = true;
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

        // CULTIST slot (type 4) — swapped from SSCT occult sprites to PARA*
        // Wafen-SS officer (nazis_ss/) so the firing animation actually
        // matches the gameplay. PARA uses individual files per rotation
        // (PARA<L><1..8>.png), so we load the front + 4 unique rotations
        // and flip 6/7/8 at draw time. State letters per the WolfenDoom
        // DECORATE: A-D walk, E aim, F pre-fire, G fire (muzzle flash),
        // H pain, I-M death sequence (single rotation).
        {
            char fp[700];
            #define LOAD_SS_FRAME(letter, dfPtr) do {                                    \
                DirFrame *df = (dfPtr); df->ok = false;                                  \
                bool ok = true;                                                          \
                for (int _r = 0; _r < 5; _r++) {                                         \
                    snprintf(fp, sizeof(fp), "%smonsters/nazis_ss/PARA%c%d.png",         \
                             appBase, (letter), _r + 1);                                 \
                    df->rot[_r] = LoadTexture(fp);                                       \
                    if (df->rot[_r].id == 0) { ok = false; continue; }                   \
                    SetTextureFilter(df->rot[_r], TEXTURE_FILTER_POINT);                 \
                    SetTextureWrap  (df->rot[_r], TEXTURE_WRAP_CLAMP);                   \
                }                                                                        \
                df->ok = ok;                                                             \
            } while (0)

            const char letters[4] = {'A','B','C','D'};
            g_cultOK = true;
            for (int i = 0; i < 4; i++) {
                LOAD_SS_FRAME(letters[i], &g_cultWalk[i]);
                if (!g_cultWalk[i].ok) g_cultOK = false;
            }
            LOAD_SS_FRAME('H', &g_cultPain);   // SS pain frame (was 'E' for SSCT)
            g_cultPainOK = g_cultPain.ok;
            LOAD_SS_FRAME('G', &g_cultAtk);    // firing pose with muzzle flash
            g_cultAtkOK = g_cultAtk.ok;

            #undef LOAD_SS_FRAME

            // Death — single rotation, frames I0..M0.
            const char *dnames[5] = {"PARAI0.png","PARAJ0.png","PARAK0.png","PARAL0.png","PARAM0.png"};
            g_cultDeathOK = true;
            for (int i = 0; i < 5; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/nazis_ss/%s", appBase, dnames[i]);
                g_cultDeathTex[i] = LoadTexture(fp);
                if (g_cultDeathTex[i].id == 0) { g_cultDeathOK = false; continue; }
                SetTextureFilter(g_cultDeathTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_cultDeathTex[i], TEXTURE_WRAP_CLAMP);
            }
        }

        // RANGED MUTANT (type 5) — MTNT* WolfenDoom mutant_range sprites.
        // Files are individually present per rotation (MTNTA1.png .. MTNTA8.png)
        // — different from cultist's mirror-pair filenames. We still use only
        // 5 unique rotations and flip 6/7/8 at draw time, so we copy A1..A5 etc.
        {
            char fp[700];
            #define LOAD_MUT_FRAME(letter, dfPtr) do {                                  \
                DirFrame *df = (dfPtr); df->ok = false;                                 \
                bool ok = true;                                                         \
                for (int _r = 0; _r < 5; _r++) {                                        \
                    snprintf(fp, sizeof(fp), "%smonsters/mutant_range/MTNT%c%d.png",    \
                             appBase, letter, _r + 1);                                  \
                    df->rot[_r] = LoadTexture(fp);                                      \
                    if (df->rot[_r].id == 0) { ok = false; continue; }                  \
                    SetTextureFilter(df->rot[_r], TEXTURE_FILTER_POINT);                \
                    SetTextureWrap  (df->rot[_r], TEXTURE_WRAP_CLAMP);                  \
                }                                                                       \
                df->ok = ok;                                                            \
            } while (0)

            const char mletters[4] = {'A','B','C','D'};
            g_mutOK = true;
            for (int i = 0; i < 4; i++) {
                LOAD_MUT_FRAME(mletters[i], &g_mutWalk[i]);
                if (!g_mutWalk[i].ok) g_mutOK = false;
            }
            LOAD_MUT_FRAME('F', &g_mutPain);     g_mutPainOK = g_mutPain.ok;
            LOAD_MUT_FRAME('E', &g_mutAtkCharge);
            LOAD_MUT_FRAME('G', &g_mutAtkFire);
            g_mutAtkOK = g_mutAtkCharge.ok && g_mutAtkFire.ok;

            #undef LOAD_MUT_FRAME

            // Death — single rotation, frames J0..M0.
            const char *mdn[4] = {"MTNTJ0.png","MTNTK0.png","MTNTL0.png","MTNTM0.png"};
            g_mutDeathOK = true;
            for (int i = 0; i < 4; i++) {
                snprintf(fp, sizeof(fp), "%smonsters/mutant_range/%s", appBase, mdn[i]);
                g_mutDeathTex[i] = LoadTexture(fp);
                if (g_mutDeathTex[i].id == 0) { g_mutDeathOK = false; continue; }
                SetTextureFilter(g_mutDeathTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_mutDeathTex[i], TEXTURE_WRAP_CLAMP);
            }
        }

        // HEAVY MECH (type 6) — MAVY* sprites in nazis_mechas/. Same per-rotation
        // file convention as the mutant (MAVYx1..MAVYx5, mirrors handled in code).
        {
            char fp[700];
            #define LOAD_MECH_FRAME(letter, dfPtr) do {                                  \
                DirFrame *df = (dfPtr); df->ok = false;                                  \
                bool ok = true;                                                          \
                for (int _r = 0; _r < 5; _r++) {                                         \
                    snprintf(fp, sizeof(fp), "%smonsters/nazis_mechas/MAVY%c%d.png",     \
                             appBase, letter, _r + 1);                                   \
                    df->rot[_r] = LoadTexture(fp);                                       \
                    if (df->rot[_r].id == 0) { ok = false; continue; }                   \
                    SetTextureFilter(df->rot[_r], TEXTURE_FILTER_POINT);                 \
                    SetTextureWrap  (df->rot[_r], TEXTURE_WRAP_CLAMP);                   \
                }                                                                        \
                df->ok = ok;                                                             \
            } while (0)

            LOAD_MECH_FRAME('N', &g_mechIdle);   g_mechIdleOK = g_mechIdle.ok;
            const char mletters[4] = {'A','B','C','D'};
            g_mechOK = true;
            for (int i = 0; i < 4; i++) {
                LOAD_MECH_FRAME(mletters[i], &g_mechWalk[i]);
                if (!g_mechWalk[i].ok) g_mechOK = false;
            }
            LOAD_MECH_FRAME('F', &g_mechFireL);
            LOAD_MECH_FRAME('G', &g_mechFireR);
            g_mechFireOK = g_mechFireL.ok && g_mechFireR.ok;
            #undef LOAD_MECH_FRAME
        }

        // Flaming barrel scenery — Freedoom 3-frame fire cycle.
        {
            char fp[700];
            const char *bnames[3] = {"fcana0.png","fcanb0.png","fcanc0.png"};
            g_barrelOK = true;
            for (int i = 0; i < 3; i++) {
                snprintf(fp, sizeof(fp), "%sscenery/%s", appBase, bnames[i]);
                g_barrelTex[i] = LoadTexture(fp);
                if (g_barrelTex[i].id == 0) { g_barrelOK = false; continue; }
                SetTextureFilter(g_barrelTex[i], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_barrelTex[i], TEXTURE_WRAP_CLAMP);
            }
        }
    }

    // Set ambient
    float amb[4]={0.40f,0.32f,0.46f,1.f};
    SetShaderValue(g_shader,u_ambient,amb,SHADER_UNIFORM_VEC4);

    // Wall textures: load all 6 candidates (BRIK_B01 + the five DOOM-style-Game
    // wall1..5). Each cell is hashed into one of these slots in BuildWallMesh,
    // so the level uses a mix of textures every run instead of a single
    // repeating brick. tWalls[i] is the texture for wall slot i; tBrick aliases
    // the first valid one and is reused for platforms.
    Texture2D tWalls[WALL_TEX_COUNT] = {0};
    Texture2D tBrick = {0};
    {
        char fp[700];
        const char *walls[WALL_TEX_COUNT] = {
            "sprites/textures/BRIK_B01.png",
            "sprites/textures/wall1.png",
            "sprites/textures/wall2.png",
            "sprites/textures/wall3.png",
            "sprites/textures/wall4.png",
            "sprites/textures/wall5.png",
        };
        for (int i = 0; i < WALL_TEX_COUNT; i++) {
            snprintf(fp, sizeof(fp), "%s" RES_PREFIX "%s", AppDir(), walls[i]);
            tWalls[i] = LoadTexture(fp);
            if (tWalls[i].id) {
                GenTextureMipmaps(&tWalls[i]);
                SetTextureFilter(tWalls[i], TEXTURE_FILTER_TRILINEAR);
                SetTextureWrap  (tWalls[i], TEXTURE_WRAP_REPEAT);
                if (tBrick.id == 0) tBrick = tWalls[i];
            }
        }
        // Last-resort fallback: procedural brick fills all slots that failed.
        if (tBrick.id == 0) {
            tBrick = MkBrick();
            for (int i = 0; i < WALL_TEX_COUNT; i++) tWalls[i] = tBrick;
        } else {
            for (int i = 0; i < WALL_TEX_COUNT; i++) {
                if (tWalls[i].id == 0) tWalls[i] = tBrick;
            }
        }
    }
    Texture2D tFloor = MkFloor();
    // Ceiling: prefer DOOM-style-Game's sky.png panoramic if available, else
    // fall back to the procedural starfield/ceiling texture.
    Texture2D tCeil;
    {
        char fp[700];
        snprintf(fp, sizeof(fp), "%s" RES_PREFIX "sprites/textures/sky.png", AppDir());
        tCeil = LoadTexture(fp);
        if (tCeil.id == 0) {
            tCeil = MkCeil();
        } else {
            GenTextureMipmaps(&tCeil);
            SetTextureFilter(tCeil, TEXTURE_FILTER_TRILINEAR);
            SetTextureWrap  (tCeil, TEXTURE_WRAP_REPEAT);
        }
    }

    ComputeWallSlots();
    for (int i = 0; i < WALL_TEX_COUNT; i++) {
        Mesh wm = BuildWallMesh(i, WALL_TEX_COUNT);
        g_wallModels[i] = MakeShaderModel(wm, tWalls[i]);
    }
    Mesh fm = BuildPlaneMesh(0.f,  (float)COLS*CELL/4.f, (float)ROWS*CELL/4.f, false);
    g_floorModel = MakeShaderModel(fm, tFloor);
    // Ceiling: UV span = 1×1 so the panoramic sky texture stretches once
    // across the whole ceiling instead of tiling across the map.
    Mesh cm = BuildPlaneMesh(WALL_H, 1.f, 1.f, true);
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

    while (!WindowShouldClose() && !g_quit) StepFrame();

    for (int i = 0; i < WALL_TEX_COUNT; i++) UnloadModel(g_wallModels[i]);
    UnloadModel(g_floorModel); UnloadModel(g_ceilModel);
    UnloadModel(g_platModel);
    UnloadTexture(tBrick); UnloadTexture(tFloor); UnloadTexture(tCeil);
    for (int w=0;w<4;w++) {
        for (int i=0;i<g_wep[w].count;i++)
            if (g_wep[w].frames[i].id) UnloadTexture(g_wep[w].frames[i]);
        if (g_wep[w].hasFlash && g_wep[w].flash.id) UnloadTexture(g_wep[w].flash);
        if (g_xhair[w].id) UnloadTexture(g_xhair[w]);
    }
    for (int i=0;i<2;i++) if (g_healthTex[i].id) UnloadTexture(g_healthTex[i]);
    for (int i=0;i<5;i++) if (g_ammoTex[i].id)   UnloadTexture(g_ammoTex[i]);
    if (g_teslaPickupTex.id) UnloadTexture(g_teslaPickupTex);
    ShutdownHUD();
    for (int i=0;i<BLOOD_SPLAT_FRAMES;i++) if (g_bloodSplatTex[i].id) UnloadTexture(g_bloodSplatTex[i]);
    for (int i=0;i<4;i++) if (g_chefTex[i].id)      UnloadTexture(g_chefTex[i]);
    for (int i=0;i<4;i++) if (g_chefDeathTex[i].id) UnloadTexture(g_chefDeathTex[i]);
    if (g_chefPainTex.id) UnloadTexture(g_chefPainTex);
    if (g_chefAtkTex.id)  UnloadTexture(g_chefAtkTex);
    for (int i=0;i<4;i++) if (g_tormTex[i].id)      UnloadTexture(g_tormTex[i]);
    for (int i=0;i<4;i++) if (g_tormDeathTex[i].id) UnloadTexture(g_tormDeathTex[i]);
    if (g_tormPainTex.id) UnloadTexture(g_tormPainTex);
    if (g_tormAtkTex.id)  UnloadTexture(g_tormAtkTex);
    for (int i=0;i<4;i++) if (g_schTex[i].id)       UnloadTexture(g_schTex[i]);
    for (int i=0;i<4;i++) if (g_schDeathTex[i].id)  UnloadTexture(g_schDeathTex[i]);
    if (g_schPainTex.id)  UnloadTexture(g_schPainTex);
    if (g_schAtkTex.id)   UnloadTexture(g_schAtkTex);
    for (int i=0;i<4;i++)
        for (int r=0;r<5;r++) if (g_mutWalk[i].rot[r].id) UnloadTexture(g_mutWalk[i].rot[r]);
    for (int r=0;r<5;r++) if (g_mutPain.rot[r].id)        UnloadTexture(g_mutPain.rot[r]);
    for (int r=0;r<5;r++) if (g_mutAtkCharge.rot[r].id)   UnloadTexture(g_mutAtkCharge.rot[r]);
    for (int r=0;r<5;r++) if (g_mutAtkFire.rot[r].id)     UnloadTexture(g_mutAtkFire.rot[r]);
    for (int i=0;i<4;i++) if (g_mutDeathTex[i].id)        UnloadTexture(g_mutDeathTex[i]);
    for (int r=0;r<5;r++) if (g_mechIdle.rot[r].id)       UnloadTexture(g_mechIdle.rot[r]);
    for (int i=0;i<4;i++)
        for (int r=0;r<5;r++) if (g_mechWalk[i].rot[r].id) UnloadTexture(g_mechWalk[i].rot[r]);
    for (int r=0;r<5;r++) if (g_mechFireL.rot[r].id)      UnloadTexture(g_mechFireL.rot[r]);
    for (int r=0;r<5;r++) if (g_mechFireR.rot[r].id)      UnloadTexture(g_mechFireR.rot[r]);
    for (int i=0;i<4;i++) if (g_bossTex[i].id)      UnloadTexture(g_bossTex[i]);
    for (int i=0;i<5;i++) if (g_bossDeathTex[i].id) UnloadTexture(g_bossDeathTex[i]);
    if (g_bossPainTex.id) UnloadTexture(g_bossPainTex);
    if (g_bossAtkTex.id)  UnloadTexture(g_bossAtkTex);
    UnloadShader(g_shader);
    UnloadSound(g_sPistol); UnloadSound(g_sShotgun); UnloadSound(g_sRocket);
    UnloadSound(g_sExplode); UnloadSound(g_sHurt); UnloadSound(g_sPickup);
    UnloadSound(g_sEmpty); UnloadSound(g_sDie);
    for (int i=0;i<CHEF_DIE_ALT_COUNT;i++) if (g_sDieAltOK[i]) UnloadSound(g_sDieAlt[i]);
    if (g_sHeadshotOK) UnloadSound(g_sHeadshot);
    if (g_sFatalityOK) UnloadSound(g_sFatality);
    if (g_sMultiOK)      UnloadSound(g_sMulti);
    if (g_sMonsterOK)    UnloadSound(g_sMonster);
    if (g_sFirstBloodOK)   UnloadSound(g_sFirstBlood);
    for (int a = 0; a < g_sEnemyAlertCount; a++) {
        if (g_sEnemyAlertOK[a]) UnloadSound(g_sEnemyAlert[a]);
    }
    if (g_sShotgunKillOK)  UnloadSound(g_sShotgunKill);
    if (g_sTeslaOK)        UnloadSound(g_sTesla);
    if (g_sMechRocketOK)   UnloadSound(g_sMechRocket);
    if (g_sMutAttackOK)    UnloadSound(g_sMutAttack);
    if (g_sChefHitOK)      UnloadSound(g_sChefHit);
    if (g_sLowHealthOK)    UnloadSound(g_sLowHealth);
    if (g_sSoldierMGOK)    UnloadSound(g_sSoldierMG);
    if (g_sSSFireOK)       UnloadSound(g_sSSFire);
    if (g_sCyberFireOK)    UnloadSound(g_sCyberFire);
    if (g_sIdleSighOK)     UnloadSound(g_sIdleSigh);
    if (g_sFirstBloodWaveOK) UnloadSound(g_sFirstBloodWave);
    if (g_titleMusicOK) {
        StopMusicStream(g_titleMusic);
        UnloadMusicStream(g_titleMusic);
    }
    if (g_sMechHitOK)      UnloadSound(g_sMechHit);
    if (g_sMGOK)           UnloadSound(g_sMG);
    if (g_sNextWaveOK)     UnloadSound(g_sNextWave);
    if (g_sHealthPickupOK) UnloadSound(g_sHealthPickup);
    if (g_sMGPickupOK)     UnloadSound(g_sMGPickup);
    if (g_sOneLeftOK)      UnloadSound(g_sOneLeft);
    for (int t = 0; t < MUSIC_TRACK_COUNT; t++) {
        if (g_musicTracksOK[t]) {
            StopMusicStream(g_musicTracks[t]);
            UnloadMusicStream(g_musicTracks[t]);
        }
    }
    if (g_dbgLog) { fclose(g_dbgLog); g_dbgLog = NULL; }
    CloseAudioDevice(); CloseWindow();
    return 0;
}
