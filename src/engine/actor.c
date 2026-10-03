// actor.c - the actor pool and logic-list runner.
#include <string.h>
#include "actor.h"
#include "actions.h"
#include "audio.h"
#include "camera.h"

Actor g_actors[MAX_ACTORS];
static int s_slot;              // logic entry currently running (for bhv_state)

#define DIE_FALL_TICKS 90
#define DIE_STAY_TICKS 30

int actor_index(const Actor *a) { return (int)(a - g_actors); }

u32 *actor_state_of(Actor *a, int index) { return a->state[index]; }
u32 *bhv_state(Actor *a) { return a->state[s_slot]; }

int actor_alive(const Actor *a) { return a && a->active && !(a->flags & ACTOR_DYING); }

void actor_rect(const Actor *a, s32 *l, s32 *t, s32 *r, s32 *b) {
    *l = fx_to_int(a->x) + a->hb_x;
    *t = fx_to_int(a->y) + a->hb_y;
    *r = *l + a->hb_w - 1;
    *b = *t + a->hb_h - 1;
}

int actor_logic_index(const Actor *a, u8 behavior) {
    for (int i = 0; i < a->logic_count; i++)
        if (a->logic[i].behavior == behavior) return i;
    return -1;
}

int actor_has_slot(const Actor *a, u8 slot) {
    return a->body && a->body->anims[slot] != NULL;
}

void actor_play_slot(Actor *a, u8 slot) {
    if (!a->body) return;
    const AnimationData *an = a->body->anims[slot];
    if (!an && slot == ANIM_RUN) an = a->body->anims[ANIM_WALK];
    if (!an && slot == ANIM_WALK) an = a->body->anims[ANIM_RUN];
    if (!an && slot == ANIM_FALL) an = a->body->anims[ANIM_JUMP];
    if (!an && slot == ANIM_DIE) an = a->body->anims[ANIM_HURT];
    if (!an) {
        slot = a->body->default_slot;
        an = a->body->anims[slot];
    }
    a->anim_slot = slot;
    anim_set(&a->anim, an, a);
}

void actor_sound(const Actor *a, u8 event) {
    s16 n = a->data->sounds[event];
    if (n >= 0) audio_play_sfx((const SfxData *)g_nodes[n].data);
}

Actor *actor_player(void) {
    for (int i = 0; i < MAX_ACTORS; i++)
        if (g_actors[i].active && g_actors[i].type == NT_PLAYER && !(g_actors[i].flags & ACTOR_DYING))
            return &g_actors[i];
    return NULL;
}

Actor *actor_spawn(s16 node, s32 x, s32 y, const LogicEntry *logic, u8 logic_count, s16 inst) {
    const ActorData *d = (const ActorData *)g_nodes[node].data;
    Actor *a = NULL;
    for (int i = 0; i < MAX_ACTORS; i++)
        if (!g_actors[i].active) { a = &g_actors[i]; break; }
    if (!a) return NULL;
    memset(a, 0, sizeof(*a));
    a->active = 1;
    a->type = d->type;
    a->category = d->category;
    a->node = node;
    a->inst = inst;
    a->data = d;
    a->body = d->body;
    a->logic = logic;
    a->logic_count = logic_count > MAX_LOGIC ? MAX_LOGIC : logic_count;
    a->x = a->prev_x = fx_from_int(x);
    a->y = a->prev_y = fx_from_int(y);
    a->home_x = x;
    a->home_y = y;
    a->riding = -1;
    if (a->body) {
        a->hb_x = a->body->hb_x;
        a->hb_y = a->body->hb_y;
        a->hb_w = a->body->hb_w;
        a->hb_h = a->body->hb_h;
        a->gravity = a->body->gravity;
        a->solid_mode = a->body->solid ? SOLID_FULL : SOLID_NONE;
        a->collides = a->body->collides;
        actor_play_slot(a, a->body->default_slot);
    } else {
        a->hb_w = (s16)d->zone_w;       // triggers: the zone, from the top-left corner
        a->hb_h = (s16)d->zone_h;
        a->collides = 1 << CAT_PLAYER;
        a->flags |= ACTOR_HIDDEN;
    }
    for (int i = 0; i < a->logic_count; i++) {
        const BehaviorDef *b = &g_behaviors[a->logic[i].behavior];
        s_slot = i;
        if (b->init) b->init(a, a->logic[i].params);
    }
    if (d->on_start) d->on_start(a);
    return a;
}

void actor_destroy(Actor *a) {
    if (!a->active) return;
    a->active = 0;
    scripts_forget_actor(a);
    camera_forget(a);
}

void actor_clear_all(void) {
    for (int i = 0; i < MAX_ACTORS; i++) g_actors[i].active = 0;
}

void actor_die(Actor *a, int stay) {
    if (!a->active || (a->flags & ACTOR_DYING)) return;
    a->flags |= ACTOR_DYING;
    a->collides = 0;
    a->solid_mode = SOLID_NONE;
    a->blink = 0;
    a->anim_lock = 0;
    a->vx = 0;
    if (stay) {
        a->vy = 0;
        a->gravity = 0;
        a->die_timer = DIE_STAY_TICKS;
    } else {
        a->vy = -fx_from_int(3);
        a->gravity = 1;
        a->die_timer = DIE_FALL_TICKS;
    }
    actor_play_slot(a, ANIM_DIE);
}

void actor_touch(Actor *a, Actor *other) {
    for (int i = 0; i < a->logic_count && a->active; i++) {
        const BehaviorDef *b = &g_behaviors[a->logic[i].behavior];
        if (!b->on_touch) continue;
        s_slot = i;
        b->on_touch(a, other, a->logic[i].params);
    }
}

void actor_update_all(void) {
    for (int n = 0; n < MAX_ACTORS; n++) {
        Actor *a = &g_actors[n];
        if (!a->active) continue;
        if (a->flags & ACTOR_DYING) {
            if (a->die_timer == 0 || --a->die_timer == 0) actor_destroy(a);
            continue;
        }
        for (int i = 0; i < a->logic_count && a->active && !(a->flags & ACTOR_DYING); i++) {
            const BehaviorDef *b = &g_behaviors[a->logic[i].behavior];
            s_slot = i;
            if (b->update) b->update(a, a->logic[i].params);
        }
        if (!a->active) continue;
        if (a->data->on_update) a->data->on_update(a);
        if (a->flags & ACTOR_FELL) {        // fell off the map and no behavior handled it
            actor_destroy(a);
            continue;
        }
        if (a->blink) a->blink--;
        if (a->anim_lock) a->anim_lock--;
    }
}
