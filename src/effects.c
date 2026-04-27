#include "raylib.h"
#include "raymath.h"
#include "common.h"
#include "effects.h"

#include <stdlib.h>
#include <string.h>

static Part g_pt[MAX_PARTS];

// Clears every slot in the particle pool. Called once at game start AND on
// each reset (e.g. after dying and restarting) so leftover decals from a
// previous run don't bleed into the new game.
void ResetParts(void) {
    memset(g_pt, 0, sizeof(g_pt));
}

// Allocates a single particle from the pool. First-fit: walks g_pt looking
// for an inactive slot. If the pool is full (MAX_PARTS=768 simultaneous
// particles) the spawn is silently dropped — a deliberate trade so heavy
// fire never drops the framerate budget on bookkeeping.
void SpawnPart(Vector3 p, Vector3 v, Color c, float life, float sz, bool grav) {
    for (int i=0;i<MAX_PARTS;i++) if (!g_pt[i].active) {
        g_pt[i]=(Part){p,v,life,life,sz,c,true,grav}; return;
    }
}

// Spawns a blood spatter at point p. The caller passes a "weight" n that's
// internally doubled — droplet counts come out denser than expected so a
// chef on flash typically dumps 12-50+ droplets across multiple Blood calls.
// Each droplet randomizes position, velocity, hue (90-160 red, no green/blue),
// size (~2-5cm), and life (0.35-0.8s). Marked grav=true so they fall and may
// settle on the floor as decals (handled in UpdParts).
void Blood(Vector3 p, int n) {
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
        Color col = { r, g, b, 220 };
        float size = 0.018f + (float)rand()/RAND_MAX * 0.030f;
        float life = 0.35f + (float)rand()/RAND_MAX * 0.45f;
        SpawnPart(spawn, v, col, life, size, true);
    }
    // v2 hit flourish: a single bright stationary "impact pop" plus a few
    // hot-yellow spark particles fanning out, so each shot landing on an
    // enemy reads as a punchy hit, not just a colour swap on the sprite.
    SpawnPart(p, (Vector3){0, 0.6f, 0}, (Color){255, 100, 90, 255},
              0.10f, 0.32f, false);
    for (int i = 0; i < 5; i++) {
        Vector3 v = {
            ((float)rand()/RAND_MAX - 0.5f) * 12.f,
            (float)rand()/RAND_MAX * 5.f + 1.f,
            ((float)rand()/RAND_MAX - 0.5f) * 12.f
        };
        SpawnPart(p, v, (Color){255, 220, 150, 255},
                  0.10f + (float)rand()/RAND_MAX * 0.10f,
                  0.06f, true);
    }
}

// Triple-layer impact effect for bullet-on-wall hits. Three particle types
// emitted in the same call:
//   1. Bright orange fast sparks (n of them) — short-lived, gravity-affected
//   2. Grey/brown debris chips (~2/3 n) — brick/concrete fragments that bounce
//   3. Dust puff (n/3) — slow non-gravity particles that fade quickly
// The 3-pass split is what makes a wall hit read as visceral instead of
// just "yellow points".
void Sparks(Vector3 p, int n) {
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
    // v2 flourish: a handful of bright hot-white sparks that fly fast and
    // brief, plus a quick yellow flash particle right at impact. Reads as
    // a hotter, more energetic muzzle/projectile-on-stone hit.
    for (int i = 0; i < n / 2 + 2; i++) {
        Vector3 v = {
            ((float)rand()/RAND_MAX - 0.5f) * 16.f,
            (float)rand()/RAND_MAX * 8.f + 1.f,
            ((float)rand()/RAND_MAX - 0.5f) * 16.f
        };
        SpawnPart(p, v, (Color){255, 250, 200, 255},
                  0.10f + (float)rand()/RAND_MAX * 0.12f,
                  0.05f, true);
    }
    // Brief stationary impact flash — single bright additive-feeling particle
    SpawnPart(p, (Vector3){0,0.3f,0}, (Color){255, 230, 130, 255},
              0.10f, 0.20f, false);
}

// Per-frame particle simulation. For each active particle:
//   - decrements life and deactivates when expired
//   - applies gravity (if grav flag)
//   - integrates position
//   - on floor contact: small slow droplets become PERMANENT floor decals
//     (stuck=true, life extended to 10-16s, no gravity); larger fast
//     particles bounce with energy loss instead.
// The "stuck decal" branch is the magic that makes blood pools accumulate
// under combat — tuned by the size+speed thresholds (sp2<3.5, size<0.08).
void UpdParts(float dt) {
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

// Renders every active particle as a cube. Stuck decals get a flattened
// y=0.02 cube (so they read as a flat splat on the floor) and fade only in
// their final 25% of life — keeping pools visible through the gameplay.
// Live particles shrink linearly with their remaining life.
void DrawParts(void) {
    for (int i=0;i<MAX_PARTS;i++) {
        Part *p=&g_pt[i]; if (!p->active) continue;
        float t=p->life/p->maxLife;
        Color c=p->col; c.a=(unsigned char)(255*t);
        if (p->stuck) {
            float s = p->size * 1.8f;
            float fade = (t < 0.25f) ? (t / 0.25f) : 1.f;
            c.a = (unsigned char)(255 * fade);
            DrawCubeV(p->pos, (Vector3){s, 0.02f, s}, c);
        } else {
            float s = p->size * t;
            DrawCubeV(p->pos, (Vector3){s, s, s}, c);
        }
    }
}
