// particles.c - particle pool and emitters.
#include "particles.h"
#include "config.h"
#include "anim.h"
#include "sprites.h"

typedef struct {
    u8 active;
    u8 front, on_top;           // front: drawn in front of the actors this frame (on_top once `behind` is over)
    u8 with_anim;               // 1: the animation is sped up with the flight
    u16 wait;                   // frames left hidden before it starts, + 1 (0 = not waiting)
    u16 behind;                 // frames left drawn behind the actors (coming out of a move's user)
    u16 life;
    u16 steps, frames, acc;     // a sped-up flight: `steps` steps spread over `frames` frames (0 = one a frame)
    u16 left;                   // steps of a sped-up flight still to go (then it stays where it is)
    u16 turn;                   // steps until it flies at vx2, vy2 instead (0 = never)
    fixed x, y, vx, vy, gravity, vx2, vy2;
    AnimPlayer anim;
} Particle;

typedef struct {
    const ParticleData *data;
    s32 x, y;
    u16 timer;
} Emitter;

EWRAM_BSS static Particle s_parts[MAX_PARTICLES];    // EWRAM (IWRAM is full); cleared on scene load
static Emitter s_emitters[MAX_EMITTERS];
static int s_next;

void particles_clear(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) s_parts[i].active = 0;
    for (int i = 0; i < MAX_EMITTERS; i++) s_emitters[i].data = NULL;
}

void particles_aim(const ParticleData *d, int i, s32 x, s32 y, ParticleShot *s) {
    s32 angle = d->even && d->count > 1                 // even: one every (max - min + 1) / count degrees
        ? d->angle_min + (d->angle_max - d->angle_min + 1) * i / d->count
        : rng_range(d->angle_min, d->angle_max);
    fixed speed = rng_range(d->speed_min, d->speed_max);
    s->data = d;
    s->x = x;
    s->y = y;
    // |cos| <= 1.0, so a plain 32-bit multiply can't overflow (fx_mul's 64-bit one is a slow
    // library call in Thumb code, and an aura pulse emits 16 of these at once).
    s->vx = (fx_cos_deg(angle) * speed) >> FX_SHIFT;
    s->vy = -((fx_sin_deg(angle) * speed) >> FX_SHIFT);  // 90 degrees = up
    s->life = d->lifetime;
}

// A free slot (the oldest one when the pool is full), set up as a plain particle: shown at once,
// one step a frame, in front of the actors when on_top.
static Particle *launch(const AnimationData *a, u8 on_top, fixed gravity, s32 x, s32 y, fixed vx, fixed vy, u16 life) {
    Particle *p = NULL;
    for (int i = 0; i < MAX_PARTICLES; i++) {           // reuse the oldest slot when full
        int k = (s_next + i) % MAX_PARTICLES;
        if (!s_parts[k].active) { p = &s_parts[k]; s_next = k + 1; break; }
    }
    if (!p) { p = &s_parts[s_next % MAX_PARTICLES]; s_next++; }
    p->active = 1;
    p->front = p->on_top = on_top;
    p->with_anim = 0;
    p->wait = p->behind = 0;
    p->life = life;
    p->steps = p->frames = p->acc = p->left = p->turn = 0;
    p->x = fx_from_int(x);
    p->y = fx_from_int(y);
    p->vx = vx;
    p->vy = vy;
    p->gravity = gravity;
    p->vx2 = p->vy2 = 0;
    anim_start(&p->anim, a, NULL);
    return p;
}

static Particle *launch_shot(const ParticleShot *s) {
    const ParticleData *d = s->data;
    return launch(d->anim, d->on_top, d->gravity, s->x, s->y, s->vx, s->vy, s->life);
}

void particles_shoot(const ParticleShot *s) { launch_shot(s); }

void particles_shoot_late(const ParticleShot *s, int wait, int behind) {
    int steps = s->life - 1;                            // it is drawn after steps 1 .. life - 1
    if (steps < 1) { launch_shot(s); return; }
    if (wait > steps - 1) wait = steps - 1;             // it shows at least its last frame
    if (wait < 0) wait = 0;
    Particle *p = launch_shot(s);
    int frames = steps - wait;
    p->wait = (u16)(wait ? wait + 1 : 0);              // + the frame it starts in (see particles_update)
    p->life = (u16)(frames + 1);
    p->steps = p->left = (u16)steps;
    p->frames = (u16)frames;
    p->with_anim = 1;
    if (behind) {
        p->behind = (u16)frames;
        p->front = 0;
    }
}

