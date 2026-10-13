"""Shared helpers for every tool: paths, the node-type registry and node loading.

The type registry below is one of the "four mirrors" (CLAUDE.md rule 8). If you add or
change a node type here, update CLAUDE.md §6, validate.py, codegen.py and the visualizer.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NODES_DIR = os.path.join(ROOT, "nodes")
CATALOG_DIR = os.path.join(ROOT, "catalog")
SRC_DIR = os.path.join(ROOT, "src")
GEN_DIR = os.path.join(ROOT, "generated")
BUILD_DIR = os.path.join(ROOT, "build")
DIST_DIR = os.path.join(ROOT, "dist")
VIS_DIR = os.path.join(ROOT, "visualizer")
CONFIG_PATH = os.path.join(ROOT, "tools", "config.json")

# --------------------------------------------------------------------------------------
# Node types (CLAUDE.md §6)
# --------------------------------------------------------------------------------------
SCENE_TYPES = ["intro", "title", "menu", "level", "inventory", "cutscene", "game_over", "credits"]

ACTOR_TYPES = ["player", "enemy", "npc", "prop", "platform", "pickup", "trigger"]
_ACTOR_OWNS = ["body", "particle", "sfx"]

OBJECT_TYPES = {
    # type:        (prefix,  may own)
    "palette":   ("pal_",  []),
    "sprite":    ("spr_",  []),
    "tileset":   ("ts_",   []),
    "tilemap":   ("map_",  ["tileset"]),
    "font":      ("font_", []),
    "icon":      ("icon_", []),
    "sfx":       ("sfx_",  []),
    "music":     ("mus_",  []),
    "animation": ("anim_", []),
    "particle":  ("ptc_",  ["animation"]),
    "body":      ("body_", ["sprite", "animation", "particle"]),
    "player":    ("plr_",  _ACTOR_OWNS),
    "enemy":     ("enm_",  _ACTOR_OWNS),
    "npc":       ("npc_",  _ACTOR_OWNS),
    "prop":      ("prop_", _ACTOR_OWNS),
    "platform":  ("plat_", _ACTOR_OWNS),
    "pickup":    ("item_", _ACTOR_OWNS),
    "trigger":   ("trg_",  []),
    "hud":       ("hud_",  ["icon"]),
    "menu":      ("menu_", ["icon"]),
    "dialog":    ("dlg_",  ["icon"]),
    "upgrade":   ("upg_",  ["icon"]),
    "species":   ("mon_",  ["body", "sprite", "sfx", "move", "palette"]),
    "move":      ("mov_",  ["particle", "animation", "sprite", "sfx"]),
}
SCENE_PREFIX = "scn_"

# Object types that a scene may place as an instance.
INSTANCE_TYPES = ["tilemap"] + ACTOR_TYPES + ["hud", "menu", "dialog", "particle"]

# Order matters: these lists become C enums in generated/node_ids.h.
# The *_up / *_down slots are for top-down actors: the engine picks them when the actor faces
# up or down (falling back to the plain slot); the plain slots face right and mirror for left.
ANIM_SLOTS = ["idle", "walk", "run", "jump", "fall", "hurt", "die", "attack", "climb",
              "idle_up", "idle_down", "walk_up", "walk_down", "attack_up", "attack_down"]
SOUND_EVENTS = ["jump", "land", "hurt", "die", "collect", "stomp", "talk", "attack"]
CATEGORIES = ["tiles", "player", "enemy", "npc", "prop", "platform", "pickup", "trigger"]
CHANNELS = ["square1", "square2", "wave", "noise"]
BUTTONS = {"A": 0x001, "B": 0x002, "SELECT": 0x004, "START": 0x008, "RIGHT": 0x010,
           "LEFT": 0x020, "UP": 0x040, "DOWN": 0x080, "R": 0x100, "L": 0x200}
TILE_FLAGS = ["solid", "one_way", "hazard", "ladder"]

# Monster battles (species / move nodes, game.battle). Order matters: these become C enums.
MOVE_SHAPES = ["single", "dash", "beam", "circle", "screen", "zone", "self"]
STATUSES = ["none", "par", "psn", "tox", "freeze"]
BUFFS = ["none", "fly", "phase"]
ZONE_PLACES = ["front", "screen", "around"]
ZONE_MAX_PIECES = 25     # effect pieces a zone lights at once (blocks of cell x cell tiles, x burst count): room for other effects
EFFECT_AT = ["target", "line", "user", "axis", "tiles", "center", "screen"]
# Where each shape's effect may appear. A move hurts what its effect draws, so the shape decides
# where the effect goes and the effect decides who is hit (screen: the whole view; self: no one).
MOVE_EFFECT_AT = {"single": ["target", "line"], "dash": ["target", "line"], "beam": ["axis"],
                  "circle": ["user"], "screen": ["screen", "user"], "zone": ["tiles", "center"],
                  "self": ["user"]}
BATTLE_TEXTS = ["damage", "miss", "super", "weak", "par", "psn", "tox"]
BATTLE_SOUNDS = ["hit", "super", "weak", "miss", "faint", "par"]
SPECIES_FIELDS = ["name", "type", "move1", "move2", "move3"]    # {var.field} placeholders
SPECIES_TITLE_W = 10     # chars of a species name
MOVE_TITLE_W = 13        # chars of a move name
SPECIES_SPEED = (10, 200)    # a species' walking speed, percent of the walker's own (default 100)


def move_reach(move):
    """Tiles a move reaches (like moves.c moves_reach): how close a wild monster walks before using it."""
    shape = move.get("shape")
    if shape == "dash":
        r, d = move.get("range", 0), move.get("dash", 0)
        return r + d if isinstance(r, int) and isinstance(d, int) else None
    if shape == "zone":
        place = (move.get("zone") or {}).get("place", "front")
        return 1 if place == "front" else move.get("range", 0) if place == "around" else 8
    if shape == "screen":
        return 8
    if shape == "self":
        return 0
    return move.get("range", 0)

# Valid OBJ sizes (w, h) -> (shape, size) as the hardware encodes them.
OBJ_SIZES = {
    (8, 8): (0, 0), (16, 16): (0, 1), (32, 32): (0, 2), (64, 64): (0, 3),
    (16, 8): (1, 0), (32, 8): (1, 1), (32, 16): (1, 2), (64, 32): (1, 3),
    (8, 16): (2, 0), (8, 32): (2, 1), (16, 32): (2, 2), (32, 64): (2, 3),
}

PIXEL_CHARS = ".123456789abcdef"
SCREEN_W, SCREEN_H = 240, 160

ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")
COLOR_RE = re.compile(r"^#[0-9a-fA-F]{6}$")

NOTE_NAMES = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7,
              "G#": 8, "A": 9, "A#": 10, "B": 11}
NOTE_RE = re.compile(r"^([A-G]#?)([0-9])$")
NOISE_RE = re.compile(r"^X([0-9a-fA-F])$")


def prefix_for(kind, type_):
    """Return the id prefix a node of this kind/type must use."""
    if kind == "scene":
        return SCENE_PREFIX
    return OBJECT_TYPES[type_][0]


def expected_path(kind, type_, node_id):
    """Return the path (relative to nodes/) where the node file must live."""
    if kind == "scene":
        return "scenes/%s.json" % node_id
    return "objects/%s/%s.json" % (type_, node_id)


def behavior_source(name):
    """Path of src/behaviors/<name>.c (or <name>.iwram.c for hot code in IWRAM), or None."""
    for fn in (name + ".c", name + ".iwram.c"):
        path = os.path.join(SRC_DIR, "behaviors", fn)
        if os.path.isfile(path):
            return path
    return None


def note_number(token):
    """MIDI-style note number for 'C#5' (C4 = 60), or None if not a note."""
    m = NOTE_RE.match(token)
    if not m:
        return None
    return NOTE_NAMES[m.group(1)] + 12 * (int(m.group(2)) + 1)


def note_ok(token):
    """True if the token is a note in the allowed range C2..B7."""
    n = note_number(token)
    return n is not None and 36 <= n <= 107


def pixel_index(ch):
    """Palette index of one pixel char ('.' = 0)."""
    return PIXEL_CHARS.index(ch)


def var_save_key(name):
    """16-bit key a variable is saved under (from its name: a save survives new variables)."""
    h = 2166136261
    for ch in name.encode("utf-8"):
        h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFFFF


def var_slot_keys(name, count):
    """Save key of each slot of a variable: its name's, or name[0], name[1]... for a list."""
    if count == 1:
        return [var_save_key(name)]
    return [var_save_key("%s[%d]" % (name, k)) for k in range(count)]


