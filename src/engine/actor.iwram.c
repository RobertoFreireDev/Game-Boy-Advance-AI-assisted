// actor.iwram.c - (hot code: ARM in IWRAM) the actor pool and logic-list runner.
#include <stddef.h>
#include <string.h>
#include "actor.h"
#include "actions.h"
#include "audio.h"
#include "camera.h"
#include "core.h"

EWRAM_BSS Actor g_actors[MAX_ACTORS];   // too big for IWRAM with hordes; EWRAM has room
static int s_slot;              // logic entry currently running (for bhv_state)

// Bookkeeping kept up to date on spawn / die / destroy, so the questions waves and weapons ask
// every tick ("how many bats are alive?", "is the pool full?", "where is the player?") don't
// walk the whole pool in slow EWRAM.
static int s_first_free;        // no free slot below this index
static int s_active;            // actors in the pool (dying ones included)
static u8 s_node_live[NODE_COUNT];  // live (not dying) actors per node
static Actor *s_player;         // last player found (checked before use)
static u32 s_no_player_frame;   // frame in which a search found no live player (+1; 0 = none)

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

void actor_face(Actor *a, int dx, int dy) {
    if (!dx && !dy) return;
    a->look_x = (s8)dx;
    a->look_y = (s8)dy;
    if (dx) a->facing_left = dx < 0;
    int keep = (dx && a->facing == (dx < 0 ? DIR_LEFT : DIR_RIGHT)) ||
               (dy && a->facing == (dy < 0 ? DIR_UP : DIR_DOWN));
    if (keep) return;
    if (dx) a->facing = dx < 0 ? DIR_LEFT : DIR_RIGHT;
    else a->facing = dy < 0 ? DIR_UP : DIR_DOWN;
}

// The *_up / *_down version of a slot for the way the actor faces, if the body has it.
static u8 directional_slot(const Actor *a, u8 slot) {
    static const u8 up[ANIM_SLOT_COUNT] = {
        [ANIM_IDLE] = ANIM_IDLE_UP, [ANIM_WALK] = ANIM_WALK_UP, [ANIM_RUN] = ANIM_WALK_UP,
        [ANIM_ATTACK] = ANIM_ATTACK_UP };
    static const u8 down[ANIM_SLOT_COUNT] = {
        [ANIM_IDLE] = ANIM_IDLE_DOWN, [ANIM_WALK] = ANIM_WALK_DOWN, [ANIM_RUN] = ANIM_WALK_DOWN,
        [ANIM_ATTACK] = ANIM_ATTACK_DOWN };
    if (a->facing != DIR_UP && a->facing != DIR_DOWN) return slot;
    u8 v = a->facing == DIR_UP ? up[slot] : down[slot];
    return (v && a->body->anims[v]) ? v : slot;
}

void actor_play_slot(Actor *a, u8 slot) {
    if (!a->body) return;
    slot = directional_slot(a, slot);
    const AnimationData *an = a->body->anims[slot];
    if (!an && slot == ANIM_RUN) an = a->body->anims[ANIM_WALK];
    if (!an && slot == ANIM_WALK) an = a->body->anims[ANIM_RUN];
    if (!an && slot == ANIM_FALL) an = a->body->anims[ANIM_JUMP];
    if (!an && slot == ANIM_DIE) an = a->body->anims[ANIM_HURT];
    if (!an && slot == ANIM_IDLE_UP) an = a->body->anims[slot = ANIM_IDLE];
    if (!an && slot == ANIM_IDLE_DOWN) an = a->body->anims[slot = ANIM_IDLE];
    if (!an && slot == ANIM_WALK_UP) an = a->body->anims[slot = ANIM_WALK];
    if (!an && slot == ANIM_WALK_DOWN) an = a->body->anims[slot = ANIM_WALK];
    if (!an && slot == ANIM_ATTACK_UP) an = a->body->anims[slot = ANIM_ATTACK];
    if (!an && slot == ANIM_ATTACK_DOWN) an = a->body->anims[slot = ANIM_ATTACK];
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
    Actor *p = s_player;
    if (p && p->active && p->type == NT_PLAYER && !(p->flags & ACTOR_DYING)) return p;
    // No live player (e.g. while the hero's death plays): search once per frame, not once per
    // monster asking. A player spawned later this frame registers itself in actor_spawn.
    u32 f = core_frame() + 1;
    if (!p && s_no_player_frame == f) return NULL;
    s_player = NULL;
    for (int i = 0; i < MAX_ACTORS; i++)
        if (g_actors[i].active && g_actors[i].type == NT_PLAYER && !(g_actors[i].flags & ACTOR_DYING))
            return s_player = &g_actors[i];
    s_no_player_frame = f;
    return NULL;
}

