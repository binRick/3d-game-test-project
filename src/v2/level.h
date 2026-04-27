#ifndef IRONFIST_LEVEL_H
#define IRONFIST_LEVEL_H

#include "common.h"

extern int       MAP[ROWS][COLS];
extern Platform  g_plats[];
extern int       g_platCount;
extern Vector3   g_playerStart;
extern float     g_playerStartYaw;

bool  IsWall(float wx, float wz);
bool  IsWallCircle(float cx, float cz, float rad);
bool  PlatBlocks(float x, float z, float currentY, float rad);
float PlatPenetration(float x, float z, float currentY, float rad);
float PlatGroundAt(float x, float z, float currentY);
float PlatGroundAtR(float x, float z, float currentY, float rad);
void  InitPlatforms(void);

#endif
