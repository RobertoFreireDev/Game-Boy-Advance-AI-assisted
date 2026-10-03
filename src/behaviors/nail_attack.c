// nail_attack - side-view melee. The button slashes forward; holding up slashes up; holding
// down in the air slashes down. Everything with health inside the blade is hurt (once per
// swing, thanks to its invincibility) and each hit fills a soul variable. A down slash that
// meets an enemy or spikes bounces the attacker up (a pogo) and gives back the air moves.
// Movement stays free while slashing; only the animation is taken over.
#include "nail_attack.h"
#include "behavior_params.h"
#include "health.h"
#include "platformer_controller.h"
#include "engine/input.h"
#include "engine/vars.h"
#include "engine/physics.h"
#include "engine/particles.h"

enum { ST_TIMER, ST_COOLDOWN, ST_DIR, ST_POGO };
enum { SLASH_FORWARD, SLASH_UP, SLASH_DOWN };

#define BLADE_OVERLAP 4     // the blade box also covers this many pixels of the body edge

static int can_hit(const Params_nail_attack *p, const Actor *o) {
    if (o->category == CAT_ENEMY) return 1;
    return p->hits == NAIL_ATTACK_HITS_ENEMY_AND_PROP && o->category == CAT_PROP;
}

static void pogo(Actor *a, const Params_nail_attack *p, u32 *st) {
    if (st[ST_POGO]) return;
    st[ST_POGO] = 1;
    a->vy = -p->pogo;
    platformer_refresh_air(a);
}

// Hurt everything with health inside the blade box this tick.
static void swing_hits(Actor *a, const Params_nail_attack *p, u32 *st) {
    s32 l, t, r, b;
    actor_rect(a, &l, &t, &r, &b);
    s32 cx = (l + r) / 2, cy = (t + b) / 2, half = p->width / 2;
    s32 x0, y0, x1, y1;
    switch (st[ST_DIR]) {
    case SLASH_UP:   y0 = t - p->reach; y1 = t - 1 + BLADE_OVERLAP; x0 = cx - half; x1 = x0 + p->width - 1; break;
    case SLASH_DOWN: y0 = b + 1 - BLADE_OVERLAP; y1 = b + p->reach; x0 = cx - half; x1 = x0 + p->width - 1; break;
    default:
        y0 = cy - half; y1 = y0 + p->width - 1;
        if (a->facing_left) { x0 = l - p->reach; x1 = l - 1 + BLADE_OVERLAP; }
        else { x0 = r + 1 - BLADE_OVERLAP; x1 = r + p->reach; }
        break;
    }
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor *o = &g_actors[i];
        if (o == a || !actor_alive(o) || !can_hit(p, o) || actor_logic_index(o, BHV_HEALTH) < 0) continue;
        s32 ol, ot, orr, ob;
        actor_rect(o, &ol, &ot, &orr, &ob);
        if (ol > x1 || orr < x0 || ot > y1 || ob < y0) continue;
        if (st[ST_DIR] == SLASH_DOWN) pogo(a, p, st);
        if (!health_damage(o, p->damage, a)) continue;          // still blinking from this swing
        actor_sound(a, SND_HIT);
        if (p->hit_particle) particles_burst(p->hit_particle, (ol + orr) / 2, (ot + ob) / 2);
        if (o->category == CAT_ENEMY && p->soul_var >= 0) {
            s32 cap = p->soul_max_var >= 0 ? vars_get(p->soul_max_var) : p->soul_max;
            s32 v = vars_get(p->soul_var) + p->soul_gain;
            vars_set(p->soul_var, v > cap ? cap : v);
        }
        if (st[ST_DIR] == SLASH_FORWARD) a->vx = (a->facing_left ? 1 : -1) * p->recoil;
    }
    // Spikes under a down slash are a springboard too.
    if (st[ST_DIR] == SLASH_DOWN && !st[ST_POGO]) {
        for (s32 y = y0; y <= y1; y += 4)
            for (s32 x = x0; x <= x1; x += 4)
                if (physics_tile_at(x, y) & TILE_HAZARD) {
                    pogo(a, p, st);
                    return;
                }
    }
}

void bhv_nail_attack_update(Actor *a, const void *params) {
    const Params_nail_attack *p = params;
    u32 *st = bhv_state(a);
    if (st[ST_COOLDOWN]) st[ST_COOLDOWN]--;
    if (st[ST_TIMER]) {
        if (a->anim_slot == ANIM_HURT) {         // got hit: the swing is cancelled
            st[ST_TIMER] = 0;
            return;
        }
        st[ST_TIMER]--;
        swing_hits(a, p, st);
        return;
    }
    if (a->anim_lock || st[ST_COOLDOWN] || !a->body || platformer_dashing(a)) return;
    if (p->var >= 0 && vars_get(p->var) == 0) return;
    if (!input_pressed(p->button)) return;
    int dy = input_dir_y();
    u8 slot = ANIM_ATTACK;
    st[ST_DIR] = SLASH_FORWARD;
    if (dy < 0) { st[ST_DIR] = SLASH_UP; slot = ANIM_ATTACK_UP; }
    else if (dy > 0 && !a->on_ground) { st[ST_DIR] = SLASH_DOWN; slot = ANIM_ATTACK_DOWN; }
    st[ST_TIMER] = (u32)p->ticks;
    st[ST_COOLDOWN] = (u32)(p->ticks + p->cooldown);
    st[ST_POGO] = 0;
    if (!a->body->anims[slot]) slot = ANIM_ATTACK;
    a->anim_slot = slot;
    anim_start(&a->anim, a->body->anims[slot], a);   // restart even if it was already playing
    actor_sound(a, SND_ATTACK);
    swing_hits(a, p, st);
}
