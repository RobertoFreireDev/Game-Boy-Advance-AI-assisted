// particles.h - particle pool (dust, sparks, splashes) and scene-placed emitters.
#ifndef ENGINE_PARTICLES_H
#define ENGINE_PARTICLES_H

#include "data.h"

// One particle picked but not sent off yet: its start (world pixels), speed (px per tick) and
// life (ticks; the particle's lifetime unless the caller cuts it short).
typedef struct {
    const ParticleData *data;
    s32 x, y;
    fixed vx, vy;
    u16 life;
} ParticleShot;

// Remove every particle and emitter (scene change).
void particles_clear(void);
// Pick one particle's direction and speed (so a move can see where it will fly first): random,
// or for an even burst set by i, its place in the burst (0 .. count - 1).
void particles_aim(const ParticleData *p, int i, s32 x, s32 y, ParticleShot *out);
// Send off a particle picked by particles_aim.
void particles_shoot(const ParticleShot *s);
// Send it off late (a move's effect landing, step 3): hidden for `wait` frames, then it plays its
// whole flight and animation sped up, so it still ends the frame it would have. behind = 1: drawn
// behind the actors all along (it lies over its user's body).
void particles_shoot_late(const ParticleShot *s, int wait, int behind);
// Send it off out of its user (steps 1-2): drawn behind the actors while it flies its whole way in
// `frames` frames, then it stays there in front of them; its animation keeps its own pace.
void particles_shoot_out(const ParticleShot *s, int frames);
// A move's travel picture (steps 1-2): animation `a` slides from (x0, y0) to (x1, y1) in t1 frames
// behind the actors (coming out of its user), then flies on to (x2, y2) in t2 frames in front of
// them, and is gone. With nowhere to fly ((x1, y1) = (x2, y2)) it stays behind them.
void particles_travel(const AnimationData *a, s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, int t1, int t2);
// Emit one particle at a world position.
void particles_emit_one(const ParticleData *p, s32 x, s32 y);
// Emit a whole burst (count particles; one particle for stream emitters).
void particles_burst(const ParticleData *p, s32 x, s32 y);
// Place a permanent emitter in the scene (stream: one every 'rate' ticks; burst: repeats).
void particles_place(const ParticleData *p, s32 x, s32 y);
// Move and age every particle; run emitters. Once per frame.
void particles_update(void);
// Add the particles to the sprite list (camera at cx, cy): front = 1 the on_top ones (drawn
// in front of the actors), 0 the others.
void particles_draw(s32 cx, s32 cy, int front);

#endif
