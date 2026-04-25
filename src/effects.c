#include "raylib.h"
#include "raymath.h"
#include "common.h"
#include "effects.h"

#include <stdlib.h>
#include <string.h>

static Part g_pt[MAX_PARTS];

void ResetParts(void) {
    memset(g_pt, 0, sizeof(g_pt));
}

void SpawnPart(Vector3 p, Vector3 v, Color c, float life, float sz, bool grav) {
    for (int i=0;i<MAX_PARTS;i++) if (!g_pt[i].active) {
        g_pt[i]=(Part){p,v,life,life,sz,c,true,grav}; return;
    }
}

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
}

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
}

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
