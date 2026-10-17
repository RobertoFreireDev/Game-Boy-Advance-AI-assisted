# NEW_GAME.md — Playbook for creating (or replacing) a game

Read this **after `CLAUDE.md`** whenever the human asks for a new game, a different genre, or
"remove the current game and make X". It tells you what to load, in what order, what you can
skip, and how to test the result. It is AI-maintained: update it when you learn something new.

---

## 1. Load context efficiently

The repo is ~10k lines of engine/tools plus a handful of template nodes (a full game is ~150-350). Don't read everything. Read in
**parallel batches** (several Read calls in one message) and stop at the tier you need.

### Tier 1 — always (enough to plan most games)

| Read | Why |
|------|-----|
| `CLAUDE.md` | The contract (node format, rules, engine, limits). |
| this file | Workflow, testing, pitfalls. |
| `nodes/nodes.json` | Current game settings, variables, the full node index. |
| `catalog/behaviors.json` | Every behavior and its params — **the real menu of what actors can do**. |
| `catalog/actions.json` | Every action and its params. |
| `git log --oneline -10` (+ `git show --stat` of the last game commit) | What the last game built, which nodes were kept as generic, engine features added for it. |
| `src/engine/config.h` | Pool limits (actors, particles, sprite sheets, icons…). |

### Tier 2 — the nodes and behaviors close to the requested genre

- 1 example of each node type you'll create, taken from the current game (§3 lists good ones).
- The `.c` of each behavior you plan to use in an unusual way (they are 25–110 lines each).

### Tier 3 — only if the game needs a new behavior, action, field or node type

