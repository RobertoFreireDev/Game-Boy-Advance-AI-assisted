// moves - what happens when a monster uses a move node. Its shape (single, dash, beam, circle,
// screen, zone, self) decides where its effect appears, and the move hurts exactly what that
// effect draws: every foe whose body comes within MOVE_HIT_MARGIN px of the effect's picture
// (a whole-screen move: every foe in view). It plays its sound, attack pose and on_use actions,
// hits (monster_hit), then plays one sound for the best result (hit / miss).
// Zones stay on the ground and hurt over time, also by what their effect draws.
// The effect is shown in 3 steps over the time it lasts (a picture only, the hits don't wait):
// its travel picture comes out of the user, flies to where the effect plays, then the effect plays
// there (MoveData step1 / step2; whole-screen moves play as they are).
// Distances are in tiles (8 px) between the monsters' bodies (hitboxes): 1 = touching, then
// 1 more every 8 px of straight-line gap, so a reach is a circle around the body.
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
// Is b within `tiles` tiles of a? 1 = touching, +1 every 8 px of straight-line gap (a circle).
int moves_within(const Actor *a, const Actor *b, int tiles);
// Age the ground zones and let them hurt. Called by every monster; runs once per frame.
void moves_tick(void);

#endif
