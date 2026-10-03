// actor.h - the actor pool. An actor is a live copy of a player/enemy/npc/prop/platform/
// pickup/trigger node placed in the scene. Every tick it runs its logic list (behaviors).
#ifndef ENGINE_ACTOR_H
#define ENGINE_ACTOR_H

#include "data.h"
#include "config.h"
#include "anim.h"

#define ACTOR_DYING   1     // playing its death; no logic, no collisions
#define ACTOR_FELL    2     // fell below the map this frame
#define ACTOR_HIDDEN  4     // not drawn

#define SOLID_NONE    0
#define SOLID_FULL    1     // blocks from every side
#define SOLID_ONE_WAY 2     // only stands-on from above

typedef struct Actor {
    u8 active, type, category, flags;
    s16 node, inst;                 // node id; scene instance index (-1 = spawned later)
    const ActorData *data;
    const BodyData *body;           // NULL for triggers
    const LogicEntry *logic;
    u8 logic_count;
    u8 facing_left;
    u8 on_ground, was_on_ground;    // standing on something now / last frame
    u8 hit_wall;                    // bumped into a wall this frame
    u8 gravity;                     // falls (behaviors may turn it off, e.g. on ladders)
    u8 climbing;                    // on a ladder: passes through one-way tiles
    u8 solid_mode;                  // SOLID_*: others can stand on it
    u8 tile_touch;                  // TILE_* flags of the tiles it overlaps
    s8 riding;                      // platform actor index it stands on, -1 = none
    u16 collides;                   // categories it collides with (bit per CAT_*)
    s16 hb_x, hb_y, hb_w, hb_h;     // hitbox relative to the origin
    fixed x, y, vx, vy;             // origin position (world pixels) and speed (px/tick)
    fixed prev_x, prev_y;           // position at the start of this frame's physics
    s32 home_x, home_y;             // where it was spawned
    AnimPlayer anim;
    u8 anim_slot;                   // body animation slot playing (ANIM_*)
    u16 blink;                     // > 0: flickers (invincible)
    u16 anim_lock;                  // > 0: movement behaviors don't change the animation
    u16 die_timer;
    u32 state[MAX_LOGIC][BSTATE_WORDS];
} Actor;

extern Actor g_actors[MAX_ACTORS];

// Create an actor from an object node at world position (x, y). Returns NULL if the pool is full.
Actor *actor_spawn(s16 node, s32 x, s32 y, const LogicEntry *logic, u8 logic_count, s16 inst);
// Remove an actor at once.
void actor_destroy(Actor *a);
// Remove every actor (scene change).
void actor_clear_all(void);
// Run every actor's logic list once (in spawn order) and handle dying actors.
void actor_update_all(void);
// Call the on_touch of every behavior of `a` (physics calls this on contact with `other`).
void actor_touch(Actor *a, Actor *other);
// Play a body animation slot, falling back to similar slots, then the default one.
void actor_play_slot(Actor *a, u8 slot);
// True if the body has an animation in this slot.
int actor_has_slot(const Actor *a, u8 slot);
// Play one of the actor's sounds (SND_*), if it has one.
void actor_sound(const Actor *a, u8 event);
// The first live player actor, or NULL.
Actor *actor_player(void);
// Start dying: stay = 1 plays the death in place, 0 pops up and falls off the screen.
void actor_die(Actor *a, int stay);
// True if the actor exists and is not dying.
int actor_alive(const Actor *a);
// Position of the behavior in the actor's logic list, or -1.
int actor_logic_index(const Actor *a, u8 behavior);
// Private state words of the behavior that is running right now.
u32 *bhv_state(Actor *a);
// Private state words of logic entry `index`.
u32 *actor_state_of(Actor *a, int index);
// Index of the actor in the pool.
int actor_index(const Actor *a);
// Hitbox in world pixels (left, top, right, bottom are inclusive).
void actor_rect(const Actor *a, s32 *l, s32 *t, s32 *r, s32 *b);

#endif
