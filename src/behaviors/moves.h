// moves - what happens when a monster uses a move node: it finds its targets by the move's
// shape (single, dash, cone, circle, screen, zone, self), plays its sound, effect and on_use
// actions, and hits them (monster_hit). Zones stay on the ground and hurt over time.
// Distances are in tiles (8 px), measured as max(|dx|, |dy|) between the monsters' feet.
#ifndef BHV_MOVES_H
#define BHV_MOVES_H

#include "engine/actor.h"

// Use a move. Returns 1 if it fired, 0 if nothing was in reach (the charge is then kept).
int moves_use(Actor *user, const MoveData *m);
// How close (tiles) a monster should get to its target before using this move.
int moves_reach(const MoveData *m);
// Tiles between two actors' feet (max of across and down, rounded).
int moves_tiles(const Actor *a, const Actor *b);
// Age the ground zones and let them hurt. Called by every monster; runs once per frame.
void moves_tick(void);

#endif
