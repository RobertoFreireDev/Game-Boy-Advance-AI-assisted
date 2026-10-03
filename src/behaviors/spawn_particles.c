// spawn_particles - emit particles always, on landing, on jumping or while running.
#include "spawn_particles.h"
#include "behavior_params.h"
#include "engine/particles.h"

void bhv_spawn_particles_update(Actor *a, const void *params) {
    const Params_spawn_particles *p = params;
    const ParticleData *d = p->particle;
    u32 *st = bhv_state(a);
    if (!d) return;
    s32 x = fx_to_int(a->x) + (a->facing_left ? -p->offset_x : p->offset_x);
    s32 y = fx_to_int(a->y) + p->offset_y;
    u32 every = d->mode == PTC_STREAM ? d->rate : d->lifetime;
    switch (p->when) {
    case SPAWN_PARTICLES_WHEN_LAND:
        if (a->on_ground && !a->was_on_ground) particles_burst(d, x, y);
        return;
    case SPAWN_PARTICLES_WHEN_JUMP:
        if (!a->on_ground && a->was_on_ground && a->vy < 0) particles_burst(d, x, y);
        return;
    case SPAWN_PARTICLES_WHEN_RUN:
        if (!a->on_ground || a->vx == 0) { st[0] = 0; return; }
        break;
    }
    if (st[0] == 0) {
        particles_burst(d, x, y);
        st[0] = every;
    }
    st[0]--;
}
