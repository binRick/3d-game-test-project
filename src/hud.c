#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "common.h"
#include "hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Game state owned by game.c — exposed via extern so the HUD can read it.
extern Player   g_p;
extern int      g_wave, g_ec, g_pkc;
extern Enemy    g_e[];
extern Pickup   g_pk[];
extern char     g_msg[80];
extern float    g_msgT;
extern char     g_hypeMsg[80];
extern float    g_hypeT, g_hypeDur;
extern Texture2D g_xhair[];
extern const char * const WPN[];
extern const Color ET_COL[];
extern const int MAP[ROWS][COLS];

// Implemented in game.c
extern int Alive(void);

// HUD-owned state — mugshot textures live here.
static Texture2D g_mugshot[5][3];
static Texture2D g_mugshotOuch;
static Texture2D g_mugshotKill;
static Texture2D g_mugshotDead;
static bool      g_mugshotOK = false;

// Loads the Doom mugshot texture set from the bundle Resources/sprites/hud/
// mugshot/ directory. Sets g_mugshotOK only if every health-tier × look-direction
// frame loads — if any one is missing the mugshot is gracefully suppressed
// and the rest of the HUD still draws. Pixel-art filtering is forced (no
// bilinear smoothing) to keep the 30×31 sprites crisp when scaled.
void InitHUD(const char *appBase) {
    char fp[700];
    g_mugshotOK = true;
    for (int t = 0; t < 5; t++) {
        for (int k = 0; k < 3; k++) {
            snprintf(fp, sizeof(fp), "%shud/mugshot/STFST%d%d.png", appBase, t, k);
            g_mugshot[t][k] = LoadTexture(fp);
            if (g_mugshot[t][k].id == 0) g_mugshotOK = false;
            else {
                SetTextureFilter(g_mugshot[t][k], TEXTURE_FILTER_POINT);
                SetTextureWrap  (g_mugshot[t][k], TEXTURE_WRAP_CLAMP);
            }
        }
    }
    snprintf(fp, sizeof(fp), "%shud/mugshot/STFOUCH0.png", appBase);
    g_mugshotOuch = LoadTexture(fp);
    if (g_mugshotOuch.id) { SetTextureFilter(g_mugshotOuch, TEXTURE_FILTER_POINT);
                            SetTextureWrap  (g_mugshotOuch, TEXTURE_WRAP_CLAMP); }
    snprintf(fp, sizeof(fp), "%shud/mugshot/STFKILL0.png", appBase);
    g_mugshotKill = LoadTexture(fp);
    if (g_mugshotKill.id) { SetTextureFilter(g_mugshotKill, TEXTURE_FILTER_POINT);
                            SetTextureWrap  (g_mugshotKill, TEXTURE_WRAP_CLAMP); }
    snprintf(fp, sizeof(fp), "%shud/mugshot/STFDEAD0.png", appBase);
    g_mugshotDead = LoadTexture(fp);
    if (g_mugshotDead.id) { SetTextureFilter(g_mugshotDead, TEXTURE_FILTER_POINT);
                            SetTextureWrap  (g_mugshotDead, TEXTURE_WRAP_CLAMP); }
}

// Releases all mugshot textures. Safe to call even if InitHUD partially
// failed — every UnloadTexture is guarded on a non-zero texture id so missing
// frames don't double-free or crash raylib.
void ShutdownHUD(void) {
    for (int t=0;t<5;t++) for (int k=0;k<3;k++) if (g_mugshot[t][k].id) UnloadTexture(g_mugshot[t][k]);
    if (g_mugshotOuch.id) UnloadTexture(g_mugshotOuch);
    if (g_mugshotKill.id) UnloadTexture(g_mugshotKill);
    if (g_mugshotDead.id) UnloadTexture(g_mugshotDead);
}

