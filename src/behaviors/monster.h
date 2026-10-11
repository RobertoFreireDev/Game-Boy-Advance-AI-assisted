// monster - a creature of a monster-battle game: its species (look, type, moves), level, hit
// points and status, and everything that happens when a move hits it (type chart, damage,
// MISS / PAR / x1.5! words, XP for the player, fainting). moves.c fires the moves.
#ifndef BHV_MONSTER_H
#define BHV_MONSTER_H

#include "engine/actor.h"

enum { TEAM_PLAYER, TEAM_WILD };

// What one hit carries (a move used by a monster, or a zone it left on the ground).
typedef struct {
    const MoveData *move;
    u8 team, level, roll;           // roll: 1 = roll the move's accuracy
    u16 power;                      // damage percent of the user (Alpha 150)
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
// Damage percent of its moves (100, Alpha 150).
int monster_power(const Actor *a);
// True if it is a monster that can move and attack now (not paralyzed, frozen or fainted).
int monster_can_act(const Actor *a);
// True if moves can hit it (a fainted or flying monster can't be hit).
int monster_targetable(const Actor *a);
// True if b is a monster that a's moves can hit (the other team, and targetable).
int monster_is_foe(const Actor *a, const Actor *b);
// A move hits monster t: accuracy, damage, words, flash, push, status, XP, fainting.
void monster_hit(Actor *t, const MonsterHit *h);
// Start a buff (BUFF_FLY / BUFF_PHASE) on a monster for `ticks`.
void monster_buff(Actor *a, int buff, int ticks);

#endif
