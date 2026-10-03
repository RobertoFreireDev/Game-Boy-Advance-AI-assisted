// stompable - defeated when the player lands on top; the player bounces off.
#include "stompable.h"
#include "behavior_params.h"
#include "engine/physics.h"
#include "engine/actions.h"

void bhv_stompable_on_touch(Actor *self, Actor *other, const void *params) {
    const Params_stompable *p = params;
    if (other->category != CAT_PLAYER || (self->flags & ACTOR_DYING) || !physics_is_stomp(other, self)) return;
    other->vy = -p->bounce;
    actor_sound(self, SND_STOMP);
    scripts_start(p->on_stomp, self);
    actor_die(self, 1);
}