// Renders the entire 2D HUD over the 3D scene. Drawn AFTER EndMode3D in the
// main loop so it's pure screen-space. Layers, in order:
//   1. Hurt vignette (red flash on damage)
//   2. CRT scanlines (purely cosmetic)
//   3. Crosshair (texture for shotgun/MG, suppressed for rocket/tesla)
//   4. Bottom panel: HP bar, ammo, weapon name, Doom mugshot
//   5. Top corners: WAVE, ENEMIES, SCORE, KILLS, power-up timers
//   6. Centre: hype banner (last-survivor / multi-kill / power-up callouts)
//   7. Top-right: rotating-radar minimap
//   8. Cursor reset (forces capture every frame so alt-tab can't release it)
//
// All globals are read-only — the HUD never mutates game state.
void DrawHUD(void) {
    int sw=GetScreenWidth(), sh=GetScreenHeight();
#ifdef IRONFIST_V2
    // Boss low-HP rage tint: when any boss-tier enemy (chef boss / cyber
    // demon / spider mastermind) is below 25% HP, slow deep-red pulse on
    // top + bottom edges so the kill moment feels imminent.
    {
        bool bossLow = false;
        for (int bi = 0; bi < g_ec; bi++) {
            Enemy *be = &g_e[bi];
            if (!be->active || be->dying) continue;
            if (be->type != 3 && be->type != 9 && be->type != 16) continue;
            if (be->hp < be->maxHp * 0.25f) { bossLow = true; break; }
        }
        if (bossLow) {
            float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 3.f);
            unsigned char a = (unsigned char)(120 * pulse);
            Color edge  = {180, 0, 0, a};
            Color clear = {180, 0, 0, 0};
            int band = sh / 6;
            DrawRectangleGradientV(0, 0,           sw, band, edge,  clear);
            DrawRectangleGradientV(0, sh - band,   sw, band, clear, edge);
        }
    }
    // Powerup active overlay: while QUAD or SPEED is running, pulse a
    // tinted halo at the screen edges so you can see at a glance you're
    // powered up. QUAD = magenta on the side edges; SPEED = cyan on the
    // top/bottom edges. They stack visually if both are active.
    if (g_p.quadT > 0.f || g_p.hasteT > 0.f) {
        // Pulse rate accelerates in the last 3s of the powerup so you can
        // feel it about to expire. Picks the soonest-expiring of the two.
        float minLeft = 999.f;
        if (g_p.quadT  > 0.f && g_p.quadT  < minLeft) minLeft = g_p.quadT;
        if (g_p.hasteT > 0.f && g_p.hasteT < minLeft) minLeft = g_p.hasteT;
        float urgency = (minLeft < 3.f) ? (1.f + (3.f - minLeft) * 2.0f) : 1.f;
        float pulse = 0.6f + 0.3f * sinf((float)GetTime() * 4.f * urgency);
        if (g_p.quadT > 0.f) {
            unsigned char a = (unsigned char)(75 * pulse);
            Color edge  = {220, 50, 220, a};
            Color clear = {220, 50, 220, 0};
            int band = sw / 8;
            DrawRectangleGradientH(0,           0, band, sh, edge,  clear);
            DrawRectangleGradientH(sw - band,   0, band, sh, clear, edge);
        }
        if (g_p.hasteT > 0.f) {
            unsigned char a = (unsigned char)(70 * pulse);
            Color edge  = {40, 210, 255, a};
            Color clear = {40, 210, 255, 0};
            int band = sh / 8;
            DrawRectangleGradientV(0, 0,           sw, band, edge,  clear);
            DrawRectangleGradientV(0, sh - band,   sw, band, clear, edge);
        }
    }
    // Low-health danger pulse: slow red breathing on the screen edges when
    // HP is critical (< 25%). Pulse rate and brightness ramp as HP drops
    // further so the screen feels more frantic the closer you are to dying.
    if (g_p.hp > 0.f && g_p.hp < g_p.maxHp * 0.25f) {
        float danger = 1.f - (g_p.hp / (g_p.maxHp * 0.25f));
        float rate   = 2.5f + danger * 4.0f;
        float pulse  = 0.5f + 0.5f * sinf((float)GetTime() * rate);
        unsigned char a = (unsigned char)(95 * pulse * (0.5f + danger * 0.5f));
        Color edge  = {220, 0, 0, a};
        Color clear = {220, 0, 0, 0};
        int vband = sh / 5;
        int hband = sw / 7;
        DrawRectangleGradientV(0, 0,           sw, vband, edge,  clear);
        DrawRectangleGradientV(0, sh - vband,  sw, vband, clear, edge);
        DrawRectangleGradientH(0, 0,           hband, sh,  edge,  clear);
        DrawRectangleGradientH(sw - hband, 0,  hband, sh,  clear, edge);
    }
    // Heal flash: brief green edge tint when picking up a health pickup,
    // so the heal moment registers as visually distinct from the pickup
    // grab burst (which uses pickup colour, red for health).
    {
        extern float g_v2HealFlash;
        if (g_v2HealFlash > 0.f) {
            float t = g_v2HealFlash / 0.45f; if (t > 1.f) t = 1.f;
            unsigned char a = (unsigned char)(140 * t);
            Color edge  = {60, 220, 90, a};
            Color clear = {60, 220, 90, 0};
            int vband = sh / 5;
            int hband = sw / 7;
            DrawRectangleGradientV(0, 0,           sw, vband, edge,  clear);
            DrawRectangleGradientV(0, sh - vband,  sw, vband, clear, edge);
            DrawRectangleGradientH(0, 0,           hband, sh,  edge,  clear);
            DrawRectangleGradientH(sw - hband, 0,  hband, sh,  clear, edge);
        }
    }
    // Death fade — dim the world with a dark red overlay while dead so the
    // GAME OVER screen feels weightier than a clean swap.
    if (g_p.dead) {
        DrawRectangle(0, 0, sw, sh, (Color){40, 0, 0, 140});
    }
    // Muzzle screen-flash: brief warm yellow edge tint while kickAnim is
    // active (set by Shoot() to 0.18 and decaying). Strongest at top
    // (where the gun barrel is) and weaker around the other edges, so
    // each shot reads as a punch on screen, not just a viewmodel kick.
    // Combo chain counter: floating "x2"/"x3"/etc when killing in quick
    // succession. Stays on screen as long as the combo window is open
    // (see g_v2ComboT decay in StepFrame).
    {
        extern int   g_v2ComboCount;
        extern float g_v2ComboT;
        if (g_v2ComboCount >= 2 && g_v2ComboT > 0.f) {
            char buf[16]; snprintf(buf, sizeof(buf), "x%d", g_v2ComboCount);
            int fsize = 64 + (g_v2ComboCount > 6 ? 6 : g_v2ComboCount) * 4;
            int tw = MeasureText(buf, fsize);
            // Position upper-right of crosshair so it doesn't block aim
            int tx = sw - tw - 60;
            int ty = sh / 3;
            unsigned char alpha = (unsigned char)(255 * (g_v2ComboT > 0.5f ? 1.f : g_v2ComboT * 2.f));
            Color tcol = (g_v2ComboCount >= 5) ? (Color){255, 80, 60, alpha}
                       : (g_v2ComboCount >= 3) ? (Color){255, 200, 80, alpha}
                       :                          (Color){240, 240, 240, alpha};
            // Drop shadow + main text
            DrawText(buf, tx + 3, ty + 3, fsize, (Color){0, 0, 0, alpha});
            DrawText(buf, tx,     ty,     fsize, tcol);
        }
    }
    if (g_p.kickAnim > 0.f) {
        float k = g_p.kickAnim / 0.18f; if (k > 1.f) k = 1.f;
        unsigned char aTop  = (unsigned char)(140 * k);
        unsigned char aSide = (unsigned char)( 70 * k);
        // Quad damage swaps the muzzle tint to magenta to match the bullet
        // tracer recolour, so the buff is visible from the gun side too.
        bool quadFire = (g_p.quadT > 0.f);
        unsigned char r = quadFire ? 240 : 255;
        unsigned char g = quadFire ?  90 : 210;
        unsigned char b = quadFire ? 255 : 130;
        Color flashT = {r, g, b, aTop };
        Color flashS = {r, g, b, aSide};
        Color clear  = {r, g, b, 0};
        int top  = sh / 5;
        int side = sw / 9;
        DrawRectangleGradientV(0, 0,           sw, top,  flashT, clear);
        DrawRectangleGradientV(0, sh - top/2,  sw, top/2,clear,  flashS);
        DrawRectangleGradientH(0, 0,           side, sh, flashS, clear);
        DrawRectangleGradientH(sw - side, 0,   side, sh, clear,  flashS);
    }
