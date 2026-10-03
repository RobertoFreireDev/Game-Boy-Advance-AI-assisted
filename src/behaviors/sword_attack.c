// sword_attack - press a button to swing a sword in the direction the actor faces. The
// actor stops while swinging and plays its attack animation (attack_up / attack_down /
// attack). Everything with health inside the blade's box is hurt and knocked away.
#include "sword_attack.h"
#include "behavior_params.h"
#include "health.h"
#include "engine/input.h"
#include "engine/vars.h"

enum { ST_TIMER, ST_COOLDOWN };

#define BLADE_OVERLAP 4     // the blade box also covers this many pixels of the body edge

static int can_hit(const Params_sword_attack *p, const Actor *o) {
    if (o->category == CAT_ENEMY) return 1;
    if (p->hits == SWORD_ATTACK_HITS_ENEMY_AND_PROP) return o->category == CAT_PROP;
    return p->hits == SWORD_ATTACK_HITS_ANY;
}

// Hurt everything with health inside the blade box this tick.
static void swing_hits(Actor *a, const Params_sword_attack *p) {
    s32 l, t, r, b;
    actor_rect(a, &l, &t, &r, &b);
    s32 cx = (l + r) / 2, cy = (t + b) / 2, half = p->width / 2;
    s32 x0, y0, x1, y1;
    switch (a->facing) {
    case DIR_LEFT:  x0 = l - p->reach; x1 = l - 1 + BLADE_OVERLAP; y0 = cy - half; y1 = y0 + p->width - 1; break;
    case DIR_UP:    y0 = t - p->reach; y1 = t - 1 + BLADE_OVERLAP; x0 = cx - half; x1 = x0 + p->width - 1; break;
    case DIR_DOWN:  y0 = b + 1 - BLADE_OVERLAP; y1 = b + p->reach; x0 = cx - half; x1 = x0 + p->width - 1; break;
    default:        x0 = r + 1 - BLADE_OVERLAP; x1 = r + p->reach; y0 = cy - half; y1 = y0 + p->width - 1; break;
    }
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *o = &g_actors[i];
        if (o == a || !actor_alive(o) || !can_hit(p, o) || actor_logic_index(o, BHV_HEALTH) < 0) continue;
        s32 ol, ot, orr, ob;
        actor_rect(o, &ol, &ot, &orr, &ob);
        if (ol <= x1 && orr >= x0 && ot <= y1 && ob >= y0) health_damage(o, p->damage, a);
    }
}

void bhv_sword_attack_update(Actor *a, const void *params) {
    const Params_sword_attack *p = params;
    u32 *st = bhv_state(a);
    if (st[ST_COOLDOWN]) st[ST_COOLDOWN]--;
    if (st[ST_TIMER]) {
        if (a->anim_slot == ANIM_HURT) {         // got hit: the swing is cancelled
            st[ST_TIMER] = 0;
            return;
        }
        st[ST_TIMER]--;
        swing_hits(a, p);
        return;
    }
    if (a->anim_lock || st[ST_COOLDOWN]) return;
    if (p->var >= 0 && vars_get(p->var) == 0) return;
    if (!input_pressed(p->button)) return;
    st[ST_TIMER] = (u32)p->ticks;
    st[ST_COOLDOWN] = (u32)(p->ticks + p->cooldown);
    a->vx = a->vy = 0;
    a->anim_lock = (u16)(p->ticks + 1);          // movement behaviors wait for the swing
    actor_play_slot(a, ANIM_ATTACK);
    anim_start(&a->anim, a->anim.anim, a);       // restart even if it was already playing
    actor_sound(a, SND_ATTACK);
    swing_hits(a, p);
}