void particles_shoot_out(const ParticleShot *s, int frames) {
    int steps = s->life - 1;
    Particle *p = launch_shot(s);
    if (frames <= 0) return;
    p->behind = (u16)frames;
    p->front = 0;
    if (frames < steps) {
        p->steps = p->left = (u16)steps;
        p->frames = (u16)frames;
    }
}

void particles_travel(const AnimationData *a, s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, int t1, int t2) {
    if (!a || t1 + t2 <= 0) return;
    fixed vx2 = t2 > 0 ? ((x2 - x1) << FX_SHIFT) / t2 : 0;
    fixed vy2 = t2 > 0 ? ((y2 - y1) << FX_SHIFT) / t2 : 0;
    Particle *p;
    if (t1 > 0) {                                       // out from behind its user, then on its way
        p = launch(a, 1, 0, x0, y0, ((x1 - x0) << FX_SHIFT) / t1, ((y1 - y0) << FX_SHIFT) / t1, (u16)(t1 + t2 + 1));
        p->turn = (u16)t1;
        p->vx2 = vx2;
        p->vy2 = vy2;
    } else {
        p = launch(a, 1, 0, x1, y1, vx2, vy2, (u16)(t2 + 1));
    }
    p->behind = (u16)(x1 == x2 && y1 == y2 ? t1 + t2 : t1);     // nowhere to fly: it stays behind
    p->front = p->behind ? 0 : 1;
}

// Emit particle number i of a burst.
static void emit(const ParticleData *d, int i, s32 x, s32 y) {
    ParticleShot s;
    particles_aim(d, i, x, y, &s);
    particles_shoot(&s);
}

void particles_emit_one(const ParticleData *d, s32 x, s32 y) { emit(d, 0, x, y); }

void particles_burst(const ParticleData *d, s32 x, s32 y) {
    int n = d->mode == PTC_BURST ? d->count : 1;
    for (int i = 0; i < n; i++) emit(d, i, x, y);
}

void particles_place(const ParticleData *d, s32 x, s32 y) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (s_emitters[i].data) continue;
        s_emitters[i].data = d;
        s_emitters[i].x = x;
        s_emitters[i].y = y;
        s_emitters[i].timer = 0;
        return;
    }
}

void particles_update(void) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        Emitter *e = &s_emitters[i];
        if (!e->data) continue;
        if (e->timer == 0) {
            particles_burst(e->data, e->x, e->y);
            e->timer = e->data->mode == PTC_STREAM ? e->data->rate : e->data->lifetime;
        }
        e->timer--;
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &s_parts[i];
        if (!p->active) continue;
        if (p->wait && --p->wait) continue;            // hidden; it starts (and shows) when this reaches 0
        if (--p->life == 0) { p->active = 0; continue; }
        p->front = p->behind ? 0 : p->on_top;
        if (p->behind) p->behind--;
        int n = 1;
        if (p->frames) {                                // sped up: `steps` steps spread over `frames` frames
            n = 0;
            if (p->left) {
                p->acc += p->steps;
                while (p->acc >= p->frames) { p->acc -= p->frames; n++; }
                if (n > p->left) n = p->left;
                p->left -= n;
            }
        }
        for (int k = 0; k < n; k++) {
            p->vy += p->gravity;
            p->x += p->vx;
            p->y += p->vy;
            if (p->turn && --p->turn == 0) { p->vx = p->vx2; p->vy = p->vy2; }
            if (p->with_anim) anim_step(&p->anim, NULL);
        }
        if (!p->with_anim) anim_step(&p->anim, NULL);
    }
}

void particles_draw(s32 cx, s32 cy, int front) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &s_parts[i];
        if (!p->active || p->wait || !p->anim.anim || p->front != front) continue;
        const AnimFrame *f = anim_frame(&p->anim);
        const SpriteData *s = p->anim.anim->sprite;
        // A particle's position is the center of its sprite.
        sprites_draw(s, f->frame, fx_to_int(p->x) - s->width / 2 - cx, fx_to_int(p->y) - s->height / 2 - cy, f->flip);
    }
}