Actor *actor_spawn(s16 node, s32 x, s32 y, const LogicEntry *logic, u8 logic_count, s16 inst) {
    const ActorData *d = (const ActorData *)g_nodes[node].data;
    Actor *a = NULL;
    for (int i = s_first_free; i < MAX_ACTORS; i++)
        if (!g_actors[i].active) { a = &g_actors[i]; s_first_free = i + 1; break; }
    if (!a) {
        s_first_free = MAX_ACTORS;
        return NULL;
    }
    if (logic_count > MAX_LOGIC) logic_count = MAX_LOGIC;
    // Clear only what this actor uses: the state rows of its own behaviors (a shot has one).
    memset(a, 0, offsetof(Actor, state));
    memset(a->state, 0, logic_count * sizeof(a->state[0]));
    s_active++;
    s_node_live[node]++;
    a->active = 1;
    if (d->type == NT_PLAYER && !s_player) s_player = a;
    a->type = d->type;
    a->category = d->category;
    a->node = node;
    a->inst = inst;
    a->data = d;
    a->body = d->body;
    a->logic = logic;
    a->logic_count = logic_count;
    a->x = a->prev_x = fx_from_int(x);
    a->y = a->prev_y = fx_from_int(y);
    a->home_x = x;
    a->home_y = y;
    a->riding = -1;
    a->facing = DIR_DOWN;
    a->look_y = 1;
    if (a->body) {
        actor_set_body(a, a->body);
    } else {
        a->hb_w = (s16)d->zone_w;       // triggers: the zone, from the top-left corner
        a->hb_h = (s16)d->zone_h;
        a->collides = d->type == NT_TRIGGER ? 1 << CAT_PLAYER : 0;   // bodiless props touch nothing
        a->flags |= ACTOR_HIDDEN;
    }
    // A behavior may remove the actor while it starts (a monster that isn't wanted in this
    // run): the rest don't start, and the caller gets NULL.
    for (int i = 0; i < a->logic_count && a->active; i++) {
        const BehaviorDef *b = &g_behaviors[a->logic[i].behavior];
        s_slot = i;
        if (b->init) b->init(a, a->logic[i].params);
    }
    if (a->active && d->on_start) d->on_start(a);
    return a->active ? a : NULL;
}

void actor_set_body(Actor *a, const BodyData *body) {
    a->body = body;
    if (!body) return;
    a->hb_x = body->hb_x;
    a->hb_y = body->hb_y;
    a->hb_w = body->hb_w;
    a->hb_h = body->hb_h;
    a->gravity = body->gravity;
    a->solid_mode = body->solid ? SOLID_FULL : SOLID_NONE;
    a->collides = body->collides;
    a->flags &= (u8)~ACTOR_HIDDEN;
    a->anim.anim = NULL;                // start the new body's animation from its first frame
    actor_play_slot(a, body->default_slot);
}

int actor_count_node(s16 node) {
    return (node >= 0 && node < NODE_COUNT) ? s_node_live[node] : 0;
}

int actor_free_slots(void) { return MAX_ACTORS - s_active; }

void actor_destroy(Actor *a) {
    if (!a->active) return;
    if (!(a->flags & ACTOR_DYING)) s_node_live[a->node]--;
    s_active--;
    int i = actor_index(a);
    if (i < s_first_free) s_first_free = i;
    a->active = 0;
    scripts_forget_actor(a);
    camera_forget(a);
}

void actor_clear_all(void) {
    // EWRAM .sbss is not zeroed at boot: wipe the whole pool once. After that, freeing every
    // slot is enough (actor_spawn clears a slot when it takes it); wiping 28 KB of slow EWRAM
    // took most of a frame on every scene change.
    static u8 s_wiped;
    if (!s_wiped) {
        memset(g_actors, 0, sizeof(g_actors));
        s_wiped = 1;
    }
    for (int i = 0; i < MAX_ACTORS; i++) g_actors[i].active = 0;
    memset(s_node_live, 0, sizeof(s_node_live));
    s_first_free = 0;
    s_active = 0;
    s_player = NULL;
    s_no_player_frame = 0;
}

void actor_die(Actor *a, int stay) {
    if (!a->active || (a->flags & ACTOR_DYING)) return;
    a->flags |= ACTOR_DYING;
    s_node_live[a->node]--;
    a->collides = 0;
    a->solid_mode = SOLID_NONE;
    a->blink = 0;
    a->anim_lock = 0;
    a->stun = 0;
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
            if (a->blink) a->blink--;       // a death that blinks out (set after actor_die)
            if (a->flash) a->flash--;
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
        if (a->flash) a->flash--;
        if (a->anim_lock) a->anim_lock--;
        if (a->stun) a->stun--;
    }
}
