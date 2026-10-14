// collectible - the player picks it up: add to a variable, play 'collect', run on_collect (as
// the item, or as the player: words float over the player), disappear.
#include "collectible.h"
#include "behavior_params.h"
#include "engine/vars.h"
#include "engine/actions.h"

void bhv_collectible_on_touch(Actor *self, Actor *other, const void *params) {
    const Params_collectible *p = params;
    if (other->category != CAT_PLAYER) return;
    vars_add(p->var, p->amount);
    actor_sound(self, SND_COLLECT);
    scripts_start(p->on_collect, p->as_player ? other : self);
    actor_destroy(self);
}