def color_to_bgr555(hex_color):
    """Convert '#RRGGBB' to a 15-bit GBA color (what the hardware shows)."""
    r = int(hex_color[1:3], 16) >> 3
    g = int(hex_color[3:5], 16) >> 3
    b = int(hex_color[5:7], 16) >> 3
    return r | (g << 5) | (b << 10)


def split_music_tokens(text):
    """Split a channel string into row tokens, dropping '|' bar lines."""
    return [t for t in text.split() if t != "|"]


HUD_VAR_WIDTH = 3  # characters reserved for a {var} value when checking that text fits
HUD_TIME_WIDTH = 5  # a {var:time} clock: 03:42

# UI text placeholders:
#   {var}        a game variable          {var:02}   with at least 2 digits (a clock: 05)
#   {var:time}   seconds as minutes:seconds (03:42); a negative value prints --:-- (no time yet)
#   {var.field}  a field (name, type, move1..3) of the species whose number is in var
#   {var|a|b|c}  one of the words: the first when var is 0, the second when 1... (last if more)
#   {list[i]}    any of the above on a list variable: the slot whose number is in variable i
PLACEHOLDER_RE = re.compile(
    r"\{(?P<var>\w+)(?:\[(?P<index>\w+)\])?(?:(?P<fmt>:02|:time)|\.(?P<field>\w+)|\|(?P<pick>[^{}]*))?\}")