| Read | When |
|------|------|
| `src/engine/actor.h`, `src/engine/data.h` | Writing a behavior (Actor struct, `bhv_state`, generated data layout). |
| `src/engine/physics.iwram.c`, `actor.iwram.c` | Changing movement, contacts, spawning. |
| `tools/codegen.py` → `class Gen` (`action()` ~line 123) | New action or new node field. |
| `tools/validate.py` → the matching `V_<type>()` function | New field/type (Grep for it; don't read all 1.3k lines). |
| `visualizer/visualizer.html` | New type/field preview (Grep for the type name; 1.4k lines). |
| `src/engine/ui.c` (548 lines) | HUD/menu/dialog features. |
| `src/engine/audio.c`, `bg.c`, `sprites.iwram.c`, `scene.c` | Only when touching that subsystem. |

**Use Grep before Read** on the big files (`validate.py`, `codegen.py`, `visualizer.html`,
`ui.c`). Never read `generated/`, `build/`, `dist/` or `visualizer/data.js` for context.

---

## 2. Genre → building blocks that already exist

Three games have been built so far: **Hero Quest** (side platformer, commit `ba0261a^`),
**Sun Shrine** (top-down Zelda-like, commit `1c794e3^`), **Night Swarm** (survivors-like, branch
`games/night_swarm`). `main` is now an **empty template** (one start scene + the generic UI
nodes), but their engine features stayed, so all three genres are covered without new code:

| Genre | Player | Enemies | World / progression |
|-------|--------|---------|---------------------|
| Platformer | `platformer_controller`, `health`, `camera_target` | `patrol`, `chase_player`, `stompable`, `damage_on_touch` | `follow_path` + `solid_platform` lifts, `collectible`, `trigger_zone` exits, gravity bodies, one-way/ladder/hazard tiles |
| Top-down adventure | `topdown_controller`, `sword_attack`, `health` | `wander`, `chase_player`, `damage_on_touch` | `talk` + dialogs, `locked_door`, `if_var` flags, directional animation slots (`*_up`, `*_down`, `*_left`, diagonals) |
| Survivors / bullet heaven | `topdown_controller` (`speed_var`), `weapon` ×N, `aura` | `swarm`, `spawn_wave` (on an invisible `prop` director) | `projectile` props, `magnet` gems, `level_up` + `upgrade` nodes + upgrade menu, `run_clock`, `persistent` vars + `save_game` shop |
| Monster battler (**Monster Survival**, branch `games/monster_survival`) | `monster` (`species_var`, HP/XP/level vars, walking dodge `dodge_walk` / `dodge_away`), `topdown_controller`, `move_buttons` | `monster` (species per instance, `avoid_var`), `monster_ai`; a sleeping Alpha (`random`, `alpha`, `wake_var`, all 3 moves via `monster_ai` `moves`) | `species` + `move` nodes (each move's `travel` picture flies from the user to its effect: one shared sheet `spr_fx_travel`), `game.battle` (type chart, floating words, the player's hit sound per type), solid top-down bodies, `species_select` picker (locked species as silhouettes, each unlocked by the first win of a monster that never won, saved with `save_game`), `drop_in` logo, `cover_screen` / `flash_screen` / `float_text`, gauges and `{var.name}` text, floor items (`pickup` + `random_spot` + `collectible` `as_player`) feeding boost variables (`power_var`, `aim_var`, `dodge_var`, `charge_var`, `speed_var`), a boss that levels up each minute (`run_clock` `on_minute` → its `level_var`), one world per type (the run scene's two map instances have `variants` picked by a `world` variable that the picker sets with `random_var`; a monster of the world's type is at home: `monster` `home_var`, `on_home` words) |

If the genre is new (shmup, puzzle, racing, RPG battles…), map it to these first, then list
the **missing** behaviors/actions and ask the human before adding a node type or changing
engine architecture (CLAUDE.md rule 8).

---

## 3. Good example nodes to copy from (Night Swarm)

These live on the `games/night_swarm` branch, not on `main`. Read them without switching branch:
`git show games/night_swarm:nodes/objects/player/plr_hunter.json`.

| Need | Example |
|------|---------|
| Player actor + logic list | `nodes/objects/player/plr_hunter.json` |
| Body with up/down slots | `nodes/objects/body/body_hunter.json` |
| Enemy | `nodes/objects/enemy/enm_bat.json`, boss `enm_lord.json` |
| Invisible logic actor | `nodes/objects/prop/prop_director.json` |
| Shot | `nodes/objects/prop/prop_bolt.json` |
| Pickup | `nodes/objects/pickup/item_gem.json` |
| Big streamed map | `nodes/objects/tilemap/map_field.json` (96×96 tiles) |
| Level scene | `nodes/scenes/scn_field.json`; title `scn_title.json` |
| HUD / menu / dialog | `hud_run`, `menu_title`, `menu_level_up`, `dlg_how_to_play` |
| Music / sfx | `mus_field`, `sfx_hurt` |

For a monster battler, copy from `games/monster_survival` instead: species `mon_emberpup` (owns
its body, portrait, Alpha palette, cry and moves), moves `mov_ember` (zone), `mov_bubble_beam`
(beam), `mov_rock_slide` (screen zone), `mov_fly` (self buff), `anim_travel_*` + `spr_fx_travel` (travel pictures, 8×8, 2 frames each); actors `plr_monster`, `enm_wild`
(species per instance), `enm_alpha`; the picker `prop_picker` + `hud_select` + `scn_menu`; the
pause screen `menu_pause` (colored texts, `color_by` greys a locked move, a `row` of options,
RESTART = `reset_vars` + `goto_scene` the run again); `game.battle` in
nodes.json. Its art, map and audio came from a scratchpad generator (session a013694e,
`scratchpad/ms/`: `main.py` writes every node; `monsters.py`, `effects.py`, `tiles.py`,
`mapgen.py`, `title.py`, `ui_art.py`, `audio.py`; `sheet_*.py` / `render_map.py` draw PNG previews).
The monster sprites, portraits and move effects were redrawn later (session 464f6e56, `scratchpad/art/`):
`base.py` (each species' front / back / side pose, semantic chars), `frames.py` (breathe, blink, walk,
lunge frames derived from them + a closed-outline check), `portrait.py` (32x32 cel-shaded portraits
from shapes), `fx.py` (every effect, with a no-edge check), `gen_mons.py` / `gen_fx.py` (write the
nodes), `bench.py` / `fxview.py` / `pview.py` (PNG previews). Harness additions in `scratchpad/t/`:
`test_move3.py <species>` (R, screenshot move 3 every 10 frames).
The 8 other worlds (Ember Crater, Coral Lagoon, Storm Plateau, Verdant Jungle, Miasma Swamp, Sky Isle,
Haunted Hollow, Boulder Canyon; the meadow is the Normal world) come from a generator; newest copy in
session f1a15eff, `scratchpad/w/` (the older ones in 538543de / 1079b7b2 / ff69787e would undo later fixes;
538543de's would bring back the leaf, puddle, feather and fossil pictures):
`gen.py [--repo] [world…]` writes each world's palettes, tilesets and maps (and PNG previews in `out/`);
`worlds.py` = ground art (palettes keep the meadow's slot layout so its path pieces recolor themselves;
`DENS` = the Alpha's 4x4 den per world), `newtiles.py` = pieces redrawn a pixel inside their tile + the
plain-ground '0' '1' '2' (where stone paving was), `border.py` = the painted borders drawn over the
monsters (a 32 px texture + a 2-tile strip per side + 16 px corner blocks keeps each under ~80 tiles),
`softstamps.py` = the first pictures of grass / poison / flying / rock (moss, bog, white clover, cracked
earth: no outline, a dithered rim, ground shades, so they read as part of the ground), `flat.py` = the
ground-level borders (the field's ground runs on 1-4 px into the band, then the shore),
`layout.py` = paths between screen hubs (auto-tiled, never 1 tile wide), plazas, patches, stamps.
`meadow.py [--repo]` rewrites only the Normal world's flowers, '0' '1' '2', den and forest canopy (its map
is otherwise hand-kept; run it once on a fresh tree). `seams.py` lists things cut off by a tile edge in
every ground map; `rr.py <world> <out.png> [scale] [x y w h]` renders a world from node JSON.
`scratchpad/t/test_worlds.py` plays 12 runs + a RESTART in the emulator: maps per world, every
monster's home bonus, real walking / recharge / damage, the floating words, a screenshot per world.

**The waves mode** (Monster Survival's second game mode, session 944e2664): the menu's picker shows the 9
monsters twice (`species_select` `passes` 2: entries 9-17 = the waves mode, locked until all 9 have won a
classic run; `entry` indexes the 18 best times). The classic run's nodes are untouched; the mode has its
own scenes (`scn_waves`, `scn_waves_victory`, `scn_waves_over`), player (`plr_waves`: `level_by_level`,
`xp_grow`, `on_level_up` opens the upgrade-card menu `menu_power`), monsters (`enm_wave_1/2/3`: `walls`
false, `monster_ai` `hunt`; `enm_wave_boss`), the director `prop_waves` (`monster_waves`), 7 `upg_*` cards,
`mus_waves`. The 2x2-screen arena = the world's ground map cut down (`map_arena_<world>`, layer 2, the
ground tilesets reused as they are) under a collision layer `map_arena_edge_<world>` that fades the
floor into the black backdrop with the world's own darkest ground shades (5, 6, 1: every ground
palette has that ramp) and is solid everywhere but the floor. Generator: session 944e2664 scratchpad
`waves/` (`gen_nodes.py` writes every node + the index + the shared menu edits; `art.py` = edge fade,
card icons, red arrows; `music.py`; `preview.py` = PNGs; `balance.py` = the rough balance model).
Tests in its `t/`: `test_classic.py` (the same bot plays the classic mode on a `git archive HEAD` copy
and on the new tree; per-checkpoint state hashes must match: proof a change left the classic mode
alone), `test_waves.py` (functional: menu loop / locks / unlock, arena walls, edge spawns out of view,
pause RESTART, waves, cards, boss, victory best time; `balance <species>`: the bot with no help;
`perf <species>`: god mode, per-phase and per-behavior CPU of each wave's worst frame; START_WAVE=n
skips ahead).

**Generic nodes kept across games** (on `main` too; reuse, recolor if needed, don't delete):
`font_default`, `pal_ui`, `icon_cursor`, `icon_heart`, `sfx_select`, `sfx_confirm`.

---

## 4. Workflow for a new game

1. **Clarify once.** If it's unclear whether to *replace* the current game or *add* scenes to it,
   or the core loop is ambiguous (run length, lives, save), ask **one** question. Otherwise pick
   and say what you picked.
2. **Plan the node list** before writing files: scenes → actors → bodies/sprites/animations →
   maps/tilesets → UI → audio → variables. Check it against `config.h` limits and GBA limits
   (CLAUDE.md §9): palettes (16 OBJ + 16 BG banks), OBJ VRAM 1024 tiles, ≤ 32 sprite sheets.
   Sheets with more than 64 tiles stream (only their frames on screen use VRAM), so give actors
   generous animations (idle breathing + blink, 4-step walks, a wind-up + lunge attack).
3. **Start from the template**: work on `main` (empty template) or a new `games/<name>` branch
   made from it. When replacing a game, delete its node files *and* their `nodes.json` entries in
   the same change; delete `src/game/*` that only the old game used. Keep engine behaviors —
   they are reusable. Replace the template's `scn_start` / `hud_start` placeholders.
4. **Update `game`** in `nodes.json`: `title` (≤ 12), `game_code` (4 chars, new one),
   `rom_name`, `start_scene`, `save`, `variables`.
5. **Write nodes bottom-up**: palettes → sprites → animations → particles → bodies → actors →
   tilesets/tilemaps → fonts/icons → hud/menus/dialogs → sfx/music → scenes.
   Run `python tools/validate.py` after each batch, not only at the end.
6. **Extend only what's missing**: a new behavior = `src/behaviors/<name>.c/.h` +
   `catalog/behaviors.json` entry (codegen auto-registers `bhv_<name>_init/update/on_touch`).
   A new action = `catalog/actions.json` + `codegen.py Gen.action` + `src/engine/actions.c`.
   A new field/type = the four mirrors (CLAUDE.md, validate, codegen, visualizer).
7. `python tools/build.py` (zero warnings), then **play-test** (§5).
8. Update `CLAUDE.md` for any new behavior/action/field, commit (the message lists the nodes and
   engine changes), report.

---

## 5. Testing the ROM — Python only, never Lua

- **Tests are driven by Python (stdlib only)**, like the repo's tools. **Do not use mGBA Lua
  scripting** — the human asked for Python explicitly, and mGBA 0.10.5 here has **no `--script`**
  option anyway (Lua is GUI-only).
- Tools available: `C:\Program Files\mGBA\mGBA.exe` (has a GDB stub) and
  `C:\devkitPro\devkitARM\bin\arm-none-eabi-gdb.exe`.
- Quick visual check: screenshot the mGBA window, or headless-screenshot the visualizer
  (`SETUP.md` §3.5). Never inject key presses into the desktop.

### Headless play-test / profiling harness

The test itself (scripted input, patches, checks) lives in the **session scratchpad**, never in
the repo. Building and running it go through **`tools/emutest.py`** (in the repo, game-agnostic,
stdlib only), never a hand-rolled `Popen` + `sleep`: `build_test_rom(out, input_c, patches)` then
`run(elf, gba, script_lines, out, done=...)`. `python tools/emutest.py selftest` proves its checks
work (~1 minute). It turns every way a run goes wrong into an `EmuError` within seconds, instead of
a test that waits forever while a paused emulator sits on the menu:

| kind | what happened |
|---|---|
| `start` | mGBA didn't open the GDB port, gdb couldn't connect, a patch anchor is missing, the build failed |
| `crash` | the game crashed: mGBA's "The game has crashed" box, or `Jumped to invalid address` in its log |
| `signal` | the CPU hit an illegal instruction (SIGILL): registers and a backtrace come with it |
| `gdb` | a gdb command failed (unknown symbol, bad format): the rest of the script never ran |
| `exit` | mGBA or gdb quit on its own |
| `stall` | no output for `stall` s (a hang): a second gdb attaches and prints where the CPU is looping |
| `stuck` | the game sat on one non-level scene (menu, title, game over) for `stuck_frames` frames: the test's input never got past it (a locked entry, a press on the wrong frame). Needs `SCENE_PRINT` |
| `stack` | the stack came within 128 bytes of the IWRAM variables. Needs `STACK_PRINT` and the input calling `emutest_stack_paint()` on frame 1 and `emutest_stack_check()` each frame |
| `timeout` | the whole run took longer than `timeout` s |

The emulator runs hidden for the whole run (a test window paused by gdb looks like a frozen game
to the human, and mGBA's crash box would pop up on their screen) and dies with the Python process
(a Windows job object), so an interrupted test never leaves an emulator behind. It takes the
machine-wide lock (`%TEMP%\gba-gdb-2345.lock`, stale when its owner pid is gone) and kills a test
emulator a dead harness left on port 2345. mGBA's own log goes to `<out>/mgba.log`; run() prints a
summary of its warnings (bad memory reads, illegal opcodes) at the end. `NULL_CHECK` as
`extra_cflags` makes every NULL pointer read stop the game with a backtrace.

1. **Test ROM** (`build_test_rom`): all sources with `-g` (`*.iwram.c` with `build.py`'s
   `IWRAM_FLAGS`), after validate + codegen of the tree it builds (a `git archive` snapshot works
   too, for old-vs-new replays), with two test changes:
   - `src/engine/input.c` → a scripted-input version: auto-plays (walks long lines in 8
     directions, taps A in menus), counts lag frames with cascaded timers TM2/TM3, and calls an
     empty `test_checkpoint()` every 600 frames. Give it knobs gdb can set at a checkpoint:
     clock warp, god mode, max loadout (write the weapon/item variables every frame), freeze XP
     (no level-up pauses), immortal enemies (fills the pool: the true upper bound).
   - Every other change is a **patch applied to a copy of the real source** at build time
     (Python `str.replace` with an assert that the anchor exists), never a hand-made copy of a
     file: a copied `core.c` silently kept an old slow `rng_range` and gave wrong profiles.
     Patch `core.c`'s loop with per-phase cycle counters (worst frame and its breakdown, frames
     over 70/80/90/100 %), and optionally wrap functions (`actor_spawn`, `health_damage`,
     behavior updates, scene loads) with timers.
   - Per-call timers cost ~100 cycles each (hundreds per frame in a horde): keep a "light" mode
     with only the per-phase counters for the real numbers.
   Alternatively change a *copy* of `nodes/` (start scene, player position) and build the copy.
2. **Run** (`run`): mGBA `-g`, then `arm-none-eabi-gdb -batch` with your script lines (run() adds
   `target remote :2345` before them and a crash report after them). In them: `break
   test_checkpoint`; at each hit `dump binary memory` VRAM / palette / OAM / IO, print stats (lag,
   cycle counters, actor count), `SCENE_PRINT`, `STACK_PRINT`, `set var` to warp the clock or give
   items, `continue`; the last line is `continue`. `done(line, log)` says when the test has what it
   needs.
3. **Render**: a Python script turns the dumps into a PNG (mode-0 BG layers + sprites). BG
   scroll registers are write-only, so read scroll from `bg.c`'s `s_layers` instead.

Pitfalls:
- **The test's own variables take the stack's room.** Plain statics of the test input live in
  IWRAM, between the game's variables and the stack. A waves-mode test ROM kept 1.5 KB of arrays
  there, had 756 bytes of stack left, and crashed in wave 10 (which needs 776): it looked like a game
  crash, but the game itself has 2.2 KB of room. Put big test arrays in `EWRAM_BSS` (cleared on frame
  1); `build_test_rom` fails below 1 KB of room and notes when the test ROM has less than the game.
- Before blaming the game for an `EmuError`, read its kind and the backtrace: a `gdb` or `stuck`
  error is the test's own fault; a `crash` in a test ROM with less stack room than the game may be too.
- mGBA's log level words are in the system's language (`[ERRO DO JOGO]`, `[AVISAR]` here), the
  messages in English. `Bad BIOS Load8` of 0x0-0x3 is gdb reading the PC as it attaches (run()
  leaves those out); a NULL read by the game shows up as a backtrace in a `NULL_CHECK` build.
- `set var` on initialized `.data` **before crt0 runs** gets overwritten — change test knobs at
  a checkpoint, not at the start. That includes the checkpoint interval itself: start every 30
  frames and set the real interval at checkpoint 1.
- `EWRAM_BSS` pools are not zeroed at boot; the engine clears them itself — keep it that way.
- gdb prints a checkpoint's `SCROLL` line *before* its `SHOT` line: pair each SCROLL with the SHOT
  that follows it, or the renderer draws the map one checkpoint late (it looks like missing rows).
- A long run with a bot needs help past walls: when it can't reach its target for ~10 s, teleport
  it next to it (test-only) and count the teleports; list the live monsters (x, y, AI state) at each
  checkpoint with a gdb `while` loop over an array the test input fills.
- Monster Survival's harness (session a013694e scratchpad `t/`): `input_auto.c` (fight bot, god
  mode, per-phase cycle counters, enemy list), `test_ms.py` (menu → run, screenshots),
  `test_long.py` (a whole run to victory), `test_species.py <n>` (one species, every move charged,
  contact sheet), `test_flow.py` (pause, game over, victory), `check_bg.py` (streamed map vs node).

---

## 6. Performance lessons (Night Swarm, measured in the emulator)

One frame = 280,896 CPU cycles. Measured worst frames (no lag in any of them): normal play 54 %,
max loadout + level-ups + boss 72 %, a full pool of ~70 monsters 70 %, max loadout against a
full pool of monsters that never die (unreachable upper bound) ~90 %. Reuse these, don't redo
them:
- Actor pool (96) in EWRAM; contacts tested only for listed categories via per-frame buckets
  with cached hitboxes, built in the same pass that moves actors; big buckets that many actors
  look into (a horde vs a volley) are binned into a 32 px grid. Free movers skip collision code.
- Hot files are ARM code in IWRAM (`*.iwram.c`): actor, physics, anim, sprites, `swarm`,
  `projectile`, `health`. IWRAM is 32 KB shared with the code, every plain `static` and the stack
  (~26 KB used, ~5.9 KB stack) — move code there only after measuring. When Monster Survival
  pushed it over, the stack silently overwrote variables (the game hung on boot, the link was
  clean); `build.py` now fails below 2 KB of stack. Cold pools (particles, scripts, floating
  texts, zones, the upgrade tables) live in EWRAM.
- **Thumb code is the trap**: no divide instruction and no 64-bit multiply. A `%` or `/` by a
  variable is a ~300–1000 cycle library call, by a constant still a call, and `fx_mul` is a call
  too. `rng_range` once cost ~1000 cycles (now one multiply); `projectile_launch` cost ~1500
  (now ARM). Keep per-actor-per-tick Thumb code free of them.
- Pool questions are O(1): live count per node, free slots, first free slot, cached player
  (a missing player is searched once per frame, not once per monster).
- Bursts are spread: `spawn_wave` spawns at most 3 monsters per tick for all waves together;
  an `aura` pulse sweeps the pool in 3 slices on 3 ticks (kill bursts spawn gems and run scripts).
- Horde: aim every 8 frames; crowd check reads the 2x2 grid cells the push zone can touch;
  EWRAM fields are read once into locals.
- Sprites: sheet numbers cached in IWRAM, far off-screen actors skipped early.
- UI: the HUD redraws from the first changed element on (not everything on every change);
  menus/boxes write whole rows; paper-font tiles are converted with bit tricks, not per pixel.
- Scene loads (~40–60 % of a frame, under a black fade): the actor pool is wiped once at boot,
  later only freed; maps stream with a pointer per row.

Monster Survival (25 solid top-down monsters that block each other, measured): normal fighting
~52 %, worst frame 74 % (a move hitting several monsters), no lag except the scene-load frame
under the fade. Solid actors made physics the hog (~200k cycles: every mover tested every solid
in slow EWRAM, twice); now the solids' hitboxes are cached in IWRAM once per frame (updated as
each one moves) and checked first, and a body standing still skips movement entirely (~50k).

Monster Survival's waves mode (wave 10: 24 hunting monsters + a boss, all with 3 moves, measured
2026-10-16): 25 monsters cost ~210k cycles before anyone attacks (AI ~70k, physics ~55k, sprites
~35k); one wild move use adds 40-120k (its effect is planned particle by particle along each
flight, and a boss may try moves 3 and 2 before 1), and the HUD redraw after a hit ~34k: 20 frames
of a 70 s wave went over budget. Fixed with at most 16 out at once (`monster_waves` `max_alive`, the
rest walk in as others faint), waiting hunters that think every 4th frame with one attack per
frame, and a HUD that lists its fast-changing elements last (it redraws from the first changed one
on). Now: worst frame 90-108 % over the 9 species, 0-2 frames over budget per wave 10.

