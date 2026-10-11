# Monster Survival — Game Design Document (GBA)

Target: Game Boy Advance, C/C++, generated entirely by AI.
Pipeline: nodes (JSON) → read-only viewer → change requests → `build.bat` → `run.bat` (emulator).

---

## 1. Concept

A short, top-down **monster survival** game. The player picks one of 9 monsters (one per type) and roams a small overworld drawn in the style of classic GBA monster-collecting games. Enemy monsters wander the map; when the player gets close, they turn hostile and attack. The player fights with auto-charging moves, gains XP from every hit and much more from every kill, and unlocks new moves as they level up.

- **Run length:** ~10 minutes
- **Enemies on screen:** few at a time (not a bullet-hell / horde game)
- **Win:** defeat all **25** enemies
- **Lose:** player HP reaches 0 → back to the Menu scene
- **No healing items.** No evolution.

All monsters, names, sprites, tiles and music are **original**. The style may evoke GBA-era monster games, but nothing is copied from existing games (no existing characters, logos, sprites, tilesets or melodies).

---

## 2. Technical baseline

| Item | Value |
|---|---|
| Screen | 240×160 px = 30×20 tiles |
| Tile | 8×8 px |
| Map size | 3×3 screens = 720×480 px = **90×60 tiles** |
| Frame rate | 60 FPS — all timers are counted in frames (1 s = 60 frames) |
| Video mode | Mode 0, tiled backgrounds + sprites (OAM) |
| Monster sprites | 16×16 px (2×2 tiles), 4-frame walk cycle per direction, 1 attack frame |
| Math | Fixed-point (no floats) |
| Distances | Measured in tiles, using Chebyshev distance (8-direction grid), `dist = max(|dx|, |dy|)` |

**Map streaming:** a regular GBA background holds at most 64×64 tiles, and the map is 90 tiles wide, so the camera must stream map columns/rows into the BG tilemap as it scrolls. The camera follows the player and clamps at map edges.

**Background layers:**
- BG0 — HUD / text
- BG1 — overlays (Sandstorm, screen flashes)
- BG2 — map decoration above ground (tree tops)
- BG3 — map ground

---

## 3. Scenes

### 3.1 Menu Scene (first scene on boot)

**Layout (240×160):**
```
+------------------------------------------+
|           M O N S T E R                  |   ← big chunky title logo
|          S U R V I V A L                 |
|                                          |
|      ◀      [ monster sprite ]      ▶    |   ← large sprite (32×32 scaled 2×)
|              EMBERPUP                    |
|              type: FIRE                  |
|                                          |
|             PRESS START                  |   ← blinks every 0.5 s
+------------------------------------------+
```

**Title "MONSTER SURVIVAL":**
- Big, chunky, blocky letters with a thick dark outline and a light bevel/highlight, in the spirit of classic handheld monster-game title screens (original lettering — not a copy of any existing logo).
- Intro animation: the title drops in from the top of the screen, bounces twice, then a quick white flash. After that the monster selector fades in.

**Monster selector:**
- Shows one monster at a time, its name and type.
- **Left / Right** cycles through the 9 monsters (wraps around). Short slide animation + a cry sound effect for each monster.
- **Start** begins the run with the selected monster.
- The selected monster idles (bobbing animation).

**Music — "Monster Survival Theme" (new, original):**
- Original chiptune composition. It should *feel* like an energetic, adventurous handheld-monster-game title theme, but the melody must be written from scratch — **do not transcribe or reproduce any existing game's melody**.
- Channels: GBA legacy sound — Pulse 1 (lead melody), Pulse 2 (harmony / counter-melody), Wave (bass), Noise (drums).
- Tempo: ~140 BPM, 4/4, key of C major.
- Structure: short 2-bar fanfare intro → A section (8 bars, bright marching melody) → B section (8 bars, slightly lower and building) → loop back to A. The fanfare plays only once; A+B loops.
- Suggested progression for A: C – F – G – C, B: Am – F – G – G7.
- Music data stored in its own node (see section 9) as note/duration arrays per channel.

