// monster - a creature of a monster-battle game: its species (look, type, moves), level, hit
// points and status, and everything that happens when a move hits it (type chart, damage,
// MISS / PAR / x1.5! words, XP for the player, fainting). moves.c fires the moves.
#ifndef BHV_MONSTER_H
#define BHV_MONSTER_H

#include "engine/actor.h"

enum { TEAM_PLAYER, TEAM_WILD };

// What a hit did, from least to most (a move that hits several keeps the best for its sound).
enum { HIT_NONE, HIT_MISS, HIT_WEAK, HIT_NORMAL, HIT_SUPER };

// What one hit carries (a move used by a monster, or a zone it left on the ground).
typedef struct {
    const MoveData *move;
    u8 team, level, roll;           // roll: 1 = roll the move's accuracy
    u16 power;                      // damage percent of the user (Alpha 150; + the player's power boost)
    s16 aim;                        // accuracy points the user adds to the move's (the player's aim boost)
    s32 from_x, from_y;             // where it came from (the target is pushed away from it)
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
// Damage percent of its moves (100, Alpha 150), plus its power_var boost.
int monster_power(const Actor *a);
// Accuracy points it adds to its moves (its aim_var boost; 0 for most).
int monster_aim(const Actor *a);
// A walking speed at its species' pace (a slower species, e.g. a long-range one); others: as is.
fixed monster_speed(const Actor *a, fixed speed);
// True if it is a monster that can move and attack now (not paralyzed, frozen, pushed or fainted).
int monster_can_act(const Actor *a);
// True if a status holds it still (paralyzed or frozen).
int monster_held(const Actor *a);
// True if moves can hit it (a fainted or flying monster can't be hit).
int monster_targetable(const Actor *a);
// True if b is a monster that a's moves can hit (the other team, and targetable).
int monster_is_foe(const Actor *a, const Actor *b);
// A move hits monster t: accuracy (the move's + the user's aim - t's dodge), damage, words,
// flash, push, status, XP, fainting, and the move's own push (t slides away from the hit, unable to move or attack until it stops).
// Returns HIT_*; it plays no sound (the caller plays one per use with monster_hit_sound).
int monster_hit(Actor *t, const MonsterHit *h);
// The sound of a hit result: MISS, or the player's type hit (+ the super ping), or hit / super / weak.
void monster_hit_sound(const MonsterHit *h, int result);
// Start a buff (BUFF_FLY / BUFF_PHASE) on a monster for `ticks`.
void monster_buff(Actor *a, int buff, int ticks);

#endif
