#include "raylib.h"
#include "common.h"
#include "level.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// V2 level engine — wall grid loaded from a text file in
// <app>/Contents/Resources/levels/level0.txt at startup. The default
// layout below is identical to v1's hardcoded MAP and is used as a
// fallback if the text file is missing or malformed, so the game still
// runs cleanly without it.
//
// Text format (v1):
//   - 30 cols x 20 rows of '#' (wall) or '.' (empty)
//   - 's' = player spawn (acts as '.' for collision; only first 's' wins)
//   - lines starting with ';' are comments
//   - blank lines are ignored
//   - characters outside the alphabet above are treated as '.'
//
// All other map-driven systems (platforms, pickups, enemies, boss spawn)
// remain hand-authored in code for now. Promoting them to text-driven
// tile classes is the next iteration.

int     MAP[ROWS][COLS] = {
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
Vector3  g_playerStart = {1.5f * CELL, 0.f, 1.5f * CELL};
float    g_playerStartYaw = 0.f;

// Reads up to 64 KiB of the file into a malloc-free static buffer; rejects
// files that don't have ROWS data lines of COLS chars each. Comment lines
// (starting ';') and blank lines are skipped.
static bool LoadLevelFromFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    int  parsedRows = 0;
    int  tmp[ROWS][COLS];
    int  spawnR = -1, spawnC = -1;
    char line[256];

    while (parsedRows < ROWS && fgets(line, sizeof(line), f)) {
        // strip trailing CR/LF
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        if (n == 0) continue;
        if (line[0] == ';') continue;
        if (line[0] == ' ' || line[0] == '\t') continue;
        // Metadata lines (any line not starting with a known map char). Parse
        // recognized keys and skip; unknown keys are silently ignored so the
        // format can grow forward-compatibly.
        if (line[0] != '#' && line[0] != '.' && line[0] != 's') {
            if (!strncmp(line, "face:", 5)) {
                g_playerStartYaw = (float)atof(line + 5);
            }
            continue;
        }
        if (n < (size_t)COLS) { fclose(f); return false; }

        for (int c = 0; c < COLS; c++) {
            char ch = line[c];
            if (ch == '#')      tmp[parsedRows][c] = 1;
            else if (ch == 's') { tmp[parsedRows][c] = 0; if (spawnR < 0) { spawnR = parsedRows; spawnC = c; } }
            else                tmp[parsedRows][c] = 0;
        }
        parsedRows++;
    }
    fclose(f);

    if (parsedRows != ROWS) return false;

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            MAP[r][c] = tmp[r][c];

    if (spawnR >= 0) {
        g_playerStart.x = spawnC * CELL + CELL * 0.5f;
        g_playerStart.z = spawnR * CELL + CELL * 0.5f;
    }
    return true;
}

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
    char path[1024];
    snprintf(path, sizeof(path), "%s../Resources/levels/level0.txt",
             GetApplicationDirectory());
    if (LoadLevelFromFile(path)) {
        TraceLog(LOG_INFO, "v2: loaded level from %s", path);
    } else {
        TraceLog(LOG_WARNING, "v2: level file missing or invalid (%s); using built-in fallback", path);
    }

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
