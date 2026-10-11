// moves - what happens when a monster uses a move node. Its shape (single, dash, beam, circle,
// screen, zone, self) decides where its effect appears, and the move hurts exactly what that
// effect draws: every foe whose body comes within MOVE_HIT_MARGIN px of the effect's picture
// (a whole-screen move: every foe in view). It plays its sound, attack pose and on_use actions,
// hits (monster_hit), then plays one sound for the best result (hit / miss).
// Zones stay on the ground and hurt over time, also by what their effect draws.
// Distances are in tiles (8 px) between the monsters' bodies (hitboxes): 1 = touching, then
// 1 more every 8 px of gap, the larger of across and down.
#ifndef BHV_MOVES_H
#define BHV_MOVES_H

#include "engine/actor.h"

// What moves_use did: nothing (the charge is kept), went off, or went off at nothing without a
// sound of its own (the caller plays a miss sound).
enum { USE_NONE, USE_FIRED, USE_WHIFF };
// Use a move. When it would reach no one it doesn't go off, unless `always` (the player's moves:
// aiming is the player's job): then a targeted move plays straight ahead at full range and hurts
// whatever its effect touches there.
int moves_use(Actor *user, const MoveData *m, int always);
// How close (tiles) a monster should get to its target before using this move.
int moves_reach(const MoveData *m);
// Tiles between two actors' bodies: 1 = touching, +1 every 8 px of gap (larger of across and down).
int moves_tiles(const Actor *a, const Actor *b);
// Age the ground zones and let them hurt. Called by every monster; runs once per frame.
void moves_tick(void);

#endif