**Behavior:**
- Menu music starts on boot and loops.
- Coming back after a Game Over or Victory returns here with the last chosen monster pre-selected.

### 3.2 Run Scene

- The player's monster spawns in the **center screen** of the 3×3 map.
- 25 enemies are placed on the map at run start (see section 6).
- Player moves freely in 8 directions. Enemies near the player turn hostile and attack.
- Moves auto-charge; the player fires them with buttons (section 5).
- Run music: a separate original, upbeat loop (same channel setup, faster ~155 BPM). A short jingle plays on level-up and when a new move unlocks.

**End conditions:**
- **Victory:** all 25 enemies defeated → "YOU SURVIVED!" screen with final time, level and kills → press Start → Menu.
- **Game Over:** player HP = 0 → player sprite blinks and fades → "GAME OVER" → after 3 s (or Start) → Menu.

**Pause:** Start during the run pauses (shows level, XP, kills, timer, and which moves are unlocked). Start again resumes.

---

## 4. Controls

| Input | Action |
|---|---|
| D-pad (8 directions, incl. diagonals) | Move. Facing direction = last movement direction |
| **A** (press) | Move 1 (if charged) |
| **B** (press) | Move 2 (if unlocked and charged) |
| **Hold A, no D-pad input, for 3 s** | Move 3 (if unlocked and charged) |
| Start | Menu: start run. Run: pause |
| Left/Right | Menu: change monster |

**Rule for A vs. hold-A:**
- Pressing A fires Move 1 immediately if it's charged.
- If A stays held and the D-pad is untouched, a "hold" counter runs. A small ring fills around the player during the hold.
- At 3 s, if Move 3 is unlocked and charged, Move 3 fires. Touching the D-pad or releasing A resets the hold counter.
- If Move 3 is locked or still charging, the ring shows in grey and nothing fires.

---

## 5. Moves

### 5.1 Charging

Every move has its own charge timer that starts as soon as the move is unlocked. A move can be used only when fully charged; using it restarts its timer.

| Move | Charge time |
|---|---|
| Move 1 | 3 s (180 frames) |
| Move 2 | 10 s (600 frames) |
| Move 3 | 60 s (3600 frames) |

### 5.2 Unlock order

Moves unlock one at a time, in order, by level (section 7). Tuned so a normal run unlocks:
- Move 2 at around **minute 3**
- Move 3 at around **minute 6**

### 5.3 Shapes / targeting

- **Single target (`<= N tiles`):** auto-aims at the **nearest** enemy within N tiles. If none is in range, the move does not fire and does not consume its charge.
- **Triangular (cone):** fires in the facing direction. Starts 1 tile wide next to the player and widens by 1 tile on each side every tile of distance, up to N tiles. Hits every enemy inside.
- **Circular:** hits every enemy within N tiles around the player.
- **Ground zone:** places a hazard on tiles; damages enemies standing on it every 0.5 s while it lasts.
- **Whole screen:** hits every enemy visible on the current camera view.

### 5.4 Move table

