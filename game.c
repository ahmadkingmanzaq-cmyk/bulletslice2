#include "game.h"
#include <math.h>
#include <string.h>

/* ---- tuning, carried over from the Godot prototype (px and seconds) ------ */
#define WALK 58.0f
#define RUN 105.0f
#define AIM_WALK 36.0f
#define ACCEL 700.0f
#define JUMP_V 255.0f
#define GRAV 720.0f
#define DODGE_VX 150.0f
#define DODGE_VY 150.0f
#define DODGE_G 420.0f
#define PAIN_HEAVY 22.0f
#define PI_F 3.14159265f

typedef struct {
    const char *name;
    float dmg, interval; int mag, reserve; float spread; int pellets;
    float speed; int automatic; float reload, life;
} Weapon;

static const Weapon WPN[3] = {
    { "PISTOL",  20.0f, 0.19f, 15,  90, 0.020f, 1, 560.0f, 0, 1.1f, 1.8f  },
    { "SMG",     11.0f, 0.075f, 30, 120, 0.055f, 1, 600.0f, 1, 1.5f, 1.6f },
    { "SHOTGUN",  9.0f, 0.70f,  6,  18, 0.130f, 7, 480.0f, 0, 2.0f, 0.38f },
};

typedef struct {
    float hp, dmg, speed; int bmin, bmax; float interval, spread, range, cd0, cd1;
    int pellets; float bspeed; int armor; uint32_t col;
} EType;

enum { T_THUG, T_RUSHER, T_BREACHER, T_HEAVY };

static const EType ET[4] = {
    {  55.0f, 7.0f,  62.0f, 1, 3, 0.28f, 0.07f, 300.0f, 0.9f, 1.8f, 1, 330.0f, 0, COL(150, 40, 40, 255)  },
    {  30.0f, 4.0f,  96.0f, 2, 4, 0.11f, 0.11f, 270.0f, 0.9f, 1.7f, 1, 350.0f, 0, COL(40, 130, 140, 255) },
    {  60.0f, 5.0f,  78.0f, 1, 1, 1.10f, 0.15f, 165.0f, 1.1f, 2.0f, 6, 300.0f, 0, COL(110, 120, 50, 255) },
    { 120.0f, 5.0f,  46.0f, 5, 9, 0.09f, 0.12f, 310.0f, 1.5f, 2.5f, 1, 350.0f, 1, COL(95, 95, 110, 255)  },
};

/* wave contents: counts of {thug, rusher, breacher, heavy} */
static const int WAVES[NUM_WAVES][4] = { { 3, 0, 0, 0 }, { 2, 2, 0, 0 }, { 0, 2, 2, 1 } };

typedef struct { float x0, x1, y; } Plat;
static const Plat PLATS[2] = { { 200.0f, 330.0f, 196.0f }, { 580.0f, 720.0f, 196.0f } };

/* ---- small helpers -------------------------------------------------------- */
static float frand(Game *g) {
    g->rng ^= g->rng << 13; g->rng ^= g->rng >> 17; g->rng ^= g->rng << 5;
    return (float)(g->rng & 0xFFFFFF) / 16777216.0f;
}
static float frange(Game *g, float a, float b) { return a + (b - a) * frand(g); }
static float move_toward(float v, float t, float d) {
    if (v < t) { v += d; return v > t ? t : v; }
    v -= d; return v < t ? t : v;
}
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static void emit(Game *g, float x, float y, int n, uint32_t col, float spd, float dirx) {
    for (int i = 0; i < n; i++) {
        Part *q = &g->part[g->part_i]; g->part_i = (g->part_i + 1) % MAX_PARTS;
        q->x = x; q->y = y;
        q->vx = dirx * spd * 0.5f + frange(g, -spd, spd) * 0.6f;
        q->vy = frange(g, -spd, spd * 0.2f);
        q->life = frange(g, 0.2f, 0.55f); q->col = col;
    }
}

static void spawn_bullet(Game *g, int owner, float x, float y, float ang, float speed, float dmg, float life) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g->b[i];
        if (b->alive) continue;
        b->alive = 1; b->owner = owner; b->x = x; b->y = y;
        b->vx = cosf(ang) * speed; b->vy = sinf(ang) * speed;
        b->dmg = dmg; b->life = life;
        return;
    }
}

static int alive_enemies(const Game *g) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) n += g->e[i].alive;
    return n;
}

/* ---- init / waves --------------------------------------------------------- */
static void start_ammo(Player *p) {
    for (int i = 0; i < 3; i++) { p->ammo[i].mag = WPN[i].mag; p->ammo[i].reserve = WPN[i].reserve; }
}

