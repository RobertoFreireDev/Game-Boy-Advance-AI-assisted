// anim.h - animation players: step through an animation's frames and fire its events.
#ifndef ENGINE_ANIM_H
#define ENGINE_ANIM_H

#include "data.h"

struct Actor;

typedef struct {
    const AnimationData *anim;
    u16 tick;           // ticks spent on the current frame
    u8 frame;           // index into anim->frames
    u8 done;            // 1 when a non-looping animation reached its last frame
} AnimPlayer;

// Start an animation from its first frame. `owner` receives frame events (may be NULL).
void anim_start(AnimPlayer *p, const AnimationData *a, struct Actor *owner);
// Switch to an animation only if it is not the one already playing.
void anim_set(AnimPlayer *p, const AnimationData *a, struct Actor *owner);
// Advance one tick.
void anim_step(AnimPlayer *p, struct Actor *owner);
// Advance every actor's animation by one tick (looping ones without events pause off screen).
void anim_update(void);
// The frame being shown (NULL if nothing plays). Inline: the sprite list asks for every actor.
static inline const AnimFrame *anim_frame(const AnimPlayer *p) {
    return p->anim ? &p->anim->frames[p->frame] : NULL;
}
// Total length of an animation in ticks.
int anim_length(const AnimationData *a);

#endif