#endif
    if (g_p.hurtFlash>0) {
#ifdef IRONFIST_V2
        // Edge-vignette pulse: 4 gradient strips fade from blood-red at the
        // screen edges to transparent toward the centre. Reads as a damage
        // pulse rather than a screen-fill blackout.
        float t = g_p.hurtFlash / 0.30f;
        if (t > 1.f) t = 1.f;
        unsigned char aMax = (unsigned char)(220 * t);
        Color edge  = {220, 0, 0, aMax};
        Color clear = {220, 0, 0, 0};
        int vband = sh / 4;
        int hband = sw / 5;
        DrawRectangleGradientV(0, 0,             sw, vband, edge,  clear);
        DrawRectangleGradientV(0, sh - vband,    sw, vband, clear, edge);
        DrawRectangleGradientH(0, 0,             hband, sh, edge,  clear);
        DrawRectangleGradientH(sw - hband, 0,    hband, sh, clear, edge);
#else
        int a=(int)(g_p.hurtFlash/0.22f*180);
        DrawRectangle(0,0,sw,sh,(Color){200,0,0,(unsigned char)a});
#endif
    }
    for (int y=0;y<sh;y+=3) DrawLine(0,y,sw,y,(Color){0,0,0,18});
    int cx=sw/2,cy=sh/2;
    int wpn=g_p.weapon;
    if (wpn == 2 || wpn == 3) {
        // no crosshair for rocket / tesla
    } else if (wpn>=0 && wpn<4 && g_xhair[wpn].id) {
        Texture2D xh = g_xhair[wpn];
        float xsc = 1.2f;
#ifdef IRONFIST_V2
        // Crosshair tinted by current weapon, plus a brief size-flare
        // pulse on hit confirm (g_v2HitMarker).
        Color xCol = (wpn == 0) ? (Color){255, 220, 130, 255}   // shotgun warm
                   : (wpn == 1) ? (Color){180, 220, 255, 255}   // MG cool
                   :              WHITE;
        extern float g_v2HitMarker;
        if (g_v2HitMarker > 0.f) {
            xsc *= 1.f + (g_v2HitMarker / 0.10f) * 0.45f;
            xCol = (Color){255, 80, 80, 255};  // red tint while hit-flaring
        }
#endif
        float xw = xh.width*xsc, xh2 = xh.height*xsc;
        DrawTexturePro(xh,
            (Rectangle){0,0,(float)xh.width,(float)xh.height},
            (Rectangle){cx-xw/2, cy-xh2/2, xw, xh2},
            (Vector2){0,0}, 0.f,
#ifdef IRONFIST_V2
            xCol);
#else
            WHITE);
