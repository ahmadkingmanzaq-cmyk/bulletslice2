/* BULLET SLICE - platform-independent game logic.
 * No PSP or OS calls in here: input in, rectangles out. That lets the same
 * code run on the PSP (main_psp.c) and be unit-tested on a PC (test/main_test.c). */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define SCR_W 480
#define SCR_H 272
#define WORLD_W 960
#define FLOOR_Y 232.0f
#define MAX_BULLETS 160
#define MAX_ENEMIES 12
#define MAX_PARTS 96
#define NUM_WAVES 3

/* Input bitmask. The backend maps the analog nub / d-pad / buttons onto these. */
enum {
    IN_LEFT = 1 << 0, IN_RIGHT = 1 << 1, IN_UP = 1 << 2, IN_DOWN = 1 << 3,
    IN_JUMP = 1 << 4, IN_FIRE = 1 << 5, IN_RELOAD = 1 << 6, IN_DODGE = 1 << 7,
    IN_PAIN = 1 << 8, IN_SPRINT = 1 << 9, IN_SWAP = 1 << 10, IN_START = 1 << 11
};

/* Colours are PSP-native ABGR8888. */
#define COL(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

typedef void (*RectFn)(float x, float y, float w, float h, uint32_t col);

typedef enum { ST_PLAY, ST_DEAD, ST_CLEAR } State;

typedef struct { int mag, reserve; } Ammo;

typedef struct {
    float x, y, vx, vy;          /* x = centre, y = feet */
    int facing;                  /* -1 / +1 */
    float hp;
    int weapon, painkillers;
    Ammo ammo[3];
    float aim;                   /* radians below horizontal, + = down */
    float fire_cd, reload_t, drink_t, dodge_cd, dodge_t, prone_t;
    float coyote, jumpbuf, anim, kick, flash;
    int grounded, dodging;
} Player;

typedef struct {
    int alive, type, facing, burst_left, armor;
    float x, y, vx, hp, hpmax, cd, burst_t, stun, flash, dead_t, anim;
} Enemy;

typedef struct { int alive, owner; float x, y, vx, vy, life, dmg; } Bullet;
typedef struct { float x, y, vx, vy, life; uint32_t col; } Part;

typedef struct {
    State state;
    Player p;
    Enemy e[MAX_ENEMIES];
    Bullet b[MAX_BULLETS];
    Part part[MAX_PARTS];
    int part_i;
    unsigned prev, rng;
    int wave, between, god;
    int kills, headshots;
    float wave_timer, banner;
    float real_t, world_t, play_t, dead_t;
    float ts, bt;                 /* current time scale, bullet-time letterbox amount */
    float hitstop, slow_t, slow_scale, pain_cd, pain_acc, hurt_flash, shake;
    float camx;
} Game;

void game_init(Game *g, unsigned seed);
void game_update(Game *g, unsigned held, float real_dt);
void game_draw(const Game *g, RectFn R);

#endif
