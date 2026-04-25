#ifndef IRONFIST_LEVEL_H
#define IRONFIST_LEVEL_H

#include "common.h"

extern const int MAP[ROWS][COLS];
extern Platform  g_plats[];
extern int       g_platCount;

bool  IsWall(float wx, float wz);
bool  IsWallCircle(float cx, float cz, float rad);
bool  PlatBlocks(float x, float z, float currentY, float rad);
float PlatPenetration(float x, float z, float currentY, float rad);
float PlatGroundAt(float x, float z, float currentY);
float PlatGroundAtR(float x, float z, float currentY, float rad);
void  InitPlatforms(void);

#endif