| Type | Move 1 | Move 2 | Move 3 |
|---|---|---|---|
| **Normal** | **Tackle** — single target ≤1 tile | **Quick Attack** — dash 1 tile toward the nearest enemy (or facing direction), then single target ≤1 tile | **Growl** — every enemy ≤2 tiles has 33% chance to be **paralyzed 1–3 s**. No damage |
| **Fire** | **Ember** — fire on the 1 tile in front of the player for 1–3 s (ground zone) | **Fire Spin** — circular, ≤3 tiles | **Flamethrower** — single target ≤6 tiles |
| **Water** | **Water Gun** — single target ≤2 tiles | **Bubble Beam** — cone ≤3 tiles | **Surf** — whole screen |
| **Electric** | **Thunder Shock** — single target ≤5 tiles | **Spark** — single target ≤5 tiles, 33% chance to **paralyze 3–5 s** | **Thunderbolt** — cone ≤10 tiles |
| **Grass** | **Vine Whip** — single target ≤3 tiles | **Razor Leaf** — cone ≤3 tiles | **Solar Beam** — cone ≤6 tiles |
| **Poison** | **Poison Sting** — single target ≤1 tile, 50% chance to **poison** | **Acid** — acid on a 3×3 tile area in front of the player for 3 s (ground zone) | **Toxic** — single target ≤4 tiles, 50% chance to **badly poison** |
| **Flying** | **Gust** — cone ≤3 tiles | **Peck** — single target ≤1 tile | **Fly** — 3 s untouchable; player sprite replaced by a shadow, can move freely. No damage |
| **Ghost** | **Lick** — single target ≤1 tile | **Night Shade** — cone ≤3 tiles | **Phase** (Ghost) — 3 s passing through enemies (no body collision). No damage |
| **Rock** | **Rock Throw** — single target ≤3 tiles | **Sandstorm** — 5 s: all enemies on screen freeze (no move, no attack); screen covered by a sand overlay so the player can't see | **Rock Slide** — 3 random 2×2 tile spots on the visible screen get hit by falling rocks |

### 5.5 Move power and accuracy

| Move slot | Base power | Accuracy |
|---|---|---|
| Move 1 | 10 | 95% |
| Move 2 | 18 | 90% |
| Move 3 (damaging) | 40 | 100% |
| Ground zones (Ember, Acid) | 4 per tick (every 0.5 s) | 100% |
| Status-only moves (Growl, Fly, Phase, Sandstorm) | 0 | — |

Accuracy roll fails → "MISS" over the target, no damage, no XP.

### 5.6 Damage formula

```
damage = basePower × (1 + 0.15 × (attackerLevel − 1)) × typeMultiplier
damage = max(1, round(damage))
```

### 5.7 Type chart

Super effective = **×1.5**, not very effective = **×0.75**, neutral = ×1.0. There are **no immunities** — every enemy must be killable by every player type (otherwise a Normal player could never finish the run against Ghosts).

| Attacker ↓ / Defender → | Nor | Fir | Wat | Ele | Gra | Poi | Fly | Gho | Roc |
|---|---|---|---|---|---|---|---|---|---|
| **Normal** | | | | | | | | 0.75 | 0.75 |
| **Fire** | | 0.75 | 0.75 | | 1.5 | | | | 0.75 |
| **Water** | | 1.5 | 0.75 | | 0.75 | | | | 1.5 |
| **Electric** | | | 1.5 | 0.75 | 0.75 | | 1.5 | | |
| **Grass** | | 0.75 | 1.5 | | 0.75 | 0.75 | 0.75 | | 1.5 |
| **Poison** | | | | | 1.5 | 0.75 | | 0.75 | 0.75 |
| **Flying** | | | | 0.75 | 1.5 | | | | 0.75 |
| **Ghost** | 0.75 | | | | | | | 1.5 | |
| **Rock** | | 1.5 | | | | | 1.5 | | |

Empty cell = ×1.0.

### 5.8 Status effects

| Status | Effect | Label |
|---|---|---|
| Paralyzed | Can't move or attack for the duration. Sprite flashes yellow | `PAR` |
| Poisoned | 1 damage per second for 5 s | `PSN` |
| Badly poisoned | Damage per second grows: 1, 2, 3, 4… for 8 s | `TOX` |
| Frozen by Sandstorm | Can't move or attack | (sand overlay) |

A new status replaces the old one. Poison damage gives XP like a hit (section 7).

### 5.9 Animations and floating text

Every move has its own animation (sprite effect, 4–8 frames): e.g. Ember = flickering flame tile, Bubble Beam = bubbles expanding in a cone, Rock Slide = rock shadow growing then rock falling, Surf = wave sweeping across the screen.