void game_init(Game *g, unsigned seed) {
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 0xC0FFEEu;
    g->state = ST_PLAY;
    g->p.x = 160.0f; g->p.y = FLOOR_Y; g->p.facing = 1; g->p.hp = 100.0f;
    g->p.painkillers = 3;
    start_ammo(&g->p);
    g->ts = 1.0f; g->slow_scale = 1.0f;
    g->between = 1; g->wave_timer = 0.8f;
    g->camx = clampf(g->p.x - SCR_W / 2, 0.0f, WORLD_W - SCR_W);
}

static void spawn_wave(Game *g, int w) {
    int n = 0;
    for (int t = 0; t < 4; t++) {
        for (int k = 0; k < WAVES[w][t]; k++, n++) {
            Enemy *e = &g->e[n];
            memset(e, 0, sizeof *e);
            e->alive = 1; e->type = t;
            e->hp = e->hpmax = ET[t].hp; e->armor = ET[t].armor;
            int right = (n % 2) == 0;
            if (right && g->p.x > WORLD_W - 300.0f) right = 0;
            if (!right && g->p.x < 300.0f) right = 1;
            e->x = right ? WORLD_W - 16.0f - n * 12.0f : 16.0f + n * 12.0f;
            e->y = FLOOR_Y;
            e->facing = right ? -1 : 1;
            e->cd = frange(g, 0.8f, 2.2f);
        }
    }
}

static void resupply(Game *g) {
    for (int i = 0; i < 3; i++) {
        g->p.ammo[i].reserve += WPN[i].mag;
        if (g->p.ammo[i].reserve > WPN[i].reserve) g->p.ammo[i].reserve = WPN[i].reserve;
    }
    if (g->p.painkillers < 5) g->p.painkillers++;
}

/* ---- damage --------------------------------------------------------------- */
static void hurt_player(Game *g, float dmg, float dirx) {
    Player *p = &g->p;
    if (g->state != ST_PLAY) return;
    if (!g->god) p->hp -= dmg;
    g->hurt_flash = 1.0f;
    g->shake = 3.4f;
    g->pain_acc += dmg;
    if (g->pain_cd <= 0.0f) {
        g->hitstop = 0.04f + (g->pain_acc >= PAIN_HEAVY ? 0.025f : 0.0f);
        g->slow_t = 0.12f; g->slow_scale = 0.45f;
        g->pain_cd = 0.25f; g->pain_acc = 0.0f;
    }
    if (!p->dodging) p->vx += dirx * 20.0f;
    emit(g, p->x, p->y - 14.0f, 5, COL(160, 20, 20, 255), 70.0f, dirx);
    if (p->hp <= 0.0f) {
        p->hp = 0.0f; g->state = ST_DEAD; g->dead_t = 0.0f;
        p->dodging = 0; p->reload_t = 0.0f;
    }
}

static void hurt_enemy(Game *g, Enemy *e, float dmg, int head, float dirx) {
    if (e->armor) {
        dmg *= 0.5f;
        if (e->hp - dmg <= e->hpmax * 0.5f) { e->armor = 0; emit(g, e->x, e->y - 14.0f, 8, COL(180, 180, 190, 255), 80.0f, dirx); }
    }
    if (head) dmg *= 2.5f;
    e->hp -= dmg; e->flash = 0.08f;
    if (dmg >= 18.0f) e->stun = 0.12f;
    emit(g, e->x, e->y - (head ? 20.0f : 13.0f), head ? 7 : 4, COL(170, 25, 25, 255), 60.0f, dirx);
    if (e->hp <= 0.0f) {
        e->alive = 0; e->dead_t = 0.0f;
        g->kills++; if (head) g->headshots++;
        if (alive_enemies(g) == 0 && g->wave >= NUM_WAVES) { g->slow_t = 0.9f; g->slow_scale = 0.2f; }
        else if (head) { g->slow_t = 0.25f; g->slow_scale = 0.35f; }
    }
}

/* ---- player --------------------------------------------------------------- */
static float shoulder_y(const Player *p) {
    return p->y - (p->dodging ? 10.0f : (p->prone_t > 0.0f ? 6.0f : 17.0f));
}

static void start_reload(Player *p) {
    const Weapon *w = &WPN[p->weapon]; Ammo *a = &p->ammo[p->weapon];
    if (p->reload_t <= 0.0f && a->mag < w->mag && a->reserve > 0) p->reload_t = w->reload;
}

