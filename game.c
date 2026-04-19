// IRON FIST 3D - raylib native FPS
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
static const char *VS = "#version 330 core\n"
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

static const char *FS =
"#version 330 core\n"
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
    LightDef scene[6] = {
        {{ 5*CELL, WALL_H*0.8f,  2*CELL}, {1.0f, 0.65f, 0.2f},  40.f, 1},
        {{14*CELL, WALL_H*0.8f,  9*CELL}, {0.2f, 0.5f,  1.0f},  50.f, 1},
        {{24*CELL, WALL_H*0.8f,  5*CELL}, {1.0f, 0.15f, 0.1f},  40.f, 1},
        {{ 5*CELL, WALL_H*0.8f, 16*CELL}, {0.15f,1.0f,  0.3f},  40.f, 1},
        {{24*CELL, WALL_H*0.8f, 16*CELL}, {0.9f, 0.3f,  1.0f},  40.f, 1},
        {{14*CELL, WALL_H*0.8f, 17*CELL}, {1.0f, 0.8f,  0.3f},  40.f, 1},
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
} Enemy;

typedef struct { Vector3 pos, vel; float life, dmg; bool active, rocket; } Bullet;
typedef struct { Vector3 pos, vel; float life, maxLife, size; Color col; bool active, grav; } Part;
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

static Sound    g_sPistol, g_sShotgun, g_sRocket, g_sExplode;
static Sound    g_sHurt, g_sPickup, g_sEmpty, g_sDie;

// enemy stat tables
static const float ET_HP[]    = {65,145,42};
static const float ET_SPD[]   = {4.0f,2.3f,6.2f};
static const float ET_DMG[]   = {10,24,8};
static const float ET_RATE[]  = {1.5f,2.1f,1.0f};
static const float ET_AR[]    = {24,20,30};
static const float ET_ATK[]   = {3.6f,3.1f,4.2f};
static const int   ET_SC[]    = {100,300,160};
static const Color ET_COL[]   = {{60,160,55,255},{140,55,185,255},{40,110,210,255}};
static const Color ET_EYE[]   = {{255,30,20,255},{255,150,0,255},{0,230,255,255}};

// weapon tables
// Weapons: [0]=shotgun (key 1), [1]=machine gun (key 2), [2]=rocket launcher (key 3)
static const char *WPN[]    = {"SHOTGUN", "MACHINE GUN", "ROCKETS"};
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
static void Slide(Vector3 *pos, float dx, float dz, float rad) {
    float nx=pos->x+dx;
    if (!IsWall(nx+(dx>=0?rad:-rad),pos->z) &&
        !IsWall(nx+(dx>=0?rad:-rad),pos->z+rad) &&
        !IsWall(nx+(dx>=0?rad:-rad),pos->z-rad)) pos->x=nx;
    float nz=pos->z+dz;
    if (!IsWall(pos->x,nz+(dz>=0?rad:-rad)) &&
        !IsWall(pos->x+rad,nz+(dz>=0?rad:-rad)) &&
        !IsWall(pos->x-rad,nz+(dz>=0?rad:-rad))) pos->z=nz;
}