Floating text appears over the target and rises for ~0.75 s:
- Damage number (e.g. `12`)
- `MISS`
- `PAR`, `PSN`, `TOX`
- `×1.5!` in a bright color when super effective, `×0.75` in grey when not very effective
- Over the player: `LV UP!`, `NEW MOVE!`

Hit feedback: target flashes white for 4 frames and gets knocked back 2 px.

### 5.10 Collisions

- Bodies block each other (player and enemies can't overlap), but **touching never causes damage**.
- Ghost's Phase disables blocking for the player for 3 s.
- Fly makes the player untargetable for 3 s.

---

## 6. Monsters

### 6.1 Roster — 9 species (one per type)

All original designs.

| # | Name | Type | Look |
|---|---|---|---|
| 1 | **Furrbit** | Normal | Round cream-colored rabbit with big ears |
| 2 | **Emberpup** | Fire | Small red puppy with a flame-tipped tail |
| 3 | **Splashell** | Water | Blue snail with a spiral shell that sprays water |
| 4 | **Voltail** | Electric | Yellow ferret with a lightning-shaped tail |
| 5 | **Sproutle** | Grass | Green turtle with a leafy sprout on its back |
| 6 | **Venomole** | Poison | Purple mole with toxic-green claws |
| 7 | **Gustling** | Flying | Small white-and-sky-blue bird with oversized wings |
| 8 | **Wispook** | Ghost | Floating violet wisp with a little cloth tail |
| 9 | **Pebbulk** | Rock | Stubby grey rock creature with moss on top |

Any of the 9 can be the player. Enemies use the same species.

### 6.2 Enemy population — 25 total

When the player picks a type, the other **8 types** populate the map:
- **3 of each of the 8 types = 24 regular enemies**
- **+1 Alpha** = 25

The **Alpha** is a random species from the 8 enemy types, drawn with an alternate palette and a small crown/aura effect. It has 3× HP and its moves hit harder. It stays dormant in a fixed spot (a clearing in a corner screen) and only wakes up once the other 24 are defeated — a final fight. It gives 3× kill XP.

### 6.3 Enemy stats

| Enemy | Level | HP |
|---|---|---|
| First 8 spawned (one per type) | 2 | 30 |
| Next 8 | 4 | 45 |
| Last 8 | 6 | 60 |
| Alpha | 8 | 180 |

Weaker enemies are placed closer to the center screen, stronger ones in outer screens.

### 6.4 Enemy behavior (AI)

States:
1. **Wander** — walks randomly within its home area, pausing often.
2. **Hostile** — triggered when the player is within **5 tiles**. Walks toward the player and attacks.
3. **Return** — if the player gets more than 10 tiles away, it walks back home and returns to Wander.

Enemy attacks:
- Enemies use only their type's **Move 1** (with the same shape/range rules), on a **4 s** charge, at their own level.
- At most **3 enemies can be Hostile at the same time**. Others that would become hostile wait in Wander until one is defeated. This keeps the screen calm.

When an enemy dies: death animation (flash + fade out, ~0.5 s), then it disappears from the map for good. No respawns.

---

## 7. Progression

### 7.1 XP

| Event | XP |
|---|---|
| Each hit that deals damage (incl. poison/ground ticks) | 2 |
| Killing an enemy | 20 |
| Killing the Alpha | 60 |

### 7.2 Levels

Max level = **10**. Thresholds are set so **20 kills always reaches max level** (20 kills × 20 XP + at least 20 hits × 2 XP = 440).

| Level | Total XP | Unlock |
|---|---|---|
| 1 | 0 | Move 1 |
| 2 | 40 | |
| 3 | 90 | |
| 4 | 140 | **Move 2** (≈ minute 3) |
| 5 | 190 | |
| 6 | 240 | |
| 7 | 290 | **Move 3** (≈ minute 6) |
| 8 | 340 | |
| 9 | 390 | |
| 10 | 440 | Max |

These numbers are the tuning knobs for hitting the minute 3 / minute 6 targets; adjust after playtesting.

### 7.3 Player stats

| Stat | Value |
|---|---|
| Max HP | 60 at level 1, +8 per level |
| Speed | 1 px per frame (diagonals normalized) |

Level-up raises max HP by 8 **and adds 8 to current HP** (only the gained amount — this is not a full heal). This is the only way HP goes up.

---

## 8. Map

### 8.1 Layout (3×3 screens)

Original tileset drawn in the style of GBA monster-game overworlds: soft outlines, bright greens, chunky trees, tall grass patches, dirt paths, flowers, fences, small ponds.

```
+-------------+-------------+-------------+
| NW          | N           | NE          |
| Dense       | Hill path   | Rocky       |
| forest      | with ledges | outcrop     |
+-------------+-------------+-------------+
| W           | CENTER      | E           |
| Pond and    | Start:      | Flower      |
| reeds       | open meadow | field       |
+-------------+-------------+-------------+
| SW          | S           | SE          |
| Old ruins   | Long tall-  | Alpha       |
| (graves)    | grass field | clearing    |
+-------------+-------------+-------------+
```

- Map border = solid row of trees (impassable).
- Paths connect all 9 screens; no dead ends that trap the player.
- Solid tiles: trees, rocks, water, fences, ruin walls. Tall grass and flowers are walkable (visual only).
- Enemy home areas spread so each screen has 2–3 enemies; the center screen has the weakest.
- Collision map stored as a 90×60 array of tile flags (walkable / solid).

### 8.2 Camera

Follows the player with the player kept in the center; stops at map edges. "Whole screen" and "visible screen" moves use the current camera view.

---

## 9. HUD (Run Scene)

```
HP ██████████░░  LV 7          KILLS 15/25
XP ████████░░░░               TIME 06:12

                [ map ]

[A ●]  [B ◔]  [HOLD A ○]
```

- HP bar (green → yellow → red), current level, XP bar to next level.
- Kills counter `X/25`.
- Run timer (mm:ss).
- Bottom: 3 move icons with a fill showing charge progress. Locked moves show a padlock. A fully charged move blinks once.

---

## 10. Suggested nodes

Following the project's node structure (each node in its own JSON, all listed in `nodes.json`; scenes at the root):

```
nodes.json
scenes/
  menu.json              # title anim, selector, music ref
  run.json               # map ref, spawn table, HUD, win/lose rules
  game_over.json
  victory.json
objects/
  player.json
    body.json            # sprite, animations per direction
  enemy.json             # AI states, hostile cap
  monsters/
    furrbit.json ... pebbulk.json   # species: type, palette, sprites, cry
  moves/
    tackle.json ... rock_slide.json # shape, range, power, accuracy, status, anim
  type_chart.json
  progression.json       # XP values, level table, HP growth
  map/
    tileset.json
    overworld.json       # 90×60 tile + collision data, enemy home areas
  hud.json
  floating_text.json
audio/
  menu_theme.json
  run_theme.json
  sfx.json               # cries, hit, miss, level up, new move
```

---

## 11. Decisions taken (change if needed)

These points weren't specified, so this doc picks a value:

1. **9 species, 25 enemies** — 3 of each of the 8 enemy types (24) + 1 Alpha boss as the 25th.
2. **No type immunities** — otherwise some player picks could never win.
3. **Single-target moves auto-aim** at the nearest enemy in range.
4. **Toxic** range = 4 tiles; **Sandstorm** lasts 5 s; **Acid** lasts 3 s.
5. **Growl** rolls 33% for each enemy within 2 tiles.
6. Enemies use only **Move 1**, with a 4 s charge; at most 3 hostile at once.
7. Level-up adds the HP gained to current HP (not a full heal).
8. Moves don't fire (and keep their charge) if no enemy is in range.
9. Moves have accuracy, which is where "MISS" comes from.
10. Ghost's Move 3 is named **Phase** in this doc to avoid confusion with the Ghost type name.