static void update_player(Game *g, unsigned held, unsigned pressed, float dt, float rdt) {
    Player *p = &g->p;
    const Weapon *w = &WPN[p->weapon];
    int dead = (g->state != ST_PLAY);

    if (p->fire_cd > 0) p->fire_cd -= dt;
    if (p->dodge_cd > 0) p->dodge_cd -= dt;
    if (p->prone_t > 0) p->prone_t -= dt;
    if (p->kick > 0) p->kick -= dt;

    int dir = 0;
    if (!dead) {
        if (held & IN_LEFT) dir -= 1;
        if (held & IN_RIGHT) dir += 1;
        if (p->prone_t > 0) dir = 0;

        if (pressed & IN_SWAP) {
            p->weapon = (p->weapon + 1) % 3; p->reload_t = 0.0f; p->fire_cd = 0.25f; w = &WPN[p->weapon];
        }
        if ((pressed & IN_PAIN) && p->drink_t <= 0 && p->painkillers > 0 && p->hp < 100.0f) {
            p->painkillers--; p->drink_t = 0.8f;
        }
        if (p->drink_t > 0) {
            p->drink_t -= dt;
            if (p->drink_t <= 0) { p->hp += 40.0f; if (p->hp > 100.0f) p->hp = 100.0f; }
        }
        if (pressed & IN_JUMP) p->jumpbuf = 0.1f; else p->jumpbuf -= dt;
        p->coyote = p->grounded ? 0.1f : p->coyote - dt;

        if ((pressed & IN_DODGE) && p->grounded && !p->dodging && p->dodge_cd <= 0 && p->prone_t <= 0) {
            int d = dir ? dir : p->facing;
            p->facing = d; p->vx = d * DODGE_VX; p->vy = -DODGE_VY;
            p->dodging = 1; p->dodge_t = 0.0f; p->grounded = 0;
        }
        if (p->dodging) {
            p->dodge_t += dt;
        } else {
            float spd = WALK;
            if (held & IN_SPRINT) spd = RUN;
            if (held & IN_FIRE) spd = AIM_WALK;
            if (p->hp < 25.0f) spd *= 0.72f;
            if (p->drink_t > 0) spd *= 0.5f;
            if (dir) p->facing = dir;
            p->vx = move_toward(p->vx, dir * spd, ACCEL * dt);
            if (p->jumpbuf > 0 && p->coyote > 0 && p->prone_t <= 0) {
                p->vy = -JUMP_V; p->jumpbuf = 0; p->coyote = 0; p->grounded = 0;
            }
        }
        /* aim is rotated in REAL time so bullet-time aiming stays precise */
        float ad = 0;
        if (held & IN_UP) ad -= 1.0f;
        if (held & IN_DOWN) ad += 1.0f;
        p->aim = clampf(p->aim + ad * 2.0f * rdt, -1.1f, 1.1f);
    } else {
        p->vx = move_toward(p->vx, 0, 200.0f * dt);
        p->dodging = 0;
    }

    /* physics */
    float prev_y = p->y;
    float gr = p->dodging ? DODGE_G : GRAV;
    if (!p->dodging && p->vy < 0 && !(held & IN_JUMP)) gr *= 1.8f;
    p->vy += gr * dt;
    p->y += p->vy * dt;
    p->x += p->vx * dt;
    p->grounded = 0;
    if (p->y >= FLOOR_Y) { p->y = FLOOR_Y; p->vy = 0; p->grounded = 1; }
    else if (p->vy >= 0) {
        for (int i = 0; i < 2; i++) {
            if (p->x > PLATS[i].x0 - 3 && p->x < PLATS[i].x1 + 3 && prev_y <= PLATS[i].y + 0.5f && p->y >= PLATS[i].y) {
                p->y = PLATS[i].y; p->vy = 0; p->grounded = 1;
            }
        }
    }
    p->x = clampf(p->x, 6.0f, WORLD_W - 6.0f);
    if (p->dodging && p->grounded && p->dodge_t > 0.1f) {
        p->dodging = 0; p->prone_t = 0.45f; p->dodge_cd = 0.5f; p->vx = 0;
    }
    p->anim += fabsf(p->vx) * dt / 12.0f;
    if (dead) return;

    /* reload + fire */
    Ammo *a = &p->ammo[p->weapon];
    if (p->reload_t > 0) {
        p->reload_t -= dt;
        if (p->reload_t <= 0) {
            int need = w->mag - a->mag, take = need < a->reserve ? need : a->reserve;
            a->mag += take; a->reserve -= take;
        }
    } else if (p->drink_t <= 0 && p->fire_cd <= 0 && ((w->automatic && (held & IN_FIRE)) || (pressed & IN_FIRE))) {
        if (a->mag > 0) {
            float dx = p->facing * cosf(p->aim), dy = sinf(p->aim);
            float ang = atan2f(dy, dx);
            float mx = p->x + dx * 12.0f, my = shoulder_y(p) + dy * 12.0f;
            for (int i = 0; i < w->pellets; i++)
                spawn_bullet(g, 0, mx, my, ang + frange(g, -w->spread, w->spread), w->speed, w->dmg, w->life);
            a->mag--; p->fire_cd = w->interval; p->kick = 0.06f;
            if (w->pellets > 1) g->shake = 2.0f; else if (g->shake < 0.8f) g->shake = 0.8f;
            emit(g, mx, my, 2, COL(255, 210, 120, 255), 50.0f, dx);
        } else if (a->reserve > 0) start_reload(p);
        else p->fire_cd = 0.3f;
    }
    if ((pressed & IN_RELOAD)) start_reload(p);
}