// ── PLATFORM COLLISION ──────────────────────────────────────────────────────
// Returns true if the position (x, z) would penetrate a platform whose top is
// more than STEP_H above currentY (i.e. too tall to walk up onto).
static bool PlatBlocks(float x, float z, float currentY) {
    for (int i = 0; i < g_platCount; i++) {
        Platform *p = &g_plats[i];
        if (x > p->x0 - PRAD && x < p->x1 + PRAD &&
            z > p->z0 - PRAD && z < p->z1 + PRAD) {
            if (currentY < p->top - STEP_H) return true;
        }
    }
    return false;
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
    for (int i=0;i<n;i++) {
        Vector3 v={(float)rand()/RAND_MAX*8-4,(float)rand()/RAND_MAX*5+1,(float)rand()/RAND_MAX*8-4};
        SpawnPart(p,v,RED,0.4f+(float)rand()/RAND_MAX*0.5f,0.06f+(float)rand()/RAND_MAX*0.07f,true);
    }
}
static void Sparks(Vector3 p, int n) {
    for (int i=0;i<n;i++) {
        Vector3 v={(float)rand()/RAND_MAX*10-5,(float)rand()/RAND_MAX*6,(float)rand()/RAND_MAX*10-5};
        SpawnPart(p,v,ORANGE,0.2f+(float)rand()/RAND_MAX*0.25f,0.04f,true);
    }
}
static void Explode(Vector3 p) {
    PlaySound(g_sExplode);
    for (int i=0;i<28;i++) {
        Vector3 v={(float)rand()/RAND_MAX*16-8,(float)rand()/RAND_MAX*9+1,(float)rand()/RAND_MAX*16-8};
        Color c=i<14?(Color){255,90,0,255}:(Color){255,210,0,255};
        SpawnPart(p,v,c,0.55f+(float)rand()/RAND_MAX*0.5f,0.14f+(float)rand()/RAND_MAX*0.16f,true);
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
        if (p->pos.y<0.05f&&p->grav){p->pos.y=0.05f;p->vel.y*=-0.25f;p->vel.x*=0.55f;p->vel.z*=0.55f;}
    }
}
static void DrawParts(void) {
    for (int i=0;i<MAX_PARTS;i++) {
        Part *p=&g_pt[i]; if (!p->active) continue;
        float t=p->life/p->maxLife;
        Color c=p->col; c.a=(unsigned char)(255*t);
        DrawSphere(p->pos,p->size*t,c);
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
            pk->active=false; PlaySound(g_sPickup);
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

static void KillEnemy(int i) {
    Enemy *e=&g_e[i];
    // Keep the enemy active=true so the sprite keeps rendering — mark dying so
    // AI/bullets/minimap/alive-count skip it.
    e->dying = true; e->deathT = 0.f; e->hp = 0.f;
    Vector3 bp={e->pos.x,1.0f,e->pos.z}; Blood(bp,20); PlaySound(g_sDie);
    g_p.score+=e->score*g_wave; g_p.kills++;
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
    if (Alive()==0) {
        g_wave++;
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
                ne->pos=(Vector3){wx,0,wz}; ne->type=type;
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
        float dx=g_p.pos.x-e->pos.x, dz=g_p.pos.z-e->pos.z;
        float dist=sqrtf(dx*dx+dz*dz);
        if (dist<e->alertR) e->state=ES_CHASE;
        e->cd-=dt; e->stateT-=dt; e->legT+=dt*e->speed*2.8f;
        if (e->state==ES_PATROL) {
            Slide(&e->pos,e->pd.x*e->speed*dt,e->pd.z*e->speed*dt,0.42f);
            // Snap enemy Y to highest reachable platform under them (auto-step stairs)
            e->pos.y = PlatGroundAt(e->pos.x, e->pos.z, e->pos.y + STEP_H);
            if (e->stateT<0){float a=(float)rand()/RAND_MAX*6.28f;e->pd=(Vector3){sinf(a),0,cosf(a)};e->stateT=1.f+(float)rand()/RAND_MAX*2.5f;}
        } else if (e->state==ES_CHASE) {
            if (dist>e->atkR) {
                Slide(&e->pos,dx/dist*e->speed*dt,dz/dist*e->speed*dt,0.42f);
                e->pos.y = PlatGroundAt(e->pos.x, e->pos.z, e->pos.y + STEP_H);
            }
            else e->state=ES_ATTACK;
        } else {
            if (dist>e->atkR*1.5f) e->state=ES_CHASE;
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

        if (g_chefOK) {
            // Size tuned per enemy type
            float spriteH = (e->type==1) ? 2.3f : (e->type==2) ? 1.7f : 2.0f;
            Vector3 pos = {e->pos.x, spriteH*0.5f, e->pos.z};
            Texture2D tex;
            bool isCorpse = e->dying && g_chefDeathOK;
            if (isCorpse) {
                // Death animation: 4 frames at CHEF_DEATH_FRAME_TIME each, freeze on last
                int df = (int)(e->deathT / CHEF_DEATH_FRAME_TIME);
                if (df > 3) df = 3;
                tex = g_chefDeathTex[df];
            } else if (e->flashT > 0.f && g_chefPainOK) {
                // Pain frame while recently hit
                tex = g_chefPainTex;
            } else {
                // Walking cycle driven by legT
                int frame = (int)(e->legT * 0.6f) % 4;
                if (frame < 0) frame += 4;
                tex = g_chefTex[frame];
            }
            // Depth mask is globally disabled for the entire enemy pass (sorted back-to-front)
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

        // HP bar — world space, always upright (hidden once dying)
        if (!e->dying && e->hp<e->maxHp) {
            float hp=e->hp/e->maxHp;
            float bary=bh+1.25f;
            DrawCube((Vector3){e->pos.x,bary,e->pos.z},0.82f,0.08f,0.02f,DARKGRAY);
            DrawCube((Vector3){e->pos.x-0.41f+0.41f*hp,bary,e->pos.z},0.82f*hp,0.08f,0.03f,GREEN);
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
                for (int j=0;j<g_ec;j++) {
                    if (!g_e[j].active || g_e[j].dying) continue;
                    float d=Vector3Distance(b->pos,g_e[j].pos);
                    if (d<5.f) DmgEnemy(j,200.f*(1.f-d/5.f));
                }
                float pd=Vector3Distance(b->pos,g_p.pos);
                if (pd<5.f){g_p.hp-=30.f*(1.f-pd/5.f);g_p.hurtFlash=0.3f;if(g_p.hp<=0){g_p.dead=true;g_gs=GS_DEAD;}}
            } else Sparks(prev,5);
            b->active=false; continue;
        }
        for (int j=0;j<g_ec;j++) {
            Enemy *e=&g_e[j]; if (!e->active || e->dying) continue;
            float _ex=b->pos.x-e->pos.x, _ez=b->pos.z-e->pos.z;
            float _xzdist=sqrtf(_ex*_ex+_ez*_ez);
            float _ydist=fabsf(b->pos.y-1.0f); // enemy mid-body ~y=1
            if ((_xzdist<0.9f && _ydist<1.2f) || Vector3Distance(b->pos,(Vector3){e->pos.x,1.f,e->pos.z})<0.9f) {
                if (b->rocket) {
                    Explode(b->pos);
                    for (int k=0;k<g_ec;k++){if(!g_e[k].active||g_e[k].dying)continue;float d=Vector3Distance(b->pos,g_e[k].pos);if(d<5.f)DmgEnemy(k,200.f*(1.f-d/5.f));}
                    float pd=Vector3Distance(b->pos,g_p.pos);
                    if (pd<5.f){g_p.hp-=30.f*(1.f-pd/5.f);g_p.hurtFlash=0.3f;if(g_p.hp<=0){g_p.dead=true;g_gs=GS_DEAD;}}
                } else { Blood(b->pos,6); DmgEnemy(j,b->dmg); }
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
    if (g_p.shootCD>0) return;
    int w=g_p.weapon;
    if (w==0&&g_p.shells<=0){PlaySound(g_sEmpty);return;}
    if (w==1&&g_p.mgAmmo<=0){PlaySound(g_sEmpty);return;}
    if (w==2&&g_p.rockets<=0){PlaySound(g_sEmpty);return;}
    if (w==0){PlaySound(g_sShotgun);g_p.shells--;}
    else if (w==1){PlaySound(g_sPistol);g_p.mgAmmo--;}  // MG reuses pistol snd
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
    int pels=WPEL[w]; float sprd=(w==1)?0.10f:0.012f;
    for (int p=0;p<pels;p++) {
        Vector3 rd=fwd;
        if (pels>1){rd.x+=((float)rand()/RAND_MAX-.5f)*sprd*2;rd.y+=((float)rand()/RAND_MAX-.5f)*sprd;rd.z+=((float)rand()/RAND_MAX-.5f)*sprd*2;rd=Vector3Normalize(rd);}
        float best=1e9f; int bi=-1; bool headshot=false;
        for (int j=0;j<g_ec;j++){
            if (!g_e[j].active || g_e[j].dying) continue;
            float bh=(g_e[j].type==1)?1.1f:(g_e[j].type==2)?0.72f:0.9f;
            // check head sphere first (more damage)
            Vector3 hc={g_e[j].pos.x, bh+0.67f, g_e[j].pos.z};
            float ht=RSphere(eyePos,rd,hc,0.26f);
            if (ht>0&&ht<best){best=ht;bi=j;headshot=true;}
            // then body
            Vector3 bc={g_e[j].pos.x,0.85f,g_e[j].pos.z};
            float bt=RSphere(eyePos,rd,bc,0.68f);
            if (bt>0&&bt<best){best=bt;bi=j;headshot=false;}
        }
        if (bi>=0){
            Vector3 hp=Vector3Add(eyePos,Vector3Scale(rd,best));
            Blood(hp,headshot?12:5);
            float dmg=(float)WD[w]*(headshot?2.5f:1.0f);
            DmgEnemy(bi,dmg);
            if (headshot && p==0) Msg("HEADSHOT!");
        }
        else{for(float t=0.5f;t<50.f;t+=0.5f){Vector3 pt=Vector3Add(eyePos,Vector3Scale(rd,t));if(IsWall(pt.x,pt.z)){Sparks(pt,4);break;}}}
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
    DrawText("[ 1 ] SHOTGUN   [ 2 ] MACHINE GUN   [ 3 ] ROCKETS",sw/2-175,sh-18,12,(Color){80,80,80,255});
    // kill msg
    if (g_msgT>0) {
        unsigned char a=(unsigned char)(255.f*fminf(1.f,g_msgT));
        int tw=MeasureText(g_msg,20);
        DrawText(g_msg,sw/2-tw/2,sh/3,20,(Color){255,100,100,a});
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
            float sx2= wx*cosY - wz*sinY;
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
            float sx2= wx*cosY - wz*sinY;
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
    // Ensure cursor stays hidden during gameplay — force every frame
    // (otherwise alt-tab back in can show the OS cursor over the crosshair)
    HideCursor();
}

// ── PLAYER ───────────────────────────────────────────────────────────────────
static void UpdPlayer(float dt, Camera3D *cam) {
    if (g_p.dead) return;
    // --- MOUSE LOOK (reliable SetMousePosition technique) ---
    int cx2=GetScreenWidth()/2, cy2=GetScreenHeight()/2;
    Vector2 mp=GetMousePosition();
    float mdx=mp.x-(float)cx2, mdy=mp.y-(float)cy2;
    SetMousePosition(cx2,cy2);
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
            !PlatBlocks(nx, g_p.pos.z, g_p.pos.y)) g_p.pos.x = nx;
        float nz = g_p.pos.z + dz;
        if (!IsWall(g_p.pos.x,nz+(dz>=0?PRAD:-PRAD)) &&
            !IsWall(g_p.pos.x+PRAD,nz+(dz>=0?PRAD:-PRAD)) &&
            !IsWall(g_p.pos.x-PRAD,nz+(dz>=0?PRAD:-PRAD)) &&
            !PlatBlocks(g_p.pos.x, nz, g_p.pos.y)) g_p.pos.z = nz;
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
    const float ENEMY_RADIUS = 0.45f;
    for (int i = 0; i < g_ec; i++) {
        Enemy *e = &g_e[i];
        if (!e->active || e->dying) continue;
        float edx = g_p.pos.x - e->pos.x;
        float edz = g_p.pos.z - e->pos.z;
        float d2 = edx*edx + edz*edz;
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

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) Shoot();

    // timers
    if (g_p.shootCD>0)   g_p.shootCD-=dt;
    if (g_p.kickAnim>0)  g_p.kickAnim=fmaxf(0,g_p.kickAnim-dt*6.f);
    if (g_p.hurtFlash>0) g_p.hurtFlash-=dt;
    if (g_p.switchAnim>0)g_p.switchAnim=fmaxf(0,g_p.switchAnim-dt*5.f);
    if (g_p.shake>0)     g_p.shake=fmaxf(0,g_p.shake-dt*4.f);
    if (g_msgT>0)        g_msgT-=dt;
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
    // spawn wave 1
    for (int k=0;k<16&&g_ec<MAX_ENEMIES;k++) {
        for (int tries=0;tries<120;tries++) {
            int r=1+rand()%(ROWS-2), c=1+rand()%(COLS-2);
            if (MAP[r][c]) continue;
            float wx=c*CELL+CELL/2.f, wz=r*CELL+CELL/2.f;
            if (hypotf(wx-g_p.pos.x,wz-g_p.pos.z)<10.f) continue;
            Enemy *ne=&g_e[g_ec++]; *ne=(Enemy){0};
            ne->pos=(Vector3){wx,0,wz}; ne->type=0; ne->state=ES_PATROL;
            ne->hp=ne->maxHp=ET_HP[0]; ne->speed=ET_SPD[0];
            ne->dmg=ET_DMG[0]; ne->rate=ne->cd=ET_RATE[0];
            ne->alertR=ET_AR[0]; ne->atkR=ET_ATK[0]; ne->score=ET_SC[0]; ne->active=true;
            float ang=(float)rand()/RAND_MAX*6.28f; ne->pd=(Vector3){sinf(ang),0,cosf(ang)};
            ne->stateT=1.f+(float)rand()/RAND_MAX*2.f;
            break;
        }
    }
    g_gs=GS_PLAY;
    HideCursor();
    SetMousePosition(GetScreenWidth()/2,GetScreenHeight()/2);
    // Grace period: swallow the click that started the game so it doesn't fire a shot
    g_p.shootCD = 0.3f;
    strcpy(g_msg,""); g_msgT=0;
}

// ── MAIN ─────────────────────────────────────────────────────────────────────
int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SW,SH,"IRON FIST 3D");
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
    srand((unsigned)time(NULL));

    InitShader();

    // Load WolfenDoom weapon sprite viewmodels from bundle Resources.
    // Each entry: folder, file list (idle first, then fire frames), scale, count.
    {
        char appBase[512];
        snprintf(appBase, sizeof(appBase), "%s../Resources/sprites/", GetApplicationDirectory());

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
        g_wep[2].yShift = -0.12f;  // ROCKET: 12% up (was 10, +2% request)

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
    }

    // Set ambient
    float amb[4]={0.40f,0.32f,0.46f,1.f};
    SetShaderValue(g_shader,u_ambient,amb,SHADER_UNIFORM_VEC4);

    Texture2D tBrick = MkBrick();
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

    Camera3D cam={0};
    cam.fovy=90.f; cam.projection=CAMERA_PERSPECTIVE; cam.up=(Vector3){0,1,0};
    cam.position=(Vector3){1.5f*CELL,EYE_H,1.5f*CELL};
    cam.target=(Vector3){1.5f*CELL+1,EYE_H,1.5f*CELL};

    g_gs=GS_MENU;

    while (!WindowShouldClose()) {
        float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;

        if (g_gs==GS_MENU) {
            if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)||IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                InitGame();
        } else if (g_gs==GS_PLAY) {
            if (IsKeyPressed(KEY_ESCAPE)){g_gs=GS_MENU;}
            UpdPlayer(dt,&cam);
            UpdEnemies(dt);
            UpdBullets(dt);
            UpdParts(dt);
            UpdPicks();
        } else if (g_gs==GS_DEAD) {
            if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)) InitGame();
        }

        BeginDrawing();
        ClearBackground((Color){4,3,6,255});

        if (g_gs==GS_PLAY||g_gs==GS_DEAD) {
            BeginMode3D(cam);
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
            DrawEnemies(cam);
            DrawBullets();
            DrawParts();
            DrawPicks(cam);
            DrawWeapon3D(cam);
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
            const char *ctrl="WASD / ARROWS — MOVE     MOUSE — LOOK     LMB — FIRE\n"
                             "SPACE — JUMP     SHIFT — SPRINT\n"
                             "1 — SHOTGUN     2 — MACHINE GUN     3 — ROCKETS";
            DrawText(ctrl,sw2/2-MeasureText("WASD / ARROWS — MOVE     MOUSE — LOOK     LMB — FIRE",15)/2,sh2/2+20,15,(Color){90,90,90,255});
            const char *st="[ ENTER  /  CLICK  TO  START ]";
            if (sinf(GetTime()*3.f)>0)
                DrawText(st,sw2/2-MeasureText(st,22)/2,sh2*3/4,22,RED);
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
    UnloadShader(g_shader);
    UnloadSound(g_sPistol); UnloadSound(g_sShotgun); UnloadSound(g_sRocket);
    UnloadSound(g_sExplode); UnloadSound(g_sHurt); UnloadSound(g_sPickup);
    UnloadSound(g_sEmpty); UnloadSound(g_sDie);
    CloseAudioDevice(); CloseWindow();
    return 0;
}