# Widest text each species field prints (set from the project by set_species_widths).
SPECIES_FIELD_W = {f: (SPECIES_TITLE_W if f == "name" else MOVE_TITLE_W) for f in SPECIES_FIELDS}


def set_species_widths(project):
    """Remember how wide species names, types and move names really are (for text fitting)."""
    w = {f: 1 for f in SPECIES_FIELDS}
    for nid, n in project.nodes.items():
        if project.type_of(nid) != "species":
            continue
        w["name"] = max(w["name"], len(str(n.get("title", ""))))
        w["type"] = max(w["type"], len(str(n.get("monster_type", ""))))
        for i, m in enumerate(n.get("moves") or []):
            if i < 3:
                w["move%d" % (i + 1)] = max(w["move%d" % (i + 1)], len(str(project.nodes.get(m, {}).get("title", ""))))
    SPECIES_FIELD_W.update(w)


def placeholder_value_width(m):
    """Columns one placeholder match takes at most."""
    if m.group("field"):
        return SPECIES_FIELD_W.get(m.group("field"), MOVE_TITLE_W)
    if m.group("pick") is not None:
        return max(len(o) for o in m.group("pick").split("|"))
    if m.group("fmt") == ":time":
        return HUD_TIME_WIDTH
    return HUD_VAR_WIDTH


def placeholder_width(text):
    """Columns a text with placeholders takes at most (HUD_VAR_WIDTH reserved per number)."""
    return len(PLACEHOLDER_RE.sub(lambda m: "x" * placeholder_value_width(m), text))


# Upgrade menus (CLAUDE.md §6.1b): each card = 16x16 icon left of the text, the title with a
# NEW!/LV2/EVOLVE! tag, then up to 2 lines of description.
UPGRADE_CATEGORIES = ["weapon", "item", "evolution", "bonus"]
UPGRADE_TITLE_W = 14     # chars
UPGRADE_TAG_DX = 15      # tag column, from the card's text x
UPGRADE_TEXT_W = 22      # description width (chars); the tag "EVOLVE!" also ends here
UPGRADE_TEXT_LINES = 2
UPGRADE_ICON_DX = 3      # icon columns left of the text (2 wide + 1 gap); cursor left of that
MAX_UPGRADE_CHOICES = 3


def wrap_text(text, width):
    """Word-wrap text to lines of at most `width` chars. Returns (lines, error_or_None)."""
    lines = []
    for para in text.split("\n"):
        line = ""
        for word in para.split(" "):
            if len(word) > width:
                return lines, "the word '%s' is longer than the box is wide (%d chars)" % (word, width)
            if not line:
                line = word
            elif len(line) + 1 + len(word) <= width:
                line += " " + word
            else:
                lines.append(line)
                line = word
        lines.append(line)
    return lines, None


