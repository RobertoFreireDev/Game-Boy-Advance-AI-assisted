// anim.iwram.c - (hot code: ARM in IWRAM) animation players and frame events.
#include "anim.h"
#include "actor.h"
#include "actions.h"

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
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *a = &g_actors[i];
        if (a->active) anim_step(&a->anim, a);
    }
}

const AnimFrame *anim_frame(const AnimPlayer *p) {
    return p->anim ? &p->anim->frames[p->frame] : NULL;
}

int anim_length(const AnimationData *a) {
    int t = 0;
    if (a)
        for (int i = 0; i < a->frame_count; i++) t += a->frames[i].ticks;
    return t;
}