Monster Survival perf pass (2026-10-17, Rock Slide felt slow; the game must play exactly the same):
a PC sampler (timer 0 interrupt every ~3000 cycles, chained in front of libtonc's `isr_master`; the
interrupted PC is the BIOS-saved `lr_irq` at `[sp_irq + 20]`, the caller of a leaf is the system-mode
`lr`; each sample tagged with the frame phase / running behavior) found the costs in minutes, where
per-function timers had only said "the scene phase". Findings: ~20 % of busy time was Thumb calls
through veneers into one-line IWRAM helpers (`actor_logic_index`, `actor_index`... each `mon()` asks
two), ~5-12 % software division (`/ 100` by a constant is a ROM library call of ~300 cycles: damage,
damage numbers, recharge), `touch_tiles` scanning every actor's tiles for hazard / ladder flags no
tileset had, and the IWRAM full of behaviors the game never uses (swarm's 2 KB grid, projectile,
health). Fixes: the helpers inline in actor.h, codegen registers only the behaviors nodes use (6.6 KB
of IWRAM back), `monster_ai` moved to IWRAM (it was the biggest share; ARM also divides by a constant
with a multiply) and caches what never changes while a monster lives, `physics_free_at` (every
knockback) in IWRAM, exact `fx_div100` / `fx_div10`, `touch_tiles` skipped on maps without such
tiles. Result (25 monsters, Pebbulk firing Rock Slide every 150 frames): classic ordinary frames
44 % -> 33 % on average, worst 113 % -> 84 % (21 frames over 85 % -> 0), Rock Slide's press 97 % ->
66 % and landing 89 % -> 66 % at worst; waves wave 8 worst 85 % -> 69 %; a hit 13k -> 8k cycles,
`monster_ai` 35k -> 15-17k a frame. Proof that nothing changed: the same bot played 18 runs (every
monster, both modes, 90,000 frames) on the old and new builds, folding every actor, variable,
particle, the random generator and the sprite list into a hash each frame: identical at every
checkpoint (a 3 px instead of 2 px knockback differs by frame 600).

