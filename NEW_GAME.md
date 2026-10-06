# NEW_GAME.md — Playbook for creating (or replacing) a game

Read this **after `CLAUDE.md`** whenever the human asks for a new game, a different genre, or
"remove the current game and make X". It tells you what to load, in what order, what you can
skip, and how to test the result. It is AI-maintained: update it when you learn something new.

---

## 1. Load context efficiently

The repo is ~8.6k lines of engine/tools plus a handful of template nodes (a full game is ~150). Don't read everything. Read in
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

Games built so far: **Hero Quest** (side platformer, commit `ba0261a^`), **Sun Shrine** (top-down
Zelda-like, commit `1c794e3^`), **Night Swarm** (survivors-like, branch `games/night_swarm`),
**Void Vessel** (metroidvania, branch `games/void_vessel`) and **Mossbrook** (cozy village life sim,
branch `games/mossbrook`). `main` is an **empty template** (one start scene + the generic UI nodes)
with the engine features of the first three games; Void Vessel and Mossbrook added theirs on their own
branches (start from that branch, or merge its engine commit, to reuse them):

| Genre | Player | Enemies | World / progression |
|-------|--------|---------|---------------------|
| Platformer | `platformer_controller`, `health`, `camera_target` | `patrol`, `chase_player`, `stompable`, `damage_on_touch` | `follow_path` + `solid_platform` lifts, `collectible`, `trigger_zone` exits, gravity bodies, one-way/ladder/hazard tiles |
| Top-down adventure | `topdown_controller`, `sword_attack`, `health` | `wander`, `chase_player`, `damage_on_touch` | `talk` + dialogs, `locked_door`, `if_var` flags, `*_up`/`*_down` animation slots |
| Village life sim (Mossbrook branch) | `topdown_controller` (run), `interactor`, `sword_attack` as the bug net | bugs: `wander` + `health` + `exists_when` (hours) | `interact` (talk, shake, dig), `day_clock` + `set_darkness`, `fish_bite`, `exists_when` daily flags, menus that sell with `add_var times`, `save` + bed, `game.y_sort` |
| Survivors / bullet heaven | `topdown_controller` (`speed_var`), `weapon` ×N, `aura` | `swarm`, `spawn_wave` (on an invisible `prop` director) | `projectile` props, `magnet` gems, `level_up` + `upgrade` nodes + upgrade menu, `run_clock`, `persistent` vars + `save_game` shop |

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

Build it in the **session scratchpad**, never in the repo (it is not committed):

1. **Test ROM**: compile all sources with `-g` (`*.iwram.c` with `build.py`'s `IWRAM_FLAGS`),
   with two test changes:
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
2. **Run**: `mGBA.exe -g test.gba`, then `arm-none-eabi-gdb -batch -x cmds.gdb test.elf`
   (`target remote :2345`). In `cmds.gdb`: `break test_checkpoint`; at each hit
   `dump binary memory` VRAM / palette / OAM / IO, print stats (lag, cycle counters, actor count),
   `set var` to warp the clock or give items, `continue`.
3. **Render**: a Python script turns the dumps into a PNG (mode-0 BG layers + sprites). BG
   scroll registers are write-only, so read scroll from `bg.c`'s `s_layers` instead.

Pitfalls:
- `set var` on initialized `.data` **before crt0 runs** gets overwritten — change test knobs at
  a checkpoint, not at the start.
- `EWRAM_BSS` pools are not zeroed at boot; the engine clears them itself — keep it that way.

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
  `projectile`, `health`. IWRAM is 32 KB shared with globals and stack (~27 KB used) — move
  code there only after measuring.
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
