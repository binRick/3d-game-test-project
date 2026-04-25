#include "raylib.h"
#include "common.h"
#include "level.h"

#include <math.h>

const int MAP[ROWS][COLS] = {
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

Platform g_plats[MAX_PLATS];
int      g_platCount = 0;

// Cell-resolution wall check at a single 2D point. Returns true for
// out-of-bounds positions too — important so projectiles at the world edge
// are treated as having hit a wall instead of escaping the map. Doesn't
// account for body radius; for that use IsWallCircle.
bool IsWall(float wx, float wz) {
    int c=(int)(wx/CELL), r=(int)(wz/CELL);
    if (r<0||r>=ROWS||c<0||c>=COLS) return true;
    return MAP[r][c]==1;
}

// Circle-vs-grid wall test — true if any wall cell's AABB is within `rad`
// of (cx, cz). Replaced an earlier 3-shoulder-points test that produced
// false-blocks at wall corners where the body only grazes a cell diagonally;
// the proper closest-point-on-AABB test here lets enemies and the player
// round corners smoothly.
bool IsWallCircle(float cx, float cz, float rad) {
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

// Platform-as-wall test. True when the position would penetrate a platform
// whose top is more than STEP_H above currentY (i.e. too tall to walk up
// onto). Step-up onto short platforms is allowed automatically because
// they pass the currentY check. Note this is a HARD block — for soft
// "can move away" semantics (e.g. an enemy that just fell off an edge),
// see PlatPenetration below.
bool PlatBlocks(float x, float z, float currentY, float rad) {
    for (int i = 0; i < g_platCount; i++) {
        Platform *p = &g_plats[i];
        if (x > p->x0 - rad && x < p->x1 + rad &&
            z > p->z0 - rad && z < p->z1 + rad) {
            if (currentY < p->top - STEP_H) return true;
        }
    }
    return false;
}

// "How deep am I inside a too-tall platform's safety margin?" — returns 0
// if the point is clear, positive if penetrating. EnemyMove uses this to
// allow moves that REDUCE penetration (e.g. an enemy that fell off an edge
// can still walk away from the cliff face) but not moves that deepen it.
// Without this gate, enemies that started inside a platform's margin
// (typically after a y-snap event) became permanently stuck.
float PlatPenetration(float x, float z, float currentY, float rad) {
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

// Returns the y-coordinate the player can stand on at (x, z) — strictly the
// highest platform top whose footprint contains the point AND is reachable
// (top <= currentY + 0.05f epsilon, so step-up moments don't snap back
// down). Default 0 if no platform applies. The player uses this for foot
// placement; enemies use the radius-margin variant below to step onto
// stairs as soon as their footprint overlaps.
float PlatGroundAt(float x, float z, float currentY) {
    float best = 0.f;
    for (int i = 0; i < g_platCount; i++) {
        Platform *p = &g_plats[i];
        if (x >= p->x0 && x <= p->x1 && z >= p->z0 && z <= p->z1) {
            if (p->top > best && p->top <= currentY + 0.05f) best = p->top;
        }
    }
    return best;
}

// Radius-margin variant of PlatGroundAt — used by enemies so their body
// climbs onto a platform the moment its footprint overlaps the platform
// edge, not only once the centre crosses the strict bounds. Without this,
// chefs would get caught on platform corners when approaching from the
// side, since their centre wasn't inside the bounds yet.
float PlatGroundAtR(float x, float z, float currentY, float rad) {
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

// Builds the level's hand-placed platform layout. Called once at game init.
// Two features:
//   1. Central staircase (rows 9-10, x=52..75) — 3 steps up to a 1.35m
//      "big platform" deck, then 2 steps back down. Each step is 0.45m
//      tall (under STEP_H=0.55) so the player walks up smoothly without
//      jumping.
//   2. Two jump-only platforms — a 1.10m mid-arena pillar (x=20-26, z=24-28)
//      and a 1.60m far-corner balcony (x=92-104, z=60-68).
// All step heights respect the global STEP_H rule so AI auto-climbing
// works consistently with the player.
void InitPlatforms(void) {
    g_platCount = 0;
    float z0 = 38.f, z1 = 42.f;
    g_plats[g_platCount++] = (Platform){52.f, z0, 55.f, z1, 0.45f};
    g_plats[g_platCount++] = (Platform){55.f, z0, 58.f, z1, 0.90f};
    g_plats[g_platCount++] = (Platform){58.f, z0, 61.f, z1, 1.35f};
    g_plats[g_platCount++] = (Platform){61.f, 36.f, 69.f, 44.f, 1.35f};
    g_plats[g_platCount++] = (Platform){69.f, z0, 72.f, z1, 0.90f};
    g_plats[g_platCount++] = (Platform){72.f, z0, 75.f, z1, 0.45f};
    g_plats[g_platCount++] = (Platform){20.f, 24.f, 26.f, 28.f, 1.10f};
    g_plats[g_platCount++] = (Platform){92.f, 60.f, 104.f, 68.f, 1.60f};
}