#endif
    } else {
        DrawRectangle(cx-14,cy-1,10,2,WHITE); DrawRectangle(cx+4,cy-1,10,2,WHITE);
        DrawRectangle(cx-1,cy-14,2,10,WHITE); DrawRectangle(cx-1,cy+4,2,10,WHITE);
    }
    DrawRectangle(0,sh-64,sw,64,(Color){8,8,12,220});
    DrawLine(0,sh-64,sw,sh-64,(Color){80,0,0,200});
    float hp=fmaxf(0.f,g_p.hp/g_p.maxHp);
    Color hcol=hp>0.5f?(Color){50,220,50,255}:hp>0.25f?(Color){220,180,0,255}:(Color){220,30,30,255};
    DrawText("HP",20,sh-56,12,(Color){180,180,180,255});
    DrawRectangle(20,sh-40,200,18,(Color){30,0,0,200});
    DrawRectangle(21,sh-39,(int)(198*hp),16,hcol);
    DrawRectangle(20,sh-40,200,18,(Color){80,80,80,80});
    char hpBuf[16]; snprintf(hpBuf,16,"%d",(int)g_p.hp);
#ifdef IRONFIST_V2
    // HP number flashes to white-red on each damage hit so the running
    // total registers visually as you take fire.
    Color hpDispCol = hcol;
    if (g_p.hurtFlash > 0.f) {
        float ht = g_p.hurtFlash / 0.30f; if (ht > 1.f) ht = 1.f;
        hpDispCol = (Color){
            (unsigned char)(hcol.r + (unsigned char)((255 - hcol.r) * ht)),
            (unsigned char)((float)hcol.g * (1.f - ht * 0.6f)),
            (unsigned char)((float)hcol.b * (1.f - ht * 0.6f)),
            255
        };
    }
    DrawText(hpBuf,228,sh-43,18,hpDispCol);
