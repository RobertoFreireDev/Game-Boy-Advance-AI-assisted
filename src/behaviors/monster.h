// monster - a creature of a monster-battle game: its species (look, type, moves), level, hit
// points and status, and everything that happens when a move hits it (type chart, damage,
// MISS / PAR / x1.5! words, XP for the player, fainting). moves.c fires the moves.
#ifndef BHV_MONSTER_H
#define BHV_MONSTER_H

#include "engine/actor.h"

enum { TEAM_PLAYER, TEAM_WILD };

// What a hit did, from least to most (a move that hits several keeps the best for its sound).
enum { HIT_NONE, HIT_MISS, HIT_WEAK, HIT_NORMAL, HIT_SUPER };

// Which blow a hit is: a single one, or the first / second of a double hit (a species' double_hit).
enum { BLOW_ONE, BLOW_FIRST, BLOW_SECOND };

// What one hit carries (a move used by a monster, or a zone it left on the ground).
typedef struct {
    const MoveData *move;
    u8 team, level, roll;           // roll: 1 = roll the move's accuracy
    u8 blow;                        // BLOW_*: a double hit's first blow floats its words a row higher
                                    // (the second's number comes under them); the second only deals damage
    u16 power;                      // damage percent of the user (Alpha 150; + the player's power boost)
    s16 aim;                        // accuracy points the user adds to the move's (the player's aim boost)
    s32 from_x, from_y;             // where it came from (the target is pushed away from it)
    s32 mid_x, mid_y;               // the middle of the user's body (a walking target dodges away from it)
} MonsterHit;

// Called once when the actor appears.
void bhv_monster_init(Actor *self, const void *params);
// Called every tick.
void bhv_monster_update(Actor *self, const void *params);

// The actor's species if it is a monster, else NULL.
const SpeciesData *monster_species(const Actor *a);
// TEAM_PLAYER or TEAM_WILD.
int monster_team(const Actor *a);
// Its level (the player's comes from a variable).
int monster_level(const Actor *a);
// Give a wild monster that just appeared its level and hit points (a wave director's scaling).
void monster_set_level(Actor *a, int level, int hp);
// Damage percent of one of its moves: `power` (0 = its own: 100, Alpha 150, plus its power_var
// boost), + home_power % in its home world.
int monster_power(const Actor *a, int power);
// Ticks a recharge of `ticks` takes it: home_charge % shorter in its home world, else as is.
u32 monster_recharge(const Actor *a, u32 ticks);
// Accuracy points it adds to its moves (its aim_var boost; 0 for most).
int monster_aim(const Actor *a);
// A walking speed at its species' pace (a slower species, e.g. a long-range one; + home_speed % in its
// home world); others: as is.
fixed monster_speed(const Actor *a, fixed speed);
// True if it is a monster that can move and attack now (not paralyzed, frozen, pushed or fainted).
int monster_can_act(const Actor *a);
// True if a status holds it still (paralyzed or frozen).
int monster_held(const Actor *a);
// True if moves can hit it (a fainted or flying monster can't be hit).
int monster_targetable(const Actor *a);
// True if b is a monster that a's moves can hit (the other team, and targetable).
int monster_is_foe(const Actor *a, const Actor *b);
// A move hits monster t: accuracy (the move's + the user's aim - t's dodge - its species' evasion - its walking dodge:
// dodge_walk if it walked in the last dodge_ticks ticks, + dodge_away diagonally away), damage, words,
// flash, push, status, XP, fainting, and the move's own push (t slides away from the hit, unable to move or attack until it stops).
// Returns HIT_*; it plays no sound (the caller plays one per use with monster_hit_sound).
int monster_hit(Actor *t, const MonsterHit *h);
// The sound of a hit result: MISS, or the player's type hit (+ the super ping), or hit / super / weak.
void monster_hit_sound(const MonsterHit *h, int result);
// 1 on a roll of its species' double_hit % (0, and no roll, for a species without it): the damaging
// move it is using hits twice.
int monster_double_roll(const Actor *a);
// The first blow of a double hit (h->blow BLOW_FIRST) just hit the `count` monsters in `hit` (not
// misses): each of them still standing takes the same blow again a moment later, and 2X floats up
// over the user.
void monster_double_hit(Actor *user, Actor *const *hit, int count, const MonsterHit *h);
// Start a buff (BUFF_FLY / BUFF_PHASE) on a monster for `ticks`.
void monster_buff(Actor *a, int buff, int ticks);

#endif
