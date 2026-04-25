#ifndef IRONFIST_EFFECTS_H
#define IRONFIST_EFFECTS_H

#include "raylib.h"
#include <stdbool.h>

void SpawnPart(Vector3 p, Vector3 v, Color c, float life, float sz, bool grav);
void Blood(Vector3 p, int n);
void Sparks(Vector3 p, int n);
void UpdParts(float dt);
void DrawParts(void);
void ResetParts(void);

#endif