#else
    DrawText(hpBuf,228,sh-43,18,hcol);
#endif
    char aBuf[16];
    if      (g_p.weapon==0) snprintf(aBuf,16,"%d",g_p.shells);
    else if (g_p.weapon==1) snprintf(aBuf,16,"%d",g_p.mgAmmo);
    else if (g_p.weapon==2) snprintf(aBuf,16,"%d",g_p.rockets);
    else                    snprintf(aBuf,16,"%d",g_p.cells);
    DrawText(WPN[g_p.weapon],sw-220,sh-58,13,SKYBLUE);
#ifdef IRONFIST_V2
    // Low-ammo pulse — when the active weapon's ammo drops under a tier,
    // the ammo number pulses red-orange so you notice you're running out.
    int lowThresh = (g_p.weapon == 0) ? 8 :
                    (g_p.weapon == 1) ? 30 :
                    (g_p.weapon == 2) ? 2  :
                                        10;
    int curAmmo = (g_p.weapon == 0) ? g_p.shells :
                  (g_p.weapon == 1) ? g_p.mgAmmo :
                  (g_p.weapon == 2) ? g_p.rockets :
                                       g_p.cells;
    Color aCol = YELLOW;
    if (curAmmo > 0 && curAmmo <= lowThresh) {
        float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 6.5f);
        aCol = (Color){255, (unsigned char)(80 + 90 * (1.f - pulse)), 50, 255};
    }
    DrawText(aBuf,sw-220,sh-46,36,aCol);
#else
    DrawText(aBuf,sw-220,sh-46,36,YELLOW);
#endif

    if (g_mugshotOK) {
        static int   look       = 1;
        static float lookT      = 0.f;
        static int   lastKills  = 0;
        static float killFlash  = 0.f;
        float dt = GetFrameTime();
        lookT += dt;
        if (lookT > 0.8f) { lookT = 0.f; look = rand() % 3; }
        if (g_p.kills > lastKills) { killFlash = 0.5f; lastKills = g_p.kills; }
        if (killFlash > 0.f) killFlash -= dt;
        float hpFrac = (g_p.hp <= 0.f) ? 0.f : g_p.hp / g_p.maxHp;
        int tier = (hpFrac >= 0.80f) ? 0 :
                   (hpFrac >= 0.60f) ? 1 :
                   (hpFrac >= 0.40f) ? 2 :
                   (hpFrac >= 0.20f) ? 3 : 4;
        Texture2D mug = g_mugshot[tier][look];
        if (g_p.hp <= 0.f && g_mugshotDead.id) mug = g_mugshotDead;
        else if (g_p.hurtFlash > 0.04f && g_mugshotOuch.id) mug = g_mugshotOuch;
        else if (killFlash > 0.f && g_mugshotKill.id) mug = g_mugshotKill;
        if (mug.id) {
            const float scale = 1.6f;
            float mw = mug.width  * scale;
            float mh = mug.height * scale;
            float mx = sw * 0.5f - mw * 0.5f;
            float my = sh - mh - 6.f;
            DrawTexturePro(mug,
                (Rectangle){0,0,(float)mug.width,(float)mug.height},
                (Rectangle){mx, my, mw, mh},
                (Vector2){0,0}, 0.f, WHITE);
        }
    }

    char sc[48]; snprintf(sc,48,"SCORE %d",g_p.score);
