#ifndef IRONFIST_COMMON_H
#define IRONFIST_COMMON_H

#include "raylib.h"
#include <stdbool.h>

// Shared types and constants used by code that lives outside game.c.
// game.c is the single source of truth for the matching globals.

#define MAX_ENEMIES 96
#define EYE_H       1.65f
#define CELL        4.0f
#define ROWS        20
#define COLS        30

typedef enum { GS_MENU, GS_PLAY, GS_DEAD } GameState;
typedef enum { ES_PATROL, ES_CHASE, ES_ATTACK } EnemyState;

typedef struct {
    Vector3 pos; float yaw, pitch, velY;
    bool onGround, dead;
    float hp, maxHp;
    float shootCD, kickAnim, bobT, hurtFlash, shake, switchAnim;
    int   weapon, bullets, shells, rockets, mgAmmo, cells, score, kills;
    bool  hasTesla;
    float quadT, hasteT, quadPeak, hastePeak;
} Player;

typedef struct { Vector3 pos; int type; int variant; bool active; float bobT; } Pickup;

typedef struct {
    Vector3 pos; EnemyState state;
    int type; float hp, maxHp, speed, dmg, rate, cd, stateT;
    Vector3 pd; float legT, flashT, alertR, atkR;
    bool active; int score;
    bool  dying;
    float deathT;
    float bleedT;
} Enemy;

#endif
