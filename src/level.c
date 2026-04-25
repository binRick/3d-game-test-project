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

bool IsWall(float wx, float wz) {
    int c=(int)(wx/CELL), r=(int)(wz/CELL);
    if (r<0||r>=ROWS||c<0||c>=COLS) return true;
    return MAP[r][c]==1;
}

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