#ifdef IRONFIST_V2
    // SCORE number flashes gold for ~250ms on each score increase, so the
    // number registers as "earned" rather than silently ticking up.
    static int  v2_lastScore = 0;
    static float v2_scoreFlash = 0.f;
    if (g_p.score > v2_lastScore) v2_scoreFlash = 0.25f;
    v2_lastScore = g_p.score;
    if (v2_scoreFlash > 0.f) v2_scoreFlash -= GetFrameTime();
    Color scCol = (v2_scoreFlash > 0.f) ? (Color){255, 220, 80, 255} : WHITE;
    int   scSize = 18 + (v2_scoreFlash > 0.f ? (int)(v2_scoreFlash / 0.25f * 4.f) : 0);
    DrawText(sc, sw - MeasureText(sc, scSize) - 170, 10, scSize, scCol);
#else
    DrawText(sc,sw-MeasureText(sc,18)-170,10,18,WHITE);
#endif
    char wv[24]; snprintf(wv,24,"WAVE %d",g_wave);
#ifdef IRONFIST_V2
    // WAVE counter pulses gold for ~1s on increment so the wave change
    // doesn't go unnoticed when the screen is busy.
    static int  v2_lastWave = 1;
    static float v2_waveFlash = 0.f;
    if (g_wave > v2_lastWave) v2_waveFlash = 1.0f;
    v2_lastWave = g_wave;
    if (v2_waveFlash > 0.f) v2_waveFlash -= GetFrameTime();
    Color wvCol = (v2_waveFlash > 0.f) ? (Color){255, 230, 80, 255} : (Color){255,160,40,255};
    int   wvSize = 18 + (v2_waveFlash > 0.f ? (int)(v2_waveFlash * 6.f) : 0);
    DrawText(wv, 12, 10, wvSize, wvCol);
#else
    DrawText(wv,12,10,18,(Color){255,160,40,255});