/* ---- enemies -------------------------------------------------------------- */
static void update_enemies(Game *g, float dt) {
    Player *p = &g->p;
    int player_alive = g->state == ST_PLAY;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->e[i];
        if (!e->alive) { e->dead_t += dt; continue; }
        const EType *t = &ET[e->type];
        if (e->flash > 0) e->flash -= dt;
        if (e->stun > 0) { e->stun -= dt; continue; }
        float dx = p->x - e->x, adx = fabsf(dx);
        e->facing = dx >= 0 ? 1 : -1;
        float rng = t->range < 230.0f ? t->range : 230.0f;
        float want = 0;
        if (player_alive) {
            if (adx > rng * 0.75f) want = e->facing * t->speed;
            else if (adx < rng * 0.3f && e->type != T_RUSHER) want = -e->facing * t->speed * 0.5f;
        }
        e->vx = move_toward(e->vx, want, 400.0f * dt);
        e->x = clampf(e->x + e->vx * dt, 8.0f, WORLD_W - 8.0f);
        e->anim += fabsf(e->vx) * dt / 12.0f;
        e->cd -= dt;
        if (e->burst_left > 0) {
            e->burst_t -= dt;
            if (e->burst_t <= 0 && player_alive) {
                float sx = e->x + e->facing * 8.0f, sy = e->y - 16.0f;
                float ty = p->y - (p->dodging ? 8.0f : 14.0f);
                float ang = atan2f(ty - sy, p->x - sx);
                for (int k = 0; k < t->pellets; k++)
                    spawn_bullet(g, 1, sx, sy, ang + frange(g, -t->spread, t->spread), t->bspeed, t->dmg, 1.4f);
                e->flash = 0.05f;
                e->burst_left--;
                e->burst_t = t->interval;
                if (e->burst_left == 0) e->cd = frange(g, t->cd0, t->cd1);
            } else if (!player_alive) e->burst_left = 0;
        } else if (e->cd <= 0 && player_alive && adx < rng) {
            e->burst_left = t->bmin + (int)(frand(g) * (t->bmax - t->bmin + 1));
            if (e->burst_left > t->bmax) e->burst_left = t->bmax;
            e->burst_t = 0.25f;   /* wind-up: you get a quarter second of warning */
        }
    }
}

/* ---- bullets -------------------------------------------------------------- */
static void update_bullets(Game *g, float dt) {
    Player *p = &g->p;
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g->b[i];
        if (!b->alive) continue;
        float speed = sqrtf(b->vx * b->vx + b->vy * b->vy);
        int steps = (int)ceilf(speed * dt / 3.0f); if (steps < 1) steps = 1;
        float sub = dt / steps;
        for (int s = 0; s < steps && b->alive; s++) {
            b->x += b->vx * sub; b->y += b->vy * sub; b->life -= sub;
            if (b->life <= 0 || b->x < 0 || b->x > WORLD_W || b->y < -40 || b->y > FLOOR_Y) {
                if (b->y > FLOOR_Y) emit(g, b->x, FLOOR_Y, 2, COL(120, 120, 130, 255), 40.0f, 0);
                b->alive = 0; break;
            }
            if (b->owner == 0) {
                for (int k = 0; k < MAX_ENEMIES; k++) {
                    Enemy *e = &g->e[k];
                    if (!e->alive) continue;
                    if (b->x > e->x - 5 && b->x < e->x + 5 && b->y > e->y - 24 && b->y < e->y) {
                        int head = b->y < e->y - 19.0f;
                        hurt_enemy(g, e, b->dmg, head, b->vx > 0 ? 1.0f : -1.0f);
                        b->alive = 0; break;
                    }
                }
            } else if (g->state == ST_PLAY) {
                float top = p->y - (p->dodging ? 14.0f : (p->prone_t > 0 ? 7.0f : 24.0f));
                if (b->x > p->x - 5 && b->x < p->x + 5 && b->y > top && b->y < p->y) {
                    hurt_player(g, b->dmg, b->vx > 0 ? 1.0f : -1.0f);
                    b->alive = 0;
                }
            }
        }
    }
}

