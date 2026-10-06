// anim.iwram.c - (hot code: ARM in IWRAM) animation players and frame events.
#include "anim.h"
#include "actor.h"
#include "actions.h"
#include "camera.h"

static void fire_events(const AnimPlayer *p, struct Actor *owner) {
    const AnimationData *a = p->anim;
    if (!owner) return;
    for (int i = 0; i < a->event_count; i++)
        if (a->events[i].at == p->frame) scripts_start(a->events[i].actions, owner);
}

void anim_start(AnimPlayer *p, const AnimationData *a, struct Actor *owner) {
    p->anim = a;
    p->tick = 0;
    p->frame = 0;
    p->done = 0;
    if (a) fire_events(p, owner);
}

void anim_set(AnimPlayer *p, const AnimationData *a, struct Actor *owner) {
    if (p->anim != a) anim_start(p, a, owner);
}

void anim_step(AnimPlayer *p, struct Actor *owner) {
    const AnimationData *a = p->anim;
    if (!a || p->done) return;
    if (++p->tick < a->frames[p->frame].ticks) return;
    p->tick = 0;
    if (p->frame + 1 < a->frame_count) {
        p->frame++;
    } else if (a->loop) {
        p->frame = 0;
    } else {
        p->done = 1;
        return;
    }
    fire_events(p, owner);
}

void anim_update(void) {
    s32 cx = camera_x(), cy = camera_y();
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &g_actors[i];
        if (!a->active) continue;
        // Far off screen (same margin as sprites.iwram.c), a looping animation without frame
        // events only changes looks: hold it until the actor comes back into view.
        const AnimationData *an = a->anim.anim;
        if (an && an->loop && !an->event_count) {
            s32 x = fx_to_int(a->x) - cx, y = fx_to_int(a->y) - cy;
            if (x <= -64 || x >= 240 + 64 || y <= -64 || y >= 160 + 64) continue;
        }
        anim_step(&a->anim, a);
    }
}

int anim_length(const AnimationData *a) {
    int t = 0;
    if (a)
        for (int i = 0; i < a->frame_count; i++) t += a->frames[i].ticks;
    return t;
}
