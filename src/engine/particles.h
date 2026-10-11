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
// Pick one particle's random direction and speed (so a move can see where it will fly first).
void particles_aim(const ParticleData *p, s32 x, s32 y, ParticleShot *out);
// Send off a particle picked by particles_aim.
void particles_shoot(const ParticleShot *s);
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