/* ---- main update ---------------------------------------------------------- */
void game_update(Game *g, unsigned held, float rdt) {
    unsigned pressed = held & ~g->prev;
    g->prev = held;
    if (rdt > 0.05f) rdt = 0.05f;
    if (rdt < 0.0f) rdt = 0.0f;
    g->real_t += rdt;

    if (g->state != ST_PLAY && g->dead_t > 0.8f && (pressed & IN_START)) {
        unsigned keep = held;
        game_init(g, g->rng + 7919u);
        g->prev = keep;
        return;
    }

    float ts = 1.0f;
    if (g->hitstop > 0) { g->hitstop -= rdt; ts = 0.0f; }
    else {
        if (g->p.dodging) ts = 0.28f;
        if (g->slow_t > 0) { g->slow_t -= rdt; if (g->slow_scale < ts) ts = g->slow_scale; }
        if (g->state == ST_DEAD) ts = 0.25f;
    }
    g->ts = ts;
    float dt = rdt * ts;

    if (g->hurt_flash > 0) g->hurt_flash -= rdt * 2.5f;
    if (g->pain_cd > 0) g->pain_cd -= rdt;
    if (g->pain_acc > 0) g->pain_acc -= 40.0f * rdt;
    g->shake -= g->shake * 8.0f * rdt; if (g->shake < 0.05f) g->shake = 0;
    g->bt += ((g->p.dodging ? 1.0f : 0.0f) - g->bt) * clampf(8.0f * rdt, 0, 1);
    if (g->banner > 0) g->banner -= rdt;
    if (g->state != ST_PLAY) g->dead_t += rdt;

    if (dt > 0) {
        g->world_t += dt;
        if (g->state == ST_PLAY) g->play_t += dt;
        update_player(g, held, pressed, dt, rdt);
        update_enemies(g, dt);
        update_bullets(g, dt);
        for (int i = 0; i < MAX_PARTS; i++) {
            Part *q = &g->part[i];
            if (q->life <= 0) continue;
            q->life -= dt; q->vy += 500.0f * dt; q->x += q->vx * dt; q->y += q->vy * dt;
            if (q->y > FLOOR_Y) { q->y = FLOOR_Y; q->vy = 0; q->vx *= 0.6f; }
        }
        if (g->p.hp > 0 && g->god) g->p.hp = 100.0f;
    }

    /* wave director runs on real time so slow-mo does not stall it */
    if (g->state == ST_PLAY && alive_enemies(g) == 0) {
        if (!g->between) {
            g->between = 1;
            g->wave_timer = g->wave == 0 ? 0.8f : (g->wave >= NUM_WAVES ? 1.6f : 2.4f);
            if (g->wave > 0 && g->wave < NUM_WAVES) resupply(g);
        }
        g->wave_timer -= rdt;
        if (g->wave_timer <= 0) {
            if (g->wave >= NUM_WAVES) { g->state = ST_CLEAR; g->dead_t = 0; }
            else { spawn_wave(g, g->wave); g->wave++; g->between = 0; g->banner = 2.0f; }
        }
    }

    /* camera */
    float target = g->p.x - SCR_W / 2 + g->p.facing * 30.0f;
    g->camx += (target - g->camx) * clampf(6.0f * rdt, 0, 1);
    g->camx = clampf(g->camx, 0.0f, WORLD_W - SCR_W);
}

/* ======================= rendering (rectangles only) ======================= */
/* 3x5 pixel font, row-major, '1' = ink */
static const char *FONT[] = {
 "111101101101111","010110010010111","111001111100111","111001111001111","101101111001001",
 "111100111001111","111100111101111","111001001001001","111101111101111","111101111001111", /* 0-9 */
 "010101111101101","110101110101110","011100100100011","110101101101110","111100110100111",
 "111100110100100","011100101101011","101101111101101","111010010010111","001001001101010",
 "101101110101101","100100100100111","101111111101101","110101101101101","010101101101010",
 "110101110100100","010101101111011","110101110101101","011100010001110","111010010010010",
 "101101101101111","101101101101010","101101111111101","101101010101101","101101010010010",
 "111001010100111",                                                                          /* A-Z */
 "000010000010000","001001010100100","000000111000000","000000000000010"                     /* : / - . */
};
static int glyph(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    switch (c) { case ':': return 36; case '/': return 37; case '-': return 38; case '.': return 39; }
    return -1;
}
static float text_w(const char *s, float sc) { int n = 0; while (*s++) n++; return n * 4 * sc - sc; }
static void text(RectFn R, float x, float y, float sc, const char *s, uint32_t col) {
    for (; *s; s++, x += 4 * sc) {
        int gi = glyph(*s);
        if (gi < 0) continue;
        const char *gl = FONT[gi];
        for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++)
            if (gl[r * 3 + c] == '1') R(x + c * sc, y + r * sc, sc, sc, col);
    }
}
static void itoa_(int v, char *out) {
    char tmp[12]; int n = 0;
    if (v < 0) v = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}
