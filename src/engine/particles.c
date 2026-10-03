// particles.c - particle pool and emitters.
#include "particles.h"
#include "config.h"
#include "anim.h"
#include "sprites.h"

typedef struct {
    u8 active;
    u16 life;
    fixed x, y, vx, vy, gravity;
    AnimPlayer anim;
} Particle;

typedef struct {
    const ParticleData *data;
    s32 x, y;
    u16 timer;
} Emitter;

static Particle s_parts[MAX_PARTICLES];
static Emitter s_emitters[MAX_EMITTERS];
static int s_next;

void particles_clear(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) s_parts[i].active = 0;
    for (int i = 0; i < MAX_EMITTERS; i++) s_emitters[i].data = NULL;
}

void particles_emit_one(const ParticleData *d, s32 x, s32 y) {
    Particle *p = NULL;
    for (int i = 0; i < MAX_PARTICLES; i++) {           // reuse the oldest slot when full
        int k = (s_next + i) % MAX_PARTICLES;
        if (!s_parts[k].active) { p = &s_parts[k]; s_next = k + 1; break; }
    }
    if (!p) { p = &s_parts[s_next % MAX_PARTICLES]; s_next++; }
    s32 angle = rng_range(d->angle_min, d->angle_max);
    fixed speed = rng_range(d->speed_min, d->speed_max);
    p->active = 1;
    p->life = d->lifetime;
    p->x = fx_from_int(x);
    p->y = fx_from_int(y);
    // |cos| <= 1.0, so a plain 32-bit multiply can't overflow (fx_mul's 64-bit one is a slow
    // library call in Thumb code, and an aura pulse emits 16 of these at once).
    p->vx = (fx_cos_deg(angle) * speed) >> FX_SHIFT;
    p->vy = -((fx_sin_deg(angle) * speed) >> FX_SHIFT);  // 90 degrees = up
    p->gravity = d->gravity;
    anim_start(&p->anim, d->anim, NULL);
}

void particles_burst(const ParticleData *d, s32 x, s32 y) {
    int n = d->mode == PTC_BURST ? d->count : 1;
    for (int i = 0; i < n; i++) particles_emit_one(d, x, y);
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
        if (--p->life == 0) { p->active = 0; continue; }
        p->vy += p->gravity;
        p->x += p->vx;
        p->y += p->vy;
        anim_step(&p->anim, NULL);
    }
}

void particles_draw(s32 cx, s32 cy) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &s_parts[i];
        if (!p->active || !p->anim.anim) continue;
        const AnimFrame *f = anim_frame(&p->anim);
        const SpriteData *s = p->anim.anim->sprite;
        // A particle's position is the center of its sprite.
        sprites_draw(s, f->frame, fx_to_int(p->x) - s->width / 2 - cx, fx_to_int(p->y) - s->height / 2 - cy, f->flip);
    }
}