If a new game pushes past this (more actors, bullets, particles), measure with the harness
(§5) before optimizing, and record the result in the commit message and here.

---

## 7. Common mistakes to avoid

- Hard-coding content in C instead of nodes; editing generated files.
- Forgetting a `nodes.json` entry or a `children`/`parent` mismatch (the validator catches it —
  run it often).
- Menu options whose actions don't `close_menu` / `goto_scene` / `open_menu` / `show_dialog`
  (the menu stays locked).
- Two fonts in one scene for HUD + box-less menu (BG0 has one font slot).
- Icons on boxes in a palette other than the font's (transparent pixels show the map).
- Sprite sizes that aren't valid OBJ sizes; UI positions that aren't multiples of 8.
- Bash heredocs with long multi-line text: write the script to a scratchpad file instead.
- Painted backdrops (title art, skylines): paint the whole 240x160 picture in a scratchpad Python
  script, cut it into 8x8 tiles and merge identical ones. A tileset has only 93 tile characters, so
  split a busy picture over layers 3 (far) and 2 (near), make hill lines repeat every 80 px, keep
  dithers and edges on the tile grid, and snap small repeated details (eyes, posts) to the same
  spot inside a tile. Night Swarm's map_sky / map_night / map_logo were made this way.
- Redrawing a level map: generate it from the committed layout (`git show HEAD:...`), never from
  the file you just rewrote, and assert that the solid cells did not move.
- Node fields named like a common field: `type` is always the node type, so a monster's type is
  `monster_type` (a `type` field silently replaced the node type and broke the index).
- Original designs only: check every character against well-known games (a yellow critter with
  red cheeks reads as a famous mascot; Voltail got a ferret mask instead).
- OBJ palettes run out fast with many species + effects + colored floating text: share palettes
  between pairs of species (6 colors each), one effect palette, and count what the validator
  reports (Monster Survival uses 15 of 16 in a run).