static void cat_int(char *dst, int v) { char t[12]; itoa_(v, t); strcat(dst, t); }

static void line(RectFn R, float x0, float y0, float x1, float y1, float th, uint32_t col) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    int n = (int)(len / th) + 1;
    for (int i = 0; i <= n; i++) { float t = (float)i / n; R(x0 + dx * t - th / 2, y0 + dy * t - th / 2, th, th, col); }
}

static uint32_t fade(uint32_t col, float a) {
    uint32_t al = (uint32_t)((float)(col >> 24) * clampf(a, 0, 1));
    return (col & 0x00FFFFFFu) | (al << 24);
}

static void draw_actor(RectFn R, float sx, float fy, int facing, uint32_t body, uint32_t skin, uint32_t pants,
                       float phase, float aim, int pose /*0 stand 1 dodge 2 prone 3 dead*/, int armor, int gun, float flash, float alpha) {
    float f = (float)facing;
    body = fade(body, alpha); skin = fade(skin, alpha); pants = fade(pants, alpha);
    if (flash > 0) { body = fade(COL(255, 255, 255, 255), alpha); }
    if (pose == 0) {
        float sw = sinf(phase * 6.2832f) * 2.5f;
        R(sx - 3 + sw, fy - 9, 3, 9, pants); R(sx + 0 - sw, fy - 9, 3, 9, pants);
        R(sx - 4, fy - 19, 8, 10, body);
        if (armor) R(sx - 4, fy - 18, 8, 6, fade(COL(40, 40, 50, 255), alpha));
        R(sx - 2.5f, fy - 24, 5, 5, skin);
    } else if (pose == 1) {
        R(sx - 8 * f - (f > 0 ? 0 : 3), fy - 11, 3, 6, pants);
        R(sx - 7, fy - 13, 14, 7, body);
        R(sx + 6 * f - 2, fy - 14, 5, 5, skin);
    } else {
        R(sx - 9, fy - 4, 18, 4, body); R(sx + 8 * f - 2, fy - 6, 5, 5, skin);
    }
    if (gun && pose != 3) {
        float sy = fy - (pose == 0 ? 17 : (pose == 1 ? 10 : 6));
        float dx = f * cosf(aim), dy = sinf(aim);
        line(R, sx + dx * 3, sy + dy * 3, sx + dx * 11, sy + dy * 11, 2, fade(COL(30, 30, 34, 255), alpha));
    }
}

