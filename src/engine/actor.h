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
#define ACTOR_STILL   8     // its animation holds the frame it shows (a paralyzed monster)

#define SOLID_NONE    0
#define SOLID_FULL    1     // blocks from every side
#define SOLID_ONE_WAY 2     // only stands-on from above

#define DIR_RIGHT     0     // Actor.facing (top-down games use all four)
#define DIR_LEFT      1
#define DIR_UP        2
#define DIR_DOWN      3

#define TURN_TICKS    8     // actor_face_walk: ticks a 45-degree turn of the art waits (no flicker)

typedef struct Actor {
    u8 active, type, category, flags;
    s16 node, inst;                 // node id; scene instance index (-1 = spawned later)
    const ActorData *data;
    const BodyData *body;           // NULL for triggers
    const LogicEntry *logic;
    u8 logic_count;
    u8 facing_left;                 // last horizontal direction (mirrors the side-view art)
    u8 facing;                      // DIR_*: which way it looks (picks *_up / *_down animations)
    s8 look_x, look_y;              // last 8-way direction it faced (-1..1 each; starts down): where moves aim
    s8 show_x, show_y;              // 8-way direction its art shows (picks the directional animations)
    u8 turn;                        // ticks show_x / show_y have waited to follow look_x / look_y
    u16 turn_at;                    // frame (low 16 bits) the art last turned
    u8 flash;                       // > 0: drawn with flash_pal for this many more ticks
    u8 flash_more, flash_len, flash_gap;    // flashes still to come after it, ticks each lasts, ticks of the gap so far
    s8 draw_dx;                    // drawn this many px right of its x (a paralyzed monster trembles)
    const PaletteData *pal;         // drawn with these colors instead of its sprite's (NULL = own)
    const PaletteData *flash_pal;   // colors of a short flash (a hit)
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
    u16 stun;                       // > 0: stunned: movement behaviors hold still, no touch damage
    u16 die_timer;
    u32 state[MAX_LOGIC][BSTATE_WORDS];
} Actor;

extern Actor g_actors[MAX_ACTORS];

// Create an actor from an object node at world position (x, y). Returns NULL if the pool is full.
Actor *actor_spawn(s16 node, s32 x, s32 y, const LogicEntry *logic, u8 logic_count, s16 inst);
// Give an actor a (new) body: its look, hitbox and physics. Used at spawn, and by behaviors
// that pick the body at run time (a monster takes its species' body).
void actor_set_body(Actor *a, const BodyData *body);
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
// Turn to face a direction (dx, dy in -1..1). look_x / look_y and the art (show_x / show_y)
// take the full 8-way direction; `facing` keeps the current one of four on diagonals when it
// is one of the two directions, otherwise prefers left/right.
void actor_face(Actor *a, int dx, int dy);
// Like actor_face, for a walker that steers every tick: a 45-degree turn of the art, or any turn
// within TURN_TICKS of the last one, waits until the new direction has been asked TURN_TICKS ticks
// in a row, so an 8-way zigzag or a monster jittering in place doesn't flicker.
void actor_face_walk(Actor *a, int dx, int dy);
// True for the slots drawn for one direction only (*_up, *_down and the *_left ones): never mirrored.
static inline int actor_slot_fixed(u8 slot) {
    switch (slot) {
    case ANIM_IDLE_UP: case ANIM_WALK_UP: case ANIM_ATTACK_UP:
    case ANIM_IDLE_DOWN: case ANIM_WALK_DOWN: case ANIM_ATTACK_DOWN:
    case ANIM_IDLE_LEFT: case ANIM_WALK_LEFT: case ANIM_ATTACK_LEFT:
    case ANIM_IDLE_UP_LEFT: case ANIM_WALK_UP_LEFT: case ANIM_ATTACK_UP_LEFT:
    case ANIM_IDLE_DOWN_LEFT: case ANIM_WALK_DOWN_LEFT: case ANIM_ATTACK_DOWN_LEFT:
        return 1;
    default:
        return 0;
    }
}
// True if the body has an animation in this slot.
int actor_has_slot(const Actor *a, u8 slot);
// Play one of the actor's sounds (SND_*), if it has one.
void actor_sound(const Actor *a, u8 event);
// The first live player actor, or NULL.
Actor *actor_player(void);
// Start dying: stay = 1 plays the death in place, 0 pops up and falls off the screen. A flash
// still playing (a hit's) plays out before its blink and die_timer start counting down.
void actor_die(Actor *a, int stay);
// Flash in other colors (a hit): `times` flashes of `ticks` ticks each, with gaps as long between
// them. The flashes show through a blink.
static inline void actor_flash(Actor *a, const PaletteData *pal, int ticks, int times) {
    a->flash_pal = pal;
    a->flash = (u8)ticks;
    a->flash_more = (u8)(ticks > 0 && times > 1 ? times - 1 : 0);
    a->flash_len = (u8)ticks;
    a->flash_gap = 0;
}
// The small questions below are inline: monsters and moves ask them many times a frame, and a
// call from Thumb code (ROM) into the actor code in IWRAM goes through a slow veneer.
// True if the actor exists and is not dying.
static inline int actor_alive(const Actor *a) { return a && a->active && !(a->flags & ACTOR_DYING); }
// Position of the behavior in the actor's logic list, or -1.
static inline int actor_logic_index(const Actor *a, u8 behavior) {
    for (int i = 0; i < a->logic_count; i++)
        if (a->logic[i].behavior == behavior) return i;
    return -1;
}
// Private state words of the behavior that is running right now.
extern int g_logic_slot;            // logic entry running now (actor_update_all / actor_spawn)
static inline u32 *bhv_state(Actor *a) { return a->state[g_logic_slot]; }
// Private state words of logic entry `index`.
static inline u32 *actor_state_of(Actor *a, int index) { return a->state[index]; }
// Live (not dying) actors made from this node.
int actor_count_node(s16 node);
// Free slots in the actor pool.
int actor_free_slots(void);
// Index of the actor in the pool.
static inline int actor_index(const Actor *a) { return (int)(a - g_actors); }
// Hitbox in world pixels (left, top, right, bottom are inclusive).
static inline void actor_rect(const Actor *a, s32 *l, s32 *t, s32 *r, s32 *b) {
    *l = fx_to_int(a->x) + a->hb_x;
    *t = fx_to_int(a->y) + a->hb_y;
    *r = *l + a->hb_w - 1;
    *b = *t + a->hb_h - 1;
}

#endif