def box_tiles(box):
    """Convert a {x,y,w,h} pixel box into tile units (x, y, w, h)."""
    return box["x"] // 8, box["y"] // 8, box["w"] // 8, box["h"] // 8


def dialog_layout(box, line):
    """Where a dialog line's text goes, in tiles: dict(text_x, text_y, text_w, text_h, ...).

    Box border takes 1 tile on each side. The speaker name uses the first inner row.
    A portrait (16x16) uses 2 columns + 1 gap on the left of the text.
    """
    bx, by, bw, bh = box_tiles(box)
    ix, iy, iw, ih = bx + 1, by + 1, bw - 2, bh - 2
    has_speaker = bool(line.get("speaker"))
    has_portrait = bool(line.get("portrait"))
    ty = iy + (1 if has_speaker else 0)
    tx = ix + (3 if has_portrait else 0)
    return {"inner_x": ix, "inner_y": iy, "inner_w": iw, "inner_h": ih,
            "speaker_x": ix, "speaker_y": iy, "portrait_x": ix, "portrait_y": ty,
            "text_x": tx, "text_y": ty, "text_w": ix + iw - tx, "text_h": iy + ih - ty}


def menu_layout(menu):
    """Tile positions of a menu: title (x, y) and each option's (x, y). Cursor sits left of x."""
    lay = menu.get("layout", {})
    x, y = lay.get("x", 64) // 8, lay.get("y", 64) // 8
    spacing = max(1, lay.get("spacing", 16) // 8)
    title = menu.get("title") or ""
    title_y = lay.get("title_y", lay.get("y", 64) - 24) // 8
    box = menu.get("box")
    if box:
        bx, _by, bw, _bh = box_tiles(box)
        title_x = bx + (bw - placeholder_width(title)) // 2
    else:
        title_x = (30 - placeholder_width(title)) // 2
    count = menu.get("upgrades") if menu.get("upgrades") else len(menu.get("options", []))
    opts = [(x, y + i * spacing) for i in range(count)]
    return {"title_x": title_x, "title_y": title_y, "options": opts}


def setup_console():
    """Make print() safe for ✔/✖ and accented paths on Windows consoles."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


def read_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_config():
    """tools/config.json as a dict (empty dict if missing)."""
    try:
        return read_json(CONFIG_PATH)
    except (OSError, ValueError):
        return {}


def read_engine_limits():
    """Read the #define limits from src/engine/config.h (single source of truth)."""
    limits = {}
    path = os.path.join(SRC_DIR, "engine", "config.h")
    try:
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                m = re.match(r"\s*#define\s+(MAX_\w+|BSTATE_\w+|MOVE_\w+|STREAM_\w+)\s+(\d+)", line)
                if m:
                    limits[m.group(1)] = int(m.group(2))
    except OSError:
        pass
    return limits


def sprite_tiles(n):
    """OBJ tiles a sprite sheet takes when kept whole in VRAM (every frame)."""
    w, h, frames = n.get("width"), n.get("height"), n.get("frames") or {}
    if not isinstance(w, int) or not isinstance(h, int):
        return 0
    return (w // 8) * (h // 8) * len(frames)


def sprite_streams(n, limits=None):
    """Does this sheet stream (too many tiles to keep whole: only the frames on screen sit in VRAM)?"""
    limits = limits if limits is not None else read_engine_limits()
    return sprite_tiles(n) > limits.get("STREAM_MIN_TILES", 1 << 30)


def stream_slot_tiles(sprites, limits=None):
    """OBJ tiles of one streaming slot: the largest frame of a streamed sheet (0 = none streams)."""
    limits = limits if limits is not None else read_engine_limits()
    return max([(n["width"] // 8) * (n["height"] // 8) for n in sprites if sprite_streams(n, limits)] or [0])


# ---- what a move's effect draws (codegen bakes it in, the visualizer gets it from bundle.py) ----
def anim_frame_at(anim, steps):
    """Index of the frame an animation shows after `steps` calls of anim_step (src/engine/anim.iwram.c)."""
    ticks = [max(1, int(f.get("ticks", 1))) for f in anim["frames"]]
    total = sum(ticks)
    if steps >= total:
        if not anim.get("loop", True):
            return len(ticks) - 1               # a finished animation keeps its last frame
        steps %= total
    for i, t in enumerate(ticks):
        if steps < t:
            return i
        steps -= t
    return len(ticks) - 1


def zone_hurt_steps(move):
    """The particle steps (frames since the zone landed, + 1) during which a zone can hurt:
    from the end of its delay to the end of its longest life (moves_tick runs before or after
    the move in the frame it is used, so one step of slack on each side)."""
    z = move.get("zone") or {}
    delay = int(z.get("delay", 0))
    ticks = z.get("ticks") or [1, 1]
    return delay + 1, delay + int(ticks[-1]) + 1


def effect_box(p, move):
    """What a move's effect draws around each of its particles: [left, top, right, bottom] pixel
    offsets from the particle's position (inclusive), or None when it draws nothing. It is the
    union of the opaque pixels of every frame shown while the particle is drawn (steps 1 to
    lifetime - 1, flips included); for a zone whose effect plays at its center, only the frames
    shown while the zone can hurt (a falling rock hurts where it lands, not up in the air).
    moves.c hurts every foe whose hitbox comes within MOVE_HIT_MARGIN px of it."""
    eff = p.nodes.get(move.get("effect") or "") or {}
    anim = p.nodes.get(eff.get("animation") or "") or {}
    spr = p.nodes.get(anim.get("sprite") or "") or {}
    frames = anim.get("frames") or []
    if not frames or not isinstance(spr.get("frames"), dict):
        return None
    life = int(eff.get("lifetime", 1))
    lo, hi = 1, life - 1
    if move.get("shape") == "zone" and move.get("effect_at") == "center":
        zlo, zhi = zone_hurt_steps(move)
        lo, hi = max(lo, zlo), min(hi, zhi)
    w, h = int(spr.get("width", 0)), int(spr.get("height", 0))
    l = t = 1 << 20
    r = b = -(1 << 20)
    for i in sorted({anim_frame_at(anim, s) for s in range(lo, hi + 1)}):
        f = frames[i]
        for y, row in enumerate(spr["frames"].get(f.get("frame"), [])):
            for x, ch in enumerate(row):
                if ch == ".":
                    continue
                px = w - 1 - x if f.get("flip_x") else x
                py = h - 1 - y if f.get("flip_y") else y
                l, r, t, b = min(l, px), max(r, px), min(t, py), max(b, py)
    if r < l:
        return None
    return [l - w // 2, t - h // 2, r - w // 2, b - h // 2]


# ---- a move's effect in 3 steps (moves.c strike_show; only a picture: who is hurt and when stays) ----
# Percent of the effect's time (its particle's lifetime) for step 1 (coming out of the user) and
# step 2 (on its way to where the effect plays); step 3 (the effect there) is the other 60 %.
MOVE_STEPS = (20, 20)


def move_steps(p, move):
    """[step 1, step 2] in frames for a move's effect, or None when it plays as it is: whole-screen
    moves, zones placed on the screen (nothing comes from the user) or around the user (it stands in
    the middle), zones whose effect plays at their center (timed to when they land), and moves
    without an effect."""
    shape = move.get("shape")
    z = move.get("zone") if isinstance(move.get("zone"), dict) else {}
    if shape == "screen" or (shape == "zone" and (z.get("place") in ("screen", "around") or move.get("effect_at") == "center")):
        return None
    life = (p.nodes.get(move.get("effect") or "") or {}).get("lifetime")
    if not isinstance(life, int) or isinstance(life, bool) or life < 2:
        return None
    t1 = (life * MOVE_STEPS[0] + 50) // 100
    t12 = (life * (MOVE_STEPS[0] + MOVE_STEPS[1]) + 50) // 100
    return [t1, t12 - t1]


def sped_up_frames(anim, steps, frames):
    """Frame indices an animation shows when its `steps` steps play over `frames` frames, spread
    evenly like particles.c does for a move's effect in step 3."""
    acc = done = 0
    shown = set()
    for _ in range(frames):
        acc += steps
        done += acc // frames
        acc %= frames
        shown.add(anim_frame_at(anim, done))
    return shown


def frames_lost_in_step3(p, move):
    """Frames of a move's effect animation that step 3 plays too fast to show (they would hurt
    without ever being drawn). Empty when the move plays as it is or its effect plays at the user
    (those keep their animation's pace)."""
    steps = move_steps(p, move)
    if not steps or move.get("effect_at") == "user":
        return []
    eff = p.nodes.get(move.get("effect") or "") or {}
    anim = p.nodes.get(eff.get("animation") or "") or {}
    if not anim.get("frames"):
        return []
    life = int(eff["lifetime"])
    normal = {anim_frame_at(anim, k) for k in range(1, life)}
    late = sped_up_frames(anim, life - 1, max(1, life - 1 - sum(steps)))     # the latest piece: at the destination
    return sorted(normal - late)


class Project:
    """Everything loaded from nodes/ and catalog/. Load problems are kept in load_errors."""

    def __init__(self):
        self.index_doc = {}
        self.game = {}
        self.index = []          # list of index entries (dicts)
        self.by_id = {}          # id -> index entry
        self.nodes = {}          # id -> parsed node json
        self.behaviors = {}
        self.actions = {}
        self.load_errors = []    # (where, message)
        self.disk_files = []     # every .json under nodes/scenes and nodes/objects (relative)

    # ---- convenience lookups ----
    def kind_of(self, node_id):
        e = self.by_id.get(node_id)
        return e.get("kind") if e else None

    def type_of(self, node_id):
        e = self.by_id.get(node_id)
        return e.get("type") if e else None

    def is_scene(self, node_id):
        return self.kind_of(node_id) == "scene"

    def var_names(self):
        return [v.get("name") for v in self.game.get("variables", []) if isinstance(v, dict)]

    def var_count(self, name):
        """Slots of a variable: its 'count' for a list (one value per monster...), else 1."""
        for v in self.game.get("variables", []):
            if isinstance(v, dict) and v.get("name") == name:
                return v.get("count", 1) if isinstance(v.get("count", 1), int) else 1
        return 1

    def var_slots(self):
        """[(name, first slot, count)] in order: a list variable takes count slots in a row."""
        out, at = [], 0
        for v in self.game.get("variables", []):
            if isinstance(v, dict):
                n = self.var_count(v.get("name"))
                out.append((v.get("name"), at, n))
                at += n
        return out


def load_project():
    """Load nodes.json, every node file it lists, and both catalogs."""
    p = Project()
    idx_path = os.path.join(NODES_DIR, "nodes.json")
    try:
        p.index_doc = read_json(idx_path)
    except OSError:
        p.load_errors.append(("nodes/nodes.json", "file is missing"))
        return p
    except ValueError as e:
        p.load_errors.append(("nodes/nodes.json", "is not valid JSON: %s" % e))
        return p
    p.game = p.index_doc.get("game", {}) if isinstance(p.index_doc.get("game"), dict) else {}
    raw = p.index_doc.get("nodes", [])
    p.index = [e for e in raw if isinstance(e, dict)] if isinstance(raw, list) else []
    for e in p.index:
        nid = e.get("id")
        if isinstance(nid, str) and nid not in p.by_id:
            p.by_id[nid] = e
    for e in p.index:
        nid, rel = e.get("id"), e.get("path")
        if not isinstance(nid, str) or not isinstance(rel, str) or nid in p.nodes:
            continue
        full = os.path.join(NODES_DIR, rel.replace("/", os.sep))
        try:
            data = read_json(full)
            if isinstance(data, dict):
                p.nodes[nid] = data
            else:
                p.load_errors.append(("nodes/" + rel, "must contain a JSON object"))
        except OSError:
            p.load_errors.append(("nodes/" + rel, "file listed in nodes.json is missing"))
        except ValueError as ex:
            p.load_errors.append(("nodes/" + rel, "is not valid JSON: %s" % ex))
    for sub in ("scenes", "objects"):
        base = os.path.join(NODES_DIR, sub)
        for dirpath, _dirs, files in os.walk(base):
            for fn in files:
                rel = os.path.relpath(os.path.join(dirpath, fn), NODES_DIR).replace(os.sep, "/")
                p.disk_files.append(rel)
    for name, attr in (("behaviors.json", "behaviors"), ("actions.json", "actions")):
        path = os.path.join(CATALOG_DIR, name)
        try:
            doc = read_json(path)
            setattr(p, attr, {k: v for k, v in doc.items() if not k.startswith("_")})
        except OSError:
            p.load_errors.append(("catalog/" + name, "file is missing"))
        except ValueError as ex:
            p.load_errors.append(("catalog/" + name, "is not valid JSON: %s" % ex))
    set_species_widths(p)
    return p