void game_draw(const Game *g, RectFn R) {
    float shx = sinf(g->real_t * 90.0f) * g->shake, shy = cosf(g->real_t * 77.0f) * g->shake;
    float cx = g->camx - shx;

    /* sky */
    for (int i = 0; i < 8; i++)
        R(0, i * 34.0f, SCR_W, 34, COL(8 + i * 3, 10 + i * 4, 22 + i * 5, 255));
    /* skyline, parallax */
    for (int i = 0; i < 16; i++) {
        float w = 44 + (i * 37 % 29), h = 60 + (i * 53 % 70);
        float bx = i * 66.0f - 20 - g->camx * 0.35f;
        R(bx, FLOOR_Y - h, w, h, COL(14, 16, 28, 255));
        for (int k = 0; k < 6; k++) {
            int on = ((i * 7 + k * 3 + (int)(g->world_t * 0.5f)) % 5) == 0;
            if (on) R(bx + 5 + (k % 3) * 12, FLOOR_Y - h + 8 + (k / 3) * 14, 4, 5, COL(190, 170, 90, 150));
        }
    }
    /* floor + platforms */
    R(0, FLOOR_Y + shy, SCR_W, SCR_H - FLOOR_Y, COL(18, 20, 26, 255));
    R(0, FLOOR_Y + shy, SCR_W, 1, COL(70, 80, 100, 255));
    for (int i = 0; i < 2; i++) {
        float x = PLATS[i].x0 - cx;
        R(x, PLATS[i].y + shy, PLATS[i].x1 - PLATS[i].x0, 3, COL(60, 66, 80, 255));
        R(x + 4, PLATS[i].y + 3 + shy, 3, FLOOR_Y - PLATS[i].y - 3, COL(30, 34, 44, 255));
        R(x + PLATS[i].x1 - PLATS[i].x0 - 7, PLATS[i].y + 3 + shy, 3, FLOOR_Y - PLATS[i].y - 3, COL(30, 34, 44, 255));
    }
    /* rain behind */
    for (int i = 0; i < 40; i++) {
        float y = fmodf(i * 53.0f + g->world_t * (380.0f + (i % 5) * 40.0f), SCR_H + 20) - 10;
        float x = fmodf(i * 97.0f - y * 0.15f + SCR_W + 40, SCR_W + 40);
        R(x, y, 1, 6, COL(120, 140, 180, 45));
    }
    /* corpses */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g->e[i];
        if (e->alive || e->type < 0 || e->hpmax <= 0) continue;
        float a = 1.0f - clampf((e->dead_t - 3.0f) / 2.0f, 0, 1);
        if (a <= 0) continue;
        draw_actor(R, e->x - cx, e->y + shy, e->facing, ET[e->type].col, COL(200, 160, 120, 255), COL(30, 30, 36, 255), 0, 0, 3, 0, 0, 0, a);
    }
    /* enemies */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g->e[i];
        if (!e->alive) continue;
        draw_actor(R, e->x - cx, e->y + shy, e->facing, ET[e->type].col, COL(200, 160, 120, 255), COL(30, 30, 36, 255),
                   e->anim, 0.0f, 0, e->armor, 1, e->flash, 1.0f);
        if (e->burst_left > 0 && e->burst_t > 0.15f)       /* wind-up tell */
            R(e->x - cx - 1, e->y - 32 + shy, 2, 4, COL(255, 60, 40, 255));
        if (e->flash > 0 && e->burst_left >= 0)
            R(e->x - cx + e->facing * 12 - 2, e->y - 17 + shy, 4, 3, COL(255, 220, 140, 255));
    }
    /* player */
    {
        const Player *p = &g->p;
        int pose = g->state != ST_PLAY ? 3 : (p->dodging ? 1 : (p->prone_t > 0 ? 2 : 0));
        float px = p->x - cx, py = p->y + shy;
        draw_actor(R, px, py, p->facing, COL(52, 60, 78, 255), COL(214, 170, 130, 255), COL(26, 28, 36, 255),
                   p->anim, p->aim, pose, 0, g->state == ST_PLAY, 0, 1.0f);
        if (g->state == ST_PLAY && p->kick > 0)
            R(px + p->facing * (14 * cosf(p->aim)) - 2, shoulder_y(p) + 14 * sinf(p->aim) + shy - 1, 4, 3, COL(255, 230, 150, 255));
        if (g->state == ST_PLAY) {      /* laser dots */
            float dx = p->facing * cosf(p->aim), dy = sinf(p->aim), sy = shoulder_y(p);
            for (int k = 0; k < 6; k++)
                R(px + dx * (24 + k * 9) - 0.5f, sy + dy * (24 + k * 9) + shy - 0.5f, 1, 1, COL(255, 50, 50, 140));
        }
    }
    /* bullets */
    for (int i = 0; i < MAX_BULLETS; i++) {
        const Bullet *b = &g->b[i];
        if (!b->alive) continue;
        uint32_t c = b->owner == 0 ? COL(255, 235, 170, 255) : COL(255, 120, 80, 255);
        for (int k = 0; k < 4; k++)
            R(b->x - b->vx * 0.006f * k - cx - 0.5f, b->y - b->vy * 0.006f * k + shy - 0.5f, 2, 2, fade(c, 1.0f - k * 0.22f));
    }
    /* particles */
    for (int i = 0; i < MAX_PARTS; i++) {
        const Part *q = &g->part[i];
        if (q->life <= 0) continue;
        R(q->x - cx, q->y + shy, 2, 2, fade(q->col, q->life * 3.0f));
    }
    /* rain front */
    for (int i = 40; i < 56; i++) {
        float y = fmodf(i * 61.0f + g->world_t * 620.0f, SCR_H + 20) - 10;
        float x = fmodf(i * 131.0f - y * 0.2f + SCR_W + 40, SCR_W + 40);
        R(x, y, 1, 9, COL(170, 190, 230, 70));
    }

    /* ---- screen effects ---- */
    if (g->bt > 0.02f) {
        float bar = 14.0f * g->bt;
        R(0, 0, SCR_W, bar, COL(0, 0, 0, 255)); R(0, SCR_H - bar, SCR_W, bar, COL(0, 0, 0, 255));
        R(0, 0, SCR_W, SCR_H, COL(60, 90, 160, (int)(22 * g->bt)));
    }
    float low = g->p.hp < 28.0f && g->state == ST_PLAY ? (0.5f + 0.5f * sinf(g->real_t * 9.0f)) : 0.0f;
    float vig = clampf(g->hurt_flash * 0.55f + low * 0.3f, 0, 1);
    if (vig > 0.01f) {
        int a = (int)(150 * vig);
        R(0, 0, SCR_W, 10, COL(170, 0, 0, a)); R(0, SCR_H - 10, SCR_W, 10, COL(170, 0, 0, a));
        R(0, 0, 10, SCR_H, COL(170, 0, 0, a)); R(SCR_W - 10, 0, 10, SCR_H, COL(170, 0, 0, a));
    }

    /* ---- HUD ---- */
    const Player *p = &g->p;
    R(8, 8, 62, 7, COL(0, 0, 0, 160));
    R(9, 9, 60.0f * (p->hp / 100.0f), 5, p->hp < 28 ? COL(220, 40, 40, 255) : COL(235, 235, 235, 255));
    for (int i = 0; i < p->painkillers; i++) {
        R(9 + i * 7, 18, 5, 8, COL(0, 0, 0, 160)); R(10 + i * 7, 19, 3, 6, COL(230, 200, 60, 255));
    }
    if (p->drink_t > 0) R(9, 28, 60.0f * (1.0f - p->drink_t / 0.8f), 2, COL(230, 200, 60, 255));
    char buf[40];
    text(R, 8, SCR_H - 30, 1, WPN[p->weapon].name, COL(200, 200, 210, 255));
    buf[0] = 0; cat_int(buf, p->ammo[p->weapon].mag); strcat(buf, "/"); cat_int(buf, p->ammo[p->weapon].reserve);
    text(R, 8, SCR_H - 22, 2, buf, p->reload_t > 0 || p->ammo[p->weapon].mag == 0 ? COL(240, 90, 70, 255) : COL(255, 255, 255, 255));
    if (p->reload_t > 0) R(8, SCR_H - 8, 50.0f * (1.0f - p->reload_t / WPN[p->weapon].reload), 2, COL(240, 90, 70, 255));
    buf[0] = 0; strcat(buf, "WAVE "); cat_int(buf, g->wave); strcat(buf, "/"); cat_int(buf, NUM_WAVES);
    text(R, SCR_W - 8 - text_w(buf, 1), 8, 1, buf, COL(200, 200, 210, 255));
    buf[0] = 0; strcat(buf, "HOSTILES "); cat_int(buf, alive_enemies(g));
    text(R, SCR_W - 8 - text_w(buf, 1), 16, 1, buf, COL(200, 200, 210, 255));

    if (g->banner > 0 && g->state == ST_PLAY) {
        buf[0] = 0; strcat(buf, "WAVE "); cat_int(buf, g->wave);
        text(R, (SCR_W - text_w(buf, 4)) / 2, 70, 4, buf, fade(COL(255, 255, 255, 255), g->banner));
    }
    if (g->state == ST_DEAD && g->dead_t > 0.5f) {
        R(0, 0, SCR_W, SCR_H, COL(0, 0, 0, (int)(170 * clampf((g->dead_t - 0.5f) / 1.0f, 0, 1))));
        text(R, (SCR_W - text_w("YOU DIED", 5)) / 2, 90, 5, "YOU DIED", COL(220, 40, 40, 255));
        if (g->dead_t > 0.8f) text(R, (SCR_W - text_w("PRESS START", 2)) / 2, 150, 2, "PRESS START", COL(220, 220, 230, 255));
    }
    if (g->state == ST_CLEAR) {
        R(0, 0, SCR_W, SCR_H, COL(0, 0, 0, 190));
        text(R, (SCR_W - text_w("AREA CLEAR", 5)) / 2, 70, 5, "AREA CLEAR", COL(255, 255, 255, 255));
        buf[0] = 0; strcat(buf, "KILLS "); cat_int(buf, g->kills);
        text(R, (SCR_W - text_w(buf, 2)) / 2, 125, 2, buf, COL(210, 210, 220, 255));
        buf[0] = 0; strcat(buf, "HEADSHOTS "); cat_int(buf, g->headshots);
        text(R, (SCR_W - text_w(buf, 2)) / 2, 142, 2, buf, COL(210, 210, 220, 255));
        int secs = (int)g->play_t;
        buf[0] = 0; strcat(buf, "TIME "); cat_int(buf, secs / 60); strcat(buf, ":"); if (secs % 60 < 10) strcat(buf, "0"); cat_int(buf, secs % 60);
        text(R, (SCR_W - text_w(buf, 2)) / 2, 159, 2, buf, COL(210, 210, 220, 255));
        if (g->dead_t > 0.8f) text(R, (SCR_W - text_w("PRESS START", 2)) / 2, 195, 2, "PRESS START", COL(220, 220, 230, 255));
    }
}