#endif
    char en[32]; snprintf(en,32,"ENEMIES: %d",Alive());
    DrawText(en,12,32,14,(Color){200,60,60,255});
    {
        const int barW = 140, barH = 7;
        int powY = 52;
        if (g_p.quadT > 0.f && g_p.quadPeak > 0.f) {
            bool urgent = g_p.quadT <= 3.f;
            unsigned char alpha = urgent ? (unsigned char)(160 + 95*(0.5f+0.5f*sinf((float)GetTime()*12.f))) : 255;
            Color col = (Color){220, 80,230,alpha};
            char qb[32]; snprintf(qb,32,"QUAD %4.1fs", g_p.quadT);
            DrawText(qb,12,powY,16,col);
            float frac = fminf(1.f, g_p.quadT / g_p.quadPeak);
            DrawRectangle(12, powY+18, barW,            barH, (Color){25, 5, 30, 200});
            DrawRectangle(12, powY+18, (int)(barW*frac),barH, col);
            DrawRectangleLines(12, powY+18, barW,       barH, (Color){80, 80, 80, 120});
            powY += 18 + barH + 4;
        }
        if (g_p.hasteT > 0.f && g_p.hastePeak > 0.f) {
            bool urgent = g_p.hasteT <= 3.f;
            unsigned char alpha = urgent ? (unsigned char)(160 + 95*(0.5f+0.5f*sinf((float)GetTime()*12.f))) : 255;
            Color col = (Color){ 60,210,255,alpha};
            char hb[32]; snprintf(hb,32,"SPEED %4.1fs", g_p.hasteT);
            DrawText(hb,12,powY,16,col);
            float frac = fminf(1.f, g_p.hasteT / g_p.hastePeak);
            DrawRectangle(12, powY+18, barW,            barH, (Color){5, 25, 30, 200});
            DrawRectangle(12, powY+18, (int)(barW*frac),barH, col);
            DrawRectangleLines(12, powY+18, barW,       barH, (Color){80, 80, 80, 120});
        }
    }
    char kl[32]; snprintf(kl,32,"KILLS: %d",g_p.kills);
    int klw=MeasureText(kl,16);
    DrawText(kl,sw-klw-170,32,16,(Color){255,80,80,255});
    if (g_msgT>0) {
        unsigned char a=(unsigned char)(255.f*fminf(1.f,g_msgT));
        int tw=MeasureText(g_msg,20);
        DrawText(g_msg,sw/2-tw/2,sh/3,20,(Color){255,100,100,a});
    }
    if (g_hypeT>0) {
        bool monster = (g_hypeMsg[0]=='M' && g_hypeMsg[1]=='O' && g_hypeMsg[2]=='N');
        float aF = fminf(1.f, g_hypeT/0.9f);
        float t = (float)GetTime();
        bool onBeat = (sinf(t*31.4f) > 0.f);
        Color c;
        if (monster) {
            c = onBeat ? (Color){255, 50, 30,(unsigned char)(255.f*aF)}
                       : (Color){255,255,255,(unsigned char)(255.f*aF)};
        } else {
            c = onBeat ? (Color){255,230, 40,(unsigned char)(255.f*aF)}
                       : (Color){255,255,255,(unsigned char)(255.f*aF)};
        }
        float amp   = monster ? 0.20f : 0.12f;
        float pulse = 1.f + amp*(0.5f + 0.5f*sinf(t*19.0f));
        int   base  = monster ? 86 : 44;
        int fs = (int)(base*pulse + 0.5f);
        int tw = MeasureText(g_hypeMsg, fs);
        int tx = sw/2 - tw/2;
        int ty = sh/4;
        int sh_off = monster ? 5 : 3;
        DrawText(g_hypeMsg, tx+sh_off, ty+sh_off, fs, (Color){0,0,0,(unsigned char)(200.f*aF)});
        DrawText(g_hypeMsg, tx,        ty,        fs, c);
    }
    {
        int cx2=sw-90, cy2=88;
        int radius=78;
        float scale=0.95f;
        float yaw=g_p.yaw+3.14159f;
        float cosY=cosf(yaw), sinY=sinf(yaw);
        DrawCircle(cx2,cy2,radius+2,(Color){0,0,0,200});
        DrawCircle(cx2,cy2,radius,(Color){15,10,8,210});
        float pw=g_p.pos.x, pz=g_p.pos.z;
        for (int r=0;r<ROWS;r++) for (int c=0;c<COLS;c++) {
            if (!MAP[r][c]) continue;
            float wx=c*CELL+CELL*0.5f - pw;
            float wz=r*CELL+CELL*0.5f - pz;
            float sx2= wz*sinY - wx*cosY;
            float sy2= wx*sinY + wz*cosY;
            int px3=cx2+(int)(sx2/scale);
            int py3=cy2-(int)(sy2/scale);
            int cs=(int)(CELL/scale)+1;
            if (cs<2) cs=2;
            float dx2=(float)(px3+cs/2-cx2), dy2=(float)(py3+cs/2-cy2);
            if (dx2*dx2+dy2*dy2 < (float)(radius*radius))
                DrawRectangle(px3,py3,cs,cs,(Color){80,50,35,230});
        }
        for (int i=0;i<g_ec;i++) {
            Enemy *e=&g_e[i]; if (!e->active || e->dying) continue;
            float wx=e->pos.x-pw, wz=e->pos.z-pz;
            float sx2= wz*sinY - wx*cosY;
            float sy2= wx*sinY + wz*cosY;
            int ex=cx2+(int)(sx2/scale);
            int ey=cy2-(int)(sy2/scale);
            float dd=(float)((ex-cx2)*(ex-cx2)+(ey-cy2)*(ey-cy2));
            if (dd<(float)(radius*radius)){
                Color ec2=ET_COL[e->type]; ec2.a=240;
                DrawCircle(ex,ey,4,ec2);
            }
        }
        for (int i=0;i<g_pkc;i++) {
            Pickup *pk=&g_pk[i]; if (!pk->active) continue;
            if (pk->type != 5 && pk->type != 6) continue;
            float wx=pk->pos.x-pw, wz=pk->pos.z-pz;
            float sx2= wz*sinY - wx*cosY;
            float sy2= wx*sinY + wz*cosY;
            int px2=cx2+(int)(sx2/scale);
            int py2=cy2-(int)(sy2/scale);
            float dd=(float)((px2-cx2)*(px2-cx2)+(py2-cy2)*(py2-cy2));
            if (dd >= (float)(radius*radius)) continue;
            Color col = (pk->type == 5) ? (Color){220, 80,230,255}
                                        : (Color){ 60,210,255,255};
            float pulse = 0.6f + 0.4f*sinf((float)GetTime()*5.f + (float)i);
            DrawCircle(px2,py2,4,col);
            DrawCircleLines(px2,py2,(int)(6+3*pulse), Fade(col, 0.85f));
        }
        // Rear-half tint — when ANY live enemy sits in the lower (rear)
        // half of the radar within 12m, pulse-tint that half red. Forward
        // = upper half because the radar rotates so player-yaw points up
        // (sy2 > 0 → py3 < cy2 = upper). Rear enemies have sy2 < 0.
        {
            bool rearAny = false;
            for (int i = 0; i < g_ec; i++) {
                Enemy *e = &g_e[i];
                if (!e->active || e->dying) continue;
                float wx = e->pos.x - pw, wz = e->pos.z - pz;
                float dlen = sqrtf(wx*wx + wz*wz);
                if (dlen > 12.f) continue;  // only flag close-range rear
                float sy2 = wx*sinY + wz*cosY;
                if (sy2 < 0.f) { rearAny = true; break; }
            }
            if (rearAny) {
                float pulse = 0.5f + 0.5f*sinf((float)GetTime()*6.f);
                unsigned char a = (unsigned char)(40 + 60*pulse);
                // Half-disc fill (lower half, y >= cy2). Stamp many short
                // chord rectangles since raylib doesn't ship a half-disc
                // primitive — quick + alpha-blends fine.
                for (int dy = 0; dy < radius; dy++) {
                    int span = (int)sqrtf((float)(radius*radius - dy*dy));
                    DrawRectangle(cx2 - span, cy2 + dy, span*2, 1,
                                  (Color){200, 50, 50, a});
                }
            }
        }
        DrawCircle(cx2,cy2,5,WHITE);
        DrawTriangle((Vector2){(float)cx2,(float)(cy2-10)},
                     (Vector2){(float)(cx2-5),(float)(cy2+2)},
                     (Vector2){(float)(cx2+5),(float)(cy2+2)}, YELLOW);
        DrawCircleLines(cx2,cy2,radius+1,(Color){100,80,60,180});
        DrawText("N",cx2-4,cy2-radius-14,12,(Color){180,180,180,200});
    }
    HideCursor();
    SetMousePosition(GetScreenWidth()/2, GetScreenHeight()/2);
}
