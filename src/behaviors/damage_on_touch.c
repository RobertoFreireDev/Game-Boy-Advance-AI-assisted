// damage_on_touch - hurt whoever touches it (unless they stomp it and it is stompable).
// With stun_ticks, a hit that lands stuns it: it holds still and hurts no one for a while.
// With vanish (an enemy's shot), it disappears once a hit lands.
#include "damage_on_touch.h"
#include "behavior_params.h"
#include "health.h"
#include "engine/physics.h"

void bhv_damage_on_touch_on_touch(Actor *self, Actor *other, const void *params) {
    const Params_damage_on_touch *p = params;
    int target = p->hurts == DAMAGE_ON_TOUCH_HURTS_ANY ||
                 (p->hurts == DAMAGE_ON_TOUCH_HURTS_PLAYER && other->category == CAT_PLAYER) ||
                 (p->hurts == DAMAGE_ON_TOUCH_HURTS_ENEMY && other->category == CAT_ENEMY);
    if (!target || self->stun || (self->flags & ACTOR_DYING)) return;
    if (actor_logic_index(self, BHV_STOMPABLE) >= 0 && other->category == CAT_PLAYER &&
        physics_is_stomp(other, self)) return;
    if (!health_damage(other, p->amount, self)) return;
    if (p->vanish) {                            // a shot: gone once it lands a hit
        actor_destroy(self);
        return;
    }
    if (p->stun_ticks > 0) {
        self->stun = (u16)p->stun_ticks;
        self->vx = self->vy = 0;
    }
}
