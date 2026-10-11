"""Check every node against the rules in CLAUDE.md. Stops the build on any error.

Usage: python tools/validate.py      (exit code 0 = ok, 1 = errors)

This file is one of the "four mirrors" (CLAUDE.md rule 8): each node type has a
check function here (V_<type>) that must match CLAUDE.md §6.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402


class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def err(self, where, msg):
        self.errors.append("%s: %s" % (where, msg))

    def warn(self, where, msg):
        self.warnings.append("%s: %s" % (where, msg))

    @property
    def ok(self):
        return not self.errors


COMMON_FIELDS = {"id", "kind", "type", "name", "notes", "tags", "children", "code"}
ACTOR_FIELDS = {"body", "logic", "sounds"}
TYPE_FIELDS = {
    "scene": {"backdrop", "music", "camera", "instances", "on_start"},
    "palette": {"colors"},
    "sprite": {"palette", "width", "height", "frames"},
    "tileset": {"palette", "tiles"},
    "tilemap": {"tileset", "layer", "rows", "parallax", "repeat_x", "over"},
    "font": {"palette", "glyphs"},
    "icon": {"palette", "pixels"},
    "sfx": {"channel", "duty", "steps", "priority"},
    "music": {"bpm", "rows_per_beat", "loop", "loop_from", "instruments", "patterns", "order"},
    "animation": {"sprite", "loop", "frames", "events"},
    "particle": {"animation", "mode", "count", "rate", "lifetime", "speed", "angle", "gravity", "on_top"},
    "body": {"origin", "hitbox", "physics", "animations", "default_animation"},
    "trigger": {"zone", "logic"},
    "hud": {"font", "elements"},
    "menu": {"font", "title", "options", "upgrades", "cursor", "layout", "on_cancel", "box", "sounds", "texts"},
    "dialog": {"font", "box", "lines", "choices", "on_end", "ticks_per_char"},
    "upgrade": {"category", "title", "icon", "var", "max_level", "descriptions", "requires", "replaces", "on_pick"},
    "species": {"title", "monster_type", "body", "alpha_palette", "portrait", "cry", "moves"},
    "move": {"title", "monster_type", "shape", "range", "power", "accuracy", "charge", "status", "dash", "zone", "buff",
             "effect", "effect_at", "sfx", "on_use"},
}
for _t in C.ACTOR_TYPES:
    if _t != "trigger":
        TYPE_FIELDS[_t] = ACTOR_FIELDS


def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


class Ctx:
    """Context for checking one node: where errors go and how to resolve references."""

    def __init__(self, project, report, node_id, node, where):
        self.p, self.r, self.id, self.n, self.where = project, report, node_id, node, where

    def err(self, msg):
        self.r.err(self.where, msg)

    def warn(self, msg):
        self.r.warn(self.where, msg)

    # ---- field helpers: return the value (or a default) and report problems ----
    def get(self, obj, key, kind, label=None, required=True, default=None, lo=None, hi=None):
        label = label or key
        if not isinstance(obj, dict) or key not in obj:
            if required:
                self.err("'%s' is required" % label)
            return default
        v = obj[key]
        okay = {"int": is_int(v), "num": is_num(v), "bool": isinstance(v, bool),
                "str": isinstance(v, str), "list": isinstance(v, list),
                "dict": isinstance(v, dict)}[kind]
        if not okay:
            self.err("'%s' must be %s" % (label, {"int": "a whole number", "num": "a number",
                                                   "bool": "true or false", "str": "text",
                                                   "list": "a list", "dict": "an object"}[kind]))
            return default
        if kind in ("int", "num"):
            if lo is not None and v < lo or hi is not None and v > hi:
                self.err("'%s' is %s but must be between %s and %s" % (label, v, lo, hi))
                return default
        return v

    def ref(self, value, types, label, allow_empty=False):
        """Check a reference to another node. types: list of object types, or 'scene'."""
        if value is None or value == "":
            if not allow_empty:
                self.err("'%s' must name a node" % label)
            return None
        if not isinstance(value, str):
            self.err("'%s' must be a node id" % label)
            return None
        if value not in self.p.by_id:
            self.err("'%s' points to '%s', which does not exist" % (label, value))
            return None
        kind, t = self.p.kind_of(value), self.p.type_of(value)
        if types == "scene":
            if kind != "scene":
                self.err("'%s' must be a scene, but '%s' is a %s" % (label, value, t))
                return None
        elif kind != "object" or t not in types:
            self.err("'%s' must be a %s, but '%s' is a %s" % (label, " or ".join(types), value, t))
            return None
        return value

    def color(self, v, label):
        if not isinstance(v, str) or not C.COLOR_RE.match(v):
            self.err("'%s' must be a color like \"#78c8f8\"" % label)

    def palette_size(self, pal_id):
        pal = self.p.nodes.get(pal_id) if pal_id else None
        if pal and isinstance(pal.get("colors"), list):
            return len(pal["colors"])
        return 16

    def pixels(self, rows, w, h, pal_id, label):
        """Check a pixel grid: h rows of w chars, valid palette indexes."""
        if not isinstance(rows, list) or not all(isinstance(r, str) for r in rows):
            self.err("'%s' must be a list of text rows" % label)
            return
        if h is not None and len(rows) != h:
            self.err("'%s' has %d rows but must have %d" % (label, len(rows), h))
        if w is not None:
            for i, r in enumerate(rows):
                if len(r) != w:
                    self.err("'%s' row %d is %d pixels wide but must be %d" % (label, i, len(r), w))
                    break
        ncolors = self.palette_size(pal_id)
        for i, r in enumerate(rows):
            for ch in r:
                if ch not in C.PIXEL_CHARS:
                    self.err("'%s' row %d uses '%s'; use '.', 1-9 or a-f" % (label, i, ch))
                    return
                if C.pixel_index(ch) >= ncolors:
                    self.err("'%s' uses color %s but palette '%s' only has %d colors"
                             % (label, ch, pal_id, ncolors))
                    return

    def box(self, box, label):
        """Check a UI box {x, y, w, h, paper, border} in pixels, on the 8px grid."""
        if not isinstance(box, dict):
            self.err("'%s' must be an object {x, y, w, h}" % label)
            return False
        good = True
        for k, hi in (("x", C.SCREEN_W - 16), ("y", C.SCREEN_H - 16), ("w", C.SCREEN_W), ("h", C.SCREEN_H)):
            v = self.get(box, k, "int", "%s.%s" % (label, k), lo=0 if k in "xy" else 24, hi=hi)
            if v is None:
                good = False
            elif v % 8:
                self.err("'%s.%s' must be a multiple of 8 (text sits on the 8x8 tile grid)" % (label, k))
                good = False
        if good and (box["x"] + box["w"] > C.SCREEN_W or box["y"] + box["h"] > C.SCREEN_H):
            self.err("'%s' goes off the 240x160 screen" % label)
            good = False
        self.get(box, "paper", "int", label + ".paper", required=False, lo=0, hi=15)
        self.get(box, "border", "int", label + ".border", required=False, lo=0, hi=15)
        return good

    def text_in_font(self, text, font_id, label):
        font = self.p.nodes.get(font_id) if font_id else None
        if not font or not isinstance(font.get("glyphs"), dict):
            return
        glyphs = font["glyphs"]
        for ch in set(text):
            if ch in " \n":
                continue
            if ch not in glyphs and ch.upper() not in glyphs:
                self.err("'%s' uses '%s' but font '%s' has no glyph for it" % (label, ch, font_id))

    # ---- logic: actions and behaviors (checked against the catalogs) ----
    def param_value(self, ptype, pdef, v, label):
        if ptype in ("int", "ticks"):
            if isinstance(v, bool) and ptype == "int":
                return
            if not is_int(v):
                self.err("'%s' must be a whole number" % label)
            elif ptype == "ticks" and not 0 <= v <= 65535:
                self.err("'%s' must be between 0 and 65535 ticks" % label)
        elif ptype == "fixed":
            if not is_num(v):
                self.err("'%s' must be a number" % label)
        elif ptype == "bool":
            if not isinstance(v, bool):
                self.err("'%s' must be true or false" % label)
        elif ptype == "string":
            if not isinstance(v, str) or not re.match(r"^[A-Za-z_]\w*$", v):
                self.err("'%s' must be a C function name" % label)
        elif ptype == "text":
            if not isinstance(v, str) or not v or not all(32 <= ord(ch) < 127 for ch in v):
                self.err("'%s' must be plain text (letters, digits, punctuation)" % label)
        elif ptype == "button":
            if v not in C.BUTTONS:
                self.err("'%s' must be one of %s" % (label, ", ".join(C.BUTTONS)))
        elif ptype == "var":
            if v == "" and not pdef.get("required"):
                return
            if v not in self.p.var_names():
                self.err("'%s' names variable '%s', which is not in nodes.json game.variables" % (label, v))
        elif ptype == "choice":
            if v not in pdef.get("options", []):
                self.err("'%s' must be one of %s" % (label, ", ".join(pdef.get("options", []))))
        elif ptype == "actions":
            self.actions(v, label)
        elif ptype == "points":
            if not isinstance(v, list) or not v or not all(
                    isinstance(pt, list) and len(pt) == 2 and all(is_int(c) for c in pt) for pt in v):
                self.err("'%s' must be a list of [x, y] points" % label)
            elif len(v) > 32:
                self.err("'%s' has more than 32 points" % label)
        elif ptype.startswith("node:"):
            t = ptype[5:]
            allow_empty = not pdef.get("required")
            if t == "scene":
                self.ref(v, "scene", label, allow_empty)
            elif t == "spawnable":
                self.ref(v, C.ACTOR_TYPES + ["particle"], label, allow_empty)
            elif t == "actor":
                self.ref(v, C.ACTOR_TYPES, label, allow_empty)
            else:
                self.ref(v, [t], label, allow_empty)
        else:
            self.err("'%s' has unknown param type '%s' in the catalog" % (label, ptype))

    def actions(self, lst, label):
        if not isinstance(lst, list):
            self.err("'%s' must be a list of actions" % label)
            return
        for i, a in enumerate(lst):
            where = "%s[%d]" % (label, i)
            if not isinstance(a, dict) or not isinstance(a.get("do"), str):
                self.err("'%s' must be an action like {\"do\": \"goto_scene\", ...}" % where)
                continue
            spec = self.p.actions.get(a["do"])
            if spec is None:
                self.err("'%s' uses unknown action '%s' (see catalog/actions.json)" % (where, a["do"]))
                continue
            params = spec.get("params", {})
            for k in a:
                if k != "do" and k not in params:
                    self.err("'%s' (%s) has unknown param '%s'" % (where, a["do"], k))
            for pname, pdef in params.items():
                if pname not in a:
                    if pdef.get("required"):
                        self.err("'%s' (%s) needs '%s'" % (where, a["do"], pname))
                    continue
                self.param_value(pdef["type"], pdef, a[pname], "%s.%s" % (where, pname))
            if a["do"] in ("set_var", "add_var") and "value" in a and a.get("from"):
                self.err("'%s' (%s) has both 'value' and 'from'; use one" % (where, a["do"]))
            if a["do"] in ("set_var", "add_var") and "value" not in a and not a.get("from"):
                self.err("'%s' (%s) needs 'value' (or 'from')" % (where, a["do"]))
            if a["do"] == "if_chance" and is_int(a.get("percent")) and not 0 <= a["percent"] <= 100:
                self.err("'%s.percent' must be between 0 and 100" % where)
            if a["do"] == "float_text" and not isinstance(self.p.game.get("battle"), dict):
                self.err("'%s' uses float_text, which needs game.battle (its font) in nodes.json" % where)
            if a["do"] == "float_text" and isinstance(a.get("text"), str):
                font = (self.p.game.get("battle") or {}).get("font")
                self.text_in_font(a["text"], font, where + ".text")
                if len(a["text"]) > 15:
                    self.err("'%s.text' is longer than 15 characters" % where)
            if a["do"] in ("save_game", "load_game") and not self.p.game.get("save"):
                self.err("'%s' uses %s but game.save is false in nodes.json" % (where, a["do"]))
            if a["do"] == "call" and isinstance(a.get("function"), str):
                if a["function"] not in find_game_functions():
                    self.err("'%s' calls '%s' but no src/game/*.c defines void %s(Actor *self)"
                             % (where, a["function"], a["function"]))

    def logic(self, lst, label="logic"):
        if not isinstance(lst, list):
            self.err("'%s' must be a list" % label)
            return
        limit = C.read_engine_limits().get("MAX_LOGIC", 6)
        if len(lst) > limit:
            self.err("'%s' has %d behaviors; the engine allows %d (MAX_LOGIC)" % (label, len(lst), limit))
        for i, entry in enumerate(lst):
            where = "%s[%d]" % (label, i)
            if not isinstance(entry, dict) or not isinstance(entry.get("behavior"), str):
                self.err("'%s' must be {\"behavior\": name, \"params\": {...}}" % where)
                continue
            for k in entry:
                if k not in ("behavior", "params"):
                    self.err("'%s' has unknown field '%s'" % (where, k))
            b = entry["behavior"]
            spec = self.p.behaviors.get(b)
            if spec is None:
                self.err("'%s' uses unknown behavior '%s' (see catalog/behaviors.json)" % (where, b))
                continue
            self.behavior_params(b, entry.get("params", {}), "%s.%s" % (where, b), check_required=True)

    def behavior_params(self, b, params, label, check_required):
        spec = self.p.behaviors[b].get("params", {})
        if not isinstance(params, dict):
            self.err("'%s' params must be an object" % label)
            return
        for k, v in params.items():
            if k not in spec:
                self.err("'%s' has unknown param '%s'" % (label, k))
                continue
            self.param_value(spec[k]["type"], spec[k], v, "%s.%s" % (label, k))
        if check_required:
            for k, pdef in spec.items():
                if pdef.get("required") and k not in params:
                    self.err("'%s' needs param '%s'" % (label, k))


_game_functions = None


def find_game_functions():
    """Names of every `void name(` function defined in src/game/*.c."""
    global _game_functions
    if _game_functions is None:
        _game_functions = set()
        gdir = os.path.join(C.SRC_DIR, "game")
        if os.path.isdir(gdir):
            for fn in os.listdir(gdir):
                if fn.endswith(".c"):
                    with open(os.path.join(gdir, fn), "r", encoding="utf-8") as f:
                        _game_functions.update(re.findall(r"^\s*void\s+(\w+)\s*\(", f.read(), re.M))
    return _game_functions


# --------------------------------------------------------------------------------------
# One check function per node type
# --------------------------------------------------------------------------------------
def V_scene(c):
    n = c.n
    if "backdrop" in n:
        c.color(n["backdrop"], "backdrop")
    if n.get("music") is not None:
        c.ref(n["music"], ["music"], "music")
    insts = c.get(n, "instances", "list", default=[])
    ids, layers = {}, {}
    huds = boxes = actors = 0
    for i, inst in enumerate(insts):
        where = "instances[%d]" % i
        if not isinstance(inst, dict):
            c.err("'%s' must be an object" % where)
            continue
        for k in inst:
            if k not in ("id", "object", "x", "y", "overrides"):
                c.err("'%s' has unknown field '%s'" % (where, k))
        iid = c.get(inst, "id", "str", where + ".id")
        if iid is not None:
            if not C.ID_RE.match(iid):
                c.err("'%s.id' must be snake_case" % where)
            if iid in ids:
                c.err("instance id '%s' is used twice" % iid)
            ids[iid] = inst
            where = "instance '%s'" % iid
        obj = c.ref(inst.get("object"), C.INSTANCE_TYPES, where + ".object")
        if obj is None:
            continue
        t = c.p.type_of(obj)
        node = c.p.nodes.get(obj, {})
        c.get(inst, "x", "int", where + ".x", required=False, lo=-4096, hi=16384)
        c.get(inst, "y", "int", where + ".y", required=False, lo=-4096, hi=16384)
        if t in C.ACTOR_TYPES:
            actors += 1
        elif t == "tilemap":
            layer = node.get("layer")
            if layer in layers:
                c.err("'%s' and '%s' both use background layer %s" % (layers[layer], iid, layer))
            layers[layer] = iid
        elif t == "hud":
            huds += 1
        elif t in ("menu", "dialog"):
            boxes += 1
        ov = inst.get("overrides")
        if ov is not None:
            if t not in C.ACTOR_TYPES:
                c.err("'%s' has overrides, but only actors (with logic) can have them" % where)
            elif not isinstance(ov, dict):
                c.err("'%s.overrides' must be an object keyed by behavior name" % where)
            else:
                used = [e.get("behavior") for e in node.get("logic", []) if isinstance(e, dict)]
                for b, params in ov.items():
                    if b not in used:
                        c.err("'%s' overrides behavior '%s', but '%s' does not use it" % (where, b, obj))
                    elif used.count(b) > 1:
                        c.err("'%s' overrides behavior '%s', but '%s' lists it %d times (which one?)"
                              % (where, b, obj, used.count(b)))
                    elif b in c.p.behaviors:
                        c.behavior_params(b, params, "%s.overrides.%s" % (where, b), False)
    if huds > 1:
        c.err("a scene can show only one HUD")
    if boxes > 1:
        c.err("a scene can open only one menu or dialog at start")
    limit = C.read_engine_limits().get("MAX_ACTORS", 48)
    if actors > limit:
        c.err("has %d actors but the engine allows %d (MAX_ACTORS)" % (actors, limit))
    cam = n.get("camera")
    if cam is not None:
        if not isinstance(cam, dict):
            c.err("'camera' must be an object")
        else:
            for k in cam:
                if k not in ("follow", "bounds", "x", "y"):
                    c.err("'camera' has unknown field '%s'" % k)
            f = cam.get("follow")
            if f is not None:
                if f not in ids:
                    c.err("'camera.follow' names instance '%s', which is not in this scene" % f)
                elif c.p.type_of(ids[f].get("object")) not in C.ACTOR_TYPES:
                    c.err("'camera.follow' must name an actor instance")
            if cam.get("bounds", "map") not in ("map", "none"):
                c.err("'camera.bounds' must be \"map\" or \"none\"")
            c.get(cam, "x", "int", "camera.x", required=False)
            c.get(cam, "y", "int", "camera.y", required=False)
    if "on_start" in n:
        c.actions(n["on_start"], "on_start")
    scene_budget(c, insts)


def scene_budget(c, insts):
    """GBA limits for one scene: OBJ VRAM, palettes, BG tiles."""
    p = c.p
    sprites, obj_pals, bg_pals, icons = set(), set(), set(), set()

    def add_anim(a):
        an = p.nodes.get(a, {})
        s = an.get("sprite")
        if s in p.nodes:
            sprites.add(s)
            obj_pals.add(p.nodes[s].get("palette"))

    def add_particle(pt):
        add_anim(p.nodes.get(pt, {}).get("animation"))

    def add_body(b):
        for a in (p.nodes.get(b, {}).get("animations") or {}).values():
            add_anim(a)

    def add_species(s, alpha=False):
        sp = p.nodes.get(s, {})
        add_body(sp.get("body"))
        if alpha:
            obj_pals.add("(alpha colors)")        # one Alpha (of any species) at a time
        for mv in sp.get("moves") or []:
            add_particle(p.nodes.get(mv, {}).get("effect"))

    def add_object(o, inst=None):
        node = p.nodes.get(o, {})
        t = p.type_of(o)
        if t == "particle":
            add_particle(o)
        add_body(node.get("body"))
        for e in node.get("logic", []) or []:
            if not isinstance(e, dict):
                continue
            prm = dict(e.get("params") or {})
            prm.update(((inst or {}).get("overrides") or {}).get(e.get("behavior"), {}))
            for v in prm.values():                      # sprites and particles a behavior shows
                if isinstance(v, str) and p.type_of(v) == "sprite":
                    sprites.add(v)
                    obj_pals.add(p.nodes[v].get("palette"))
                elif isinstance(v, str) and p.type_of(v) == "particle":
                    add_particle(v)
                elif isinstance(v, str) and p.type_of(v) == "palette" and e.get("behavior") != "monster":
                    obj_pals.add(v)
            if e.get("behavior") == "monster":
                bt = p.game.get("battle") if isinstance(p.game.get("battle"), dict) else {}
                obj_pals.update([bt.get("flash_palette"), bt.get("par_palette")])
                obj_pals.update(t.get("palette") for t in (bt.get("texts") or {}).values() if isinstance(t, dict))
                if p.type_of(prm.get("species")) == "species":
                    add_species(prm["species"], prm.get("alpha"))
                elif prm.get("species_var") or prm.get("random"):
                    for s in species_nodes(p):
                        add_species(s, prm.get("alpha"))
        if t == "tilemap":
            ts = p.nodes.get(node.get("tileset"), {})
            bg_pals.add(ts.get("palette"))
            ntiles = len(ts.get("tiles", {}) or {})
            if ntiles > (254 if node.get("layer") == 3 else 510):
                c.err("tileset '%s' has %d tiles, too many for layer %s" % (node.get("tileset"), ntiles, node.get("layer")))
        if t in ("hud", "menu", "dialog"):
            bg_pals.add(p.nodes.get(node.get("font"), {}).get("palette"))
            refs = set(re.findall(r'"(icon_\w+)"', str(node).replace("'", '"')))
            if t == "menu" and node.get("upgrades"):
                refs |= {u.get("icon") for u in upgrade_nodes(p).values() if u.get("icon")}
            for ref in refs:
                bg_pals.add(p.nodes.get(ref, {}).get("palette"))
                icons.add(ref)

    for inst in insts:
        if isinstance(inst, dict) and inst.get("object") in p.nodes:
            add_object(inst["object"], inst)
    for a in re.findall(r"'do': 'spawn', [^}]*'object': '(\w+)'", str(c.n.get("on_start", []))):
        add_object(a)                                   # things the scene spawns when it starts
    tiles = 0
    for s in sprites:
        sp = p.nodes[s]
        if isinstance(sp.get("width"), int) and isinstance(sp.get("height"), int):
            tiles += (sp["width"] // 8) * (sp["height"] // 8) * len(sp.get("frames", {}) or {})
    if tiles > 1024:
        c.err("sprites in this scene need %d OBJ tiles; the GBA has 1024" % tiles)
    obj_pals.discard(None)
    bg_pals.discard(None)
    if len(obj_pals) > 16:
        c.err("sprites use %d palettes; the GBA has 16 sprite palettes" % len(obj_pals))
    if len(bg_pals) > 16:
        c.err("backgrounds and UI use %d palettes; the GBA has 16" % len(bg_pals))
    icons.discard(None)
    max_icons = C.read_engine_limits().get("MAX_ICONS", 24)
    if len(icons) > max_icons:
        c.err("HUD, menus and dialogs here can show %d different icons; the engine holds %d (MAX_ICONS)"
              % (len(icons), max_icons))


def upgrade_nodes(p):
    return {nid: n for nid, n in p.nodes.items() if p.type_of(nid) == "upgrade"}


def V_palette(c):
    cols = c.get(c.n, "colors", "list", default=[])
    if not 1 <= len(cols) <= 16:
        c.err("'colors' must have 1 to 16 colors (index 0 is the transparent slot)")
    for i, col in enumerate(cols):
        c.color(col, "colors[%d]" % i)


def V_sprite(c):
    n = c.n
    pal = c.ref(n.get("palette"), ["palette"], "palette")
    w = c.get(n, "width", "int")
    h = c.get(n, "height", "int")
    if w is not None and h is not None and (w, h) not in C.OBJ_SIZES:
        c.err("%dx%d is not a valid sprite size. Valid: %s" % (
            w, h, ", ".join("%dx%d" % s for s in C.OBJ_SIZES)))
        w = h = None
    frames = c.get(n, "frames", "dict", default={})
    if not frames:
        c.err("'frames' needs at least one frame")
    for name, rows in frames.items():
        if not C.ID_RE.match(name):
            c.err("frame name '%s' must be snake_case" % name)
        c.pixels(rows, w, h, pal, "frames.%s" % name)
    if w and h and len(frames) * (w // 8) * (h // 8) > 1024:
        c.err("this sheet needs more than the 1024 OBJ tiles the GBA has")


def V_tileset(c):
    pal = c.ref(c.n.get("palette"), ["palette"], "palette")
    tiles = c.get(c.n, "tiles", "dict", default={})
    if not tiles:
        c.err("'tiles' needs at least one tile")
    for ch, t in tiles.items():
        label = "tiles['%s']" % ch
        if len(ch) != 1 or ch in ". " or not 33 <= ord(ch) <= 126:
            c.err("%s: tile keys must be one printable ASCII character, not '.' or space" % label)
        if not isinstance(t, dict):
            c.err("%s must be an object with 'pixels'" % label)
            continue
        for k in t:
            if k not in ["pixels"] + C.TILE_FLAGS:
                c.err("%s has unknown field '%s'" % (label, k))
        c.pixels(t.get("pixels"), 8, 8, pal, label + ".pixels")
        for f in C.TILE_FLAGS:
            c.get(t, f, "bool", "%s.%s" % (label, f), required=False)
        if t.get("solid") and t.get("one_way"):
            c.err("%s cannot be both solid and one_way" % label)


def V_tilemap(c):
    n = c.n
    ts = c.ref(n.get("tileset"), ["tileset"], "tileset")
    layer = c.get(n, "layer", "int", lo=1, hi=3)
    par = c.get(n, "parallax", "num", required=False, default=1, lo=0, hi=4)
    c.get(n, "repeat_x", "bool", required=False)
    c.get(n, "over", "bool", required=False)
    if layer == 1 and n.get("over"):
        c.err("layer 1 is the ground (collision) layer; only layers 2-3 can be drawn over the sprites")
    if layer == 1 and par != 1:
        c.err("layer 1 is the collision layer and must have parallax 1")
    rows = c.get(n, "rows", "list", default=[])
    if not rows:
        c.err("'rows' needs at least one row")
        return
    w = len(rows[0]) if isinstance(rows[0], str) else 0
    chars = set((c.p.nodes.get(ts, {}).get("tiles") or {}).keys()) if ts else None
    for i, r in enumerate(rows):
        if not isinstance(r, str):
            c.err("rows[%d] must be text" % i)
            return
        if len(r) != w:
            c.err("rows[%d] is %d tiles wide but row 0 is %d" % (i, len(r), w))
            return
        if chars is not None:
            for ch in r:
                if ch != "." and ch not in chars:
                    c.err("rows[%d] uses '%s' but tileset '%s' has no such tile" % (i, ch, ts))
                    return
    if w > 1024 or len(rows) > 1024:
        c.err("map is larger than 1024 tiles in one direction")
    if layer == 1 and (w * 8 < C.SCREEN_W or len(rows) * 8 < C.SCREEN_H):
        c.warn("collision map is smaller than the 240x160 screen")


def V_font(c):
    pal = c.ref(c.n.get("palette"), ["palette"], "palette")
    glyphs = c.get(c.n, "glyphs", "dict", default={})
    if not glyphs:
        c.err("'glyphs' needs at least one glyph")
    for ch, rows in glyphs.items():
        if len(ch) != 1 or not 32 <= ord(ch) <= 126:
            c.err("glyph key '%s' must be one printable ASCII character" % ch)
        c.pixels(rows, 8, 8, pal, "glyphs['%s']" % ch)


def V_icon(c):
    pal = c.ref(c.n.get("palette"), ["palette"], "palette")
    rows = c.get(c.n, "pixels", "list", default=[])
    size = len(rows)
    if size not in (8, 16):
        c.err("an icon must be 8x8 or 16x16 pixels")
        return
    c.pixels(rows, size, size, pal, "pixels")


def V_sfx(c):
    n = c.n
    ch = n.get("channel")
    if ch not in C.CHANNELS:
        c.err("'channel' must be one of %s" % ", ".join(C.CHANNELS))
    c.get(n, "duty", "int", required=False, lo=0, hi=3)
    c.get(n, "priority", "int", required=False, lo=0, hi=255)
    steps = c.get(n, "steps", "list", default=[])
    if not 1 <= len(steps) <= 64:
        c.err("'steps' must have 1 to 64 steps")
    total = 0
    for i, s in enumerate(steps):
        label = "steps[%d]" % i
        if not isinstance(s, dict):
            c.err("%s must be an object" % label)
            continue
        for k in s:
            if k not in ("note", "pitch", "ticks", "volume"):
                c.err("%s has unknown field '%s'" % (label, k))
        total += c.get(s, "ticks", "int", label + ".ticks", lo=1, hi=255) or 0
        c.get(s, "volume", "int", label + ".volume", required=False, lo=0, hi=4 if ch == "wave" else 15)
        if ch == "noise":
            c.get(s, "pitch", "int", label + ".pitch", lo=0, hi=15)
        else:
            note = s.get("note")
            if note != ".." and not (isinstance(note, str) and C.note_ok(note)):
                c.err("%s.note must be a note C2..B7 like \"C#5\", or \"..\" for silence" % label)
    if total > 30:
        c.warn("sound lasts %d ticks; sound effects should be 30 ticks or shorter" % total)


def V_music(c):
    n = c.n
    c.get(n, "bpm", "int", lo=30, hi=300)
    c.get(n, "rows_per_beat", "int", lo=1, hi=16)
    c.get(n, "loop", "bool", required=False)
    inst = c.get(n, "instruments", "dict", default={})
    for ch, ins in inst.items():
        label = "instruments.%s" % ch
        if ch not in C.CHANNELS:
            c.err("'%s': unknown channel (use %s)" % (label, ", ".join(C.CHANNELS)))
            continue
        if not isinstance(ins, dict):
            c.err("'%s' must be an object" % label)
            continue
        allowed = {"square1": ("duty", "volume", "decay"), "square2": ("duty", "volume", "decay"),
                   "wave": ("wave", "volume"), "noise": ("volume", "decay")}[ch]
        for k in ins:
            if k not in allowed:
                c.err("'%s' has unknown field '%s'" % (label, k))
        if ch == "wave":
            w = ins.get("wave")
            if not isinstance(w, str) or not re.match(r"^[0-9a-fA-F]{32}$", w):
                c.err("'%s.wave' must be 32 hex digits (one 4-bit waveform)" % label)
            c.get(ins, "volume", "int", label + ".volume", required=False, lo=0, hi=4)
        else:
            c.get(ins, "volume", "int", label + ".volume", required=False, lo=0, hi=15)
            c.get(ins, "decay", "int", label + ".decay", required=False, lo=0, hi=7)
            if ch != "noise":
                c.get(ins, "duty", "int", label + ".duty", required=False, lo=0, hi=3)
    pats = c.get(n, "patterns", "dict", default={})
    if not pats:
        c.err("'patterns' needs at least one pattern")
    for pname, pat in pats.items():
        label = "patterns.%s" % pname
        if not isinstance(pat, dict) or not pat:
            c.err("'%s' must map channels to note strings" % label)
            continue
        lengths = {}
        for ch, text in pat.items():
            if ch not in C.CHANNELS:
                c.err("'%s' uses unknown channel '%s'" % (label, ch))
                continue
            if ch not in inst:
                c.err("'%s' plays %s, but there is no instruments.%s" % (label, ch, ch))
            if not isinstance(text, str):
                c.err("'%s.%s' must be a string of notes" % (label, ch))
                continue
            toks = C.split_music_tokens(text)
            lengths[ch] = len(toks)
            for t in toks:
                if t in ("--", ".."):
                    continue
                if ch == "noise":
                    if not C.NOISE_RE.match(t):
                        c.err("'%s.noise' has '%s'; noise uses X0..Xf, -- or .." % (label, t))
                        break
                elif not C.note_ok(t):
                    c.err("'%s.%s' has '%s'; use notes C2..B7 like C#5, -- (hold) or .. (silence)" % (label, ch, t))
                    break
        if len(set(lengths.values())) > 1:
            c.err("'%s': every channel must have the same number of rows (%s)" % (
                label, ", ".join("%s=%d" % kv for kv in lengths.items())))
    order = c.get(n, "order", "list", default=[])
    if not order:
        c.err("'order' must list at least one pattern")
    total = 0
    for i, o in enumerate(order):
        if o not in pats:
            c.err("order[%d] names pattern '%s', which does not exist" % (i, o))
        else:
            pat = pats[o]
            if isinstance(pat, dict) and pat:
                first = next(iter(pat.values()))
                total += len(C.split_music_tokens(first)) if isinstance(first, str) else 0
    if total > 8192:
        c.err("song is %d rows long; keep it under 8192" % total)
    lf = c.get(n, "loop_from", "int", required=False, lo=0, hi=max(0, len(order) - 1))
    if lf and n.get("loop") is False:
        c.warn("'loop_from' does nothing on a song that does not loop")


def V_animation(c):
    n = c.n
    spr = c.ref(n.get("sprite"), ["sprite"], "sprite")
    names = set((c.p.nodes.get(spr, {}).get("frames") or {}).keys()) if spr else set()
    c.get(n, "loop", "bool", required=False)
    frames = c.get(n, "frames", "list", default=[])
    if not 1 <= len(frames) <= 64:
        c.err("'frames' must have 1 to 64 frames")
    for i, f in enumerate(frames):
        label = "frames[%d]" % i
        if not isinstance(f, dict):
            c.err("%s must be an object" % label)
            continue
        for k in f:
            if k not in ("frame", "ticks", "flip_x", "flip_y"):
                c.err("%s has unknown field '%s'" % (label, k))
        if spr and f.get("frame") not in names:
            c.err("%s uses frame '%s' but sprite '%s' has %s" % (label, f.get("frame"), spr, ", ".join(sorted(names))))
        c.get(f, "ticks", "int", label + ".ticks", lo=1, hi=255)
        c.get(f, "flip_x", "bool", label + ".flip_x", required=False)
        c.get(f, "flip_y", "bool", label + ".flip_y", required=False)
    for i, ev in enumerate(c.get(n, "events", "list", required=False, default=[])):
        label = "events[%d]" % i
        if not isinstance(ev, dict):
            c.err("%s must be an object" % label)
            continue
        c.get(ev, "at", "int", label + ".at", lo=0, hi=max(0, len(frames) - 1))
        if "action" in ev:
            c.actions([ev["action"]], label + ".action")
        elif "actions" in ev:
            c.actions(ev["actions"], label + ".actions")
        else:
            c.err("%s needs 'action' (or 'actions')" % label)


def V_particle(c):
    n = c.n
    c.ref(n.get("animation"), ["animation"], "animation")
    mode = n.get("mode")
    if mode not in ("burst", "stream"):
        c.err("'mode' must be \"burst\" or \"stream\"")
    c.get(n, "count", "int", required=mode == "burst", lo=1, hi=32)
    c.get(n, "rate", "int", required=mode == "stream", lo=1, hi=255)
    c.get(n, "lifetime", "int", lo=1, hi=600)
    c.get(n, "gravity", "num", required=False, lo=-4, hi=4)
    c.get(n, "on_top", "bool", required=False)
    for k, lo, hi in (("speed", 0, 8), ("angle", -360, 720)):
        v = c.get(n, k, "list")
        if v is not None:
            if len(v) != 2 or not all(is_num(x) for x in v) or v[0] > v[1] or v[0] < lo or v[1] > hi:
                c.err("'%s' must be [min, max] with min <= max, between %s and %s" % (k, lo, hi))


def V_body(c):
    n = c.n
    o = c.get(n, "origin", "list")
    if o is not None and (len(o) != 2 or not all(is_int(v) and 0 <= v <= 64 for v in o)):
        c.err("'origin' must be [x, y] in pixels from the art's top-left")
    hb = c.get(n, "hitbox", "dict")
    if hb is not None:
        for k in ("x", "y"):
            c.get(hb, k, "int", "hitbox." + k, lo=-64, hi=64)
        for k in ("w", "h"):
            c.get(hb, k, "int", "hitbox." + k, lo=1, hi=64)
    ph = c.get(n, "physics", "dict", required=False, default={})
    for k in ph:
        if k not in ("gravity", "solid", "collides_with"):
            c.err("'physics' has unknown field '%s'" % k)
    c.get(ph, "gravity", "bool", "physics.gravity", required=False)
    c.get(ph, "solid", "bool", "physics.solid", required=False)
    for cat in c.get(ph, "collides_with", "list", "physics.collides_with", required=False, default=[]):
        if cat not in C.CATEGORIES:
            c.err("'physics.collides_with' has '%s'; use %s" % (cat, ", ".join(C.CATEGORIES)))
    anims = c.get(n, "animations", "dict", default={})
    if not anims:
        c.err("'animations' needs at least one slot")
    for slot, a in anims.items():
        if slot not in C.ANIM_SLOTS:
            c.err("animation slot '%s' is unknown; use %s" % (slot, ", ".join(C.ANIM_SLOTS)))
        c.ref(a, ["animation"], "animations.%s" % slot)
    d = n.get("default_animation")
    if d not in anims:
        c.err("'default_animation' must be one of the animation slots (%s)" % ", ".join(anims))


def V_actor(c):
    n = c.n
    if c.p.type_of(c.id) == "trigger":
        z = c.get(n, "zone", "dict")
        if z is not None:
            c.get(z, "w", "int", "zone.w", lo=1, hi=1024)
            c.get(z, "h", "int", "zone.h", lo=1, hi=1024)
    else:
        if c.p.type_of(c.id) == "prop" and n.get("body") is None:
            pass                # an invisible prop: only runs its logic (a director, a timer...)
        elif n.get("body") is None and monster_entry(n):
            pass                # a monster: its body comes from its species
        else:
            c.ref(n.get("body"), ["body"], "body")
        snd = c.get(n, "sounds", "dict", required=False, default={})
        for ev, s in snd.items():
            if ev not in C.SOUND_EVENTS:
                c.err("sound '%s' is unknown; use %s" % (ev, ", ".join(C.SOUND_EVENTS)))
            c.ref(s, ["sfx"], "sounds.%s" % ev)
    c.logic(c.get(n, "logic", "list", required=False, default=[]))


def hud_text_width(text):
    return C.placeholder_width(text)


def check_placeholders(c, text, font, label):
    """Placeholders must name game variables (and species fields); the rest must be in the font."""
    for m in C.PLACEHOLDER_RE.finditer(text):
        var, field, pick = m.group(1), m.group(3), m.group(4)
        if var not in c.p.var_names():
            c.err("%s uses {%s}, which is not a game variable" % (label, var))
        if field is not None:
            if field not in C.SPECIES_FIELDS:
                c.err("%s uses {%s.%s}; a species field is one of %s" % (label, var, field, ", ".join(C.SPECIES_FIELDS)))
            if not species_nodes(c.p):
                c.err("%s uses {%s.%s}, but there are no species nodes" % (label, var, field))
            for sid, sp in species_nodes(c.p).items():
                words = [str(sp.get("title", "")), str(sp.get("monster_type", "")).upper()]
                words += [str(c.p.nodes.get(mv, {}).get("title", "")) for mv in sp.get("moves") or []]
                c.text_in_font(" ".join(words), font, "%s (species '%s')" % (label, sid))
        if pick is not None:
            c.text_in_font(pick.replace("|", " "), font, label)
    leftover = C.PLACEHOLDER_RE.sub("", text)
    if "{" in leftover or "}" in leftover:
        c.err("%s has a '{' or '}' that is not a placeholder ({var}, {var:02}, {var.name}, {var|a|b})" % label)
    c.text_in_font(leftover.replace("{", "").replace("}", ""), font, label)


def V_hud(c):
    n = c.n
    font = c.ref(n.get("font"), ["font"], "font")
    els = c.get(n, "elements", "list", default=[])
    for i, e in enumerate(els):
        label = "elements[%d]" % i
        if not isinstance(e, dict):
            c.err("%s must be an object" % label)
            continue
        kind = e.get("kind")
        allowed = {"text": ("text", "align", "blink"), "icon": ("icon",),
                   "icon_repeat": ("icon", "empty_icon", "var", "max_var", "max", "spacing"),
                   "bar": ("var", "max", "max_var", "length", "color", "back", "mid_color", "low_color"),
                   "gauge": ("var", "icons")}.get(kind)
        if allowed is None:
            c.err("%s.kind must be text, icon, icon_repeat, bar or gauge" % label)
            continue
        for k in e:
            if k not in ("kind", "x", "y") + allowed:
                c.err("%s (%s) has unknown field '%s'" % (label, kind, k))
        x = c.get(e, "x", "int", label + ".x", lo=0, hi=C.SCREEN_W - 8)
        y = c.get(e, "y", "int", label + ".y", lo=0, hi=C.SCREEN_H - 8)
        for v, k in ((x, "x"), (y, "y")):
            if v is not None and v % 8:
                c.err("%s.%s must be a multiple of 8 (the HUD sits on the 8x8 tile grid)" % (label, k))
        width_tiles = 0
        if kind == "text":
            t = c.get(e, "text", "str", label + ".text", default="")
            check_placeholders(c, t, font, label + ".text")
            width_tiles = hud_text_width(t)
            if e.get("align", "left") not in ("left", "center"):
                c.err("%s.align must be \"left\" or \"center\"" % label)
            c.get(e, "blink", "int", label + ".blink", required=False, lo=1, hi=255)
            if e.get("align") == "center" and x is not None:
                if x // 8 - (width_tiles + 1) // 2 - 1 < 0 or x // 8 + (width_tiles + 1) // 2 + 1 > 30:
                    c.err("%s (centered on x %d) does not fit on the screen" % (label, x))
                width_tiles = 0
        elif kind in ("icon", "icon_repeat"):
            ic = c.ref(e.get("icon"), ["icon"], label + ".icon")
            size = len(c.p.nodes.get(ic, {}).get("pixels", [])) // 8 if ic else 1
            width_tiles = size
            if kind == "icon_repeat":
                if "empty_icon" in e:
                    c.ref(e["empty_icon"], ["icon"], label + ".empty_icon")
                c.param_value("var", {"required": True}, e.get("var"), label + ".var")
                if "max_var" in e:
                    c.param_value("var", {"required": True}, e["max_var"], label + ".max_var")
                    if "empty_icon" not in e:
                        c.warn("%s.max_var only shows something with an empty_icon" % label)
                mx = c.get(e, "max", "int", label + ".max", lo=1, hi=30) or 1
                sp = c.get(e, "spacing", "int", label + ".spacing", required=False, default=size * 8, lo=8, hi=64)
                if sp % 8:
                    c.err("%s.spacing must be a multiple of 8" % label)
                width_tiles = (mx - 1) * (sp // 8) + size
        elif kind == "bar":
            c.param_value("var", {"required": True}, e.get("var"), label + ".var")
            if "max_var" in e:
                c.param_value("var", {"required": True}, e["max_var"], label + ".max_var")
                c.get(e, "max", "int", label + ".max", required=False, lo=1)
            else:
                c.get(e, "max", "int", label + ".max", lo=1)
            width_tiles = c.get(e, "length", "int", label + ".length", lo=1, hi=30) or 1
            c.get(e, "color", "int", label + ".color", lo=1, hi=15)
            c.get(e, "back", "int", label + ".back", required=False, lo=0, hi=15)
            c.get(e, "mid_color", "int", label + ".mid_color", required=False, lo=1, hi=15)
            c.get(e, "low_color", "int", label + ".low_color", required=False, lo=1, hi=15)
        elif kind == "gauge":
            c.param_value("var", {"required": True}, e.get("var"), label + ".var")
            icons = c.get(e, "icons", "list", label + ".icons", default=[])
            if not 2 <= len(icons) <= 16:
                c.err("%s.icons must list 2 to 16 icons (one per value of the variable)" % label)
            sizes = set()
            for k, ic in enumerate(icons):
                if c.ref(ic, ["icon"], "%s.icons[%d]" % (label, k)):
                    sizes.add(len(c.p.nodes.get(ic, {}).get("pixels", [])))
            if len(sizes) > 1:
                c.err("%s.icons must all be the same size" % label)
            width_tiles = (max(sizes) // 8) if sizes else 1
        if x is not None and x // 8 + width_tiles > 30:
            c.err("%s does not fit on the screen (30 tiles wide)" % label)


def V_menu(c):
    n = c.n
    font = c.ref(n.get("font"), ["font"], "font")
    c.ref(n.get("cursor"), ["icon"], "cursor")
    title = c.get(n, "title", "str", required=False, default="")
    check_placeholders(c, title, font, "title")
    ups = n.get("upgrades")
    if ups is not None:
        V_upgrade_menu(c, font)
        return
    opts = c.get(n, "options", "list", default=[])
    if not 1 <= len(opts) <= 10:
        c.err("'options' must have 1 to 10 options (or use 'upgrades')")
    lay = c.get(n, "layout", "dict", default={})
    for k in lay:
        if k not in ("x", "y", "spacing", "title_y"):
            c.err("'layout' has unknown field '%s'" % k)
    for k in ("x", "y", "spacing", "title_y"):
        v = c.get(lay, k, "int", "layout." + k, required=k in ("x", "y"), lo=0, hi=C.SCREEN_H if k != "x" else C.SCREEN_W)
        if v is not None and v % 8:
            c.err("'layout.%s' must be a multiple of 8" % k)
    if "box" in n:
        c.box(n["box"], "box")
    for i, t in enumerate(c.get(n, "texts", "list", required=False, default=[])):
        label = "texts[%d]" % i
        if not isinstance(t, dict):
            c.err("%s must be {x, y, text}" % label)
            continue
        for k in t:
            if k not in ("x", "y", "text"):
                c.err("%s has unknown field '%s'" % (label, k))
        tx = c.get(t, "x", "int", label + ".x", lo=0, hi=C.SCREEN_W - 8)
        ty = c.get(t, "y", "int", label + ".y", lo=0, hi=C.SCREEN_H - 8)
        text = c.get(t, "text", "str", label + ".text", default="")
        check_placeholders(c, text, font, label + ".text")
        if isinstance(tx, int) and isinstance(ty, int):
            if tx % 8 or ty % 8:
                c.err("%s x and y must be multiples of 8" % label)
            right = tx // 8 + C.placeholder_width(text)
            box = n.get("box") if isinstance(n.get("box"), dict) else None
            if box and all(isinstance(box.get(k), int) for k in "xywh"):
                bx, by, bw, bh = C.box_tiles(box)
                if tx // 8 < bx + 1 or right > bx + bw - 1 or ty // 8 < by + 1 or ty // 8 > by + bh - 2:
                    c.err("%s does not fit inside the box" % label)
            elif right > 30:
                c.err("%s does not fit on the screen" % label)
    lay_ok = isinstance(lay.get("x"), int) and isinstance(lay.get("y"), int)
    pos = C.menu_layout(n)["options"] if lay_ok else []
    for i, o in enumerate(opts):
        label = "options[%d]" % i
        if not isinstance(o, dict):
            c.err("%s must be an object" % label)
            continue
        for k in o:
            if k not in ("label", "actions"):
                c.err("%s has unknown field '%s'" % (label, k))
        text = c.get(o, "label", "str", label + ".label", default="")
        check_placeholders(c, text, font, label + ".label")
        c.actions(o.get("actions", []), label + ".actions")
        if not ({"goto_scene", "close_menu", "open_menu", "show_dialog"} & set(re.findall(r"'do': '(\w+)'", str(o.get("actions", []))))):
            c.warn("%s never closes the menu or changes scene: after picking it the game stays paused" % label)
        if i < len(pos):
            x, y = pos[i]
            if x + C.placeholder_width(text) > 30 or y > 19:
                c.err("%s does not fit on the screen" % label)
            if x < 1:
                c.err("'layout.x' must leave room for the cursor on the left")
    menu_common(c)


def menu_common(c):
    n = c.n
    if "on_cancel" in n:
        c.actions(n["on_cancel"], "on_cancel")
    snd = c.get(n, "sounds", "dict", required=False, default={})
    for k, v in snd.items():
        if k not in ("move", "select"):
            c.err("'sounds' has unknown event '%s' (use move, select)" % k)
        c.ref(v, ["sfx"], "sounds." + k)


def V_upgrade_menu(c, font):
    """A menu whose options are random upgrade cards (picked by the engine when it opens)."""
    n = c.n
    if "options" in n:
        c.err("an upgrade menu ('upgrades') cannot also have 'options'")
    count = c.get(n, "upgrades", "int", lo=1, hi=C.MAX_UPGRADE_CHOICES) or 1
    lay = c.get(n, "layout", "dict", default={})
    for k in lay:
        if k not in ("x", "y", "spacing", "title_y"):
            c.err("'layout' has unknown field '%s'" % k)
    for k in ("x", "y", "spacing", "title_y"):
        v = c.get(lay, k, "int", "layout." + k, required=k in ("x", "y"), lo=0, hi=C.SCREEN_W)
        if v is not None and v % 8:
            c.err("'layout.%s' must be a multiple of 8" % k)
    if "box" not in n:
        c.err("an upgrade menu needs a 'box' (cards are drawn on it)")
    elif c.box(n["box"], "box") and isinstance(lay.get("x"), int) and isinstance(lay.get("y"), int):
        bx, by, bw, bh = C.box_tiles(n["box"])
        cw = len(c.p.nodes.get(n.get("cursor"), {}).get("pixels", [])) // 8 or 1
        x, y = lay["x"] // 8, lay["y"] // 8
        spacing = lay.get("spacing", 16) // 8
        rows = 1 + C.UPGRADE_TEXT_LINES
        if spacing < rows:
            c.err("'layout.spacing' must be at least %d px: each card uses %d rows" % (rows * 8, rows))
        if x - C.UPGRADE_ICON_DX - cw < bx + 1:
            c.err("'layout.x' must leave %d columns inside the box for the cursor and the card icon" % (C.UPGRADE_ICON_DX + cw))
        if x + C.UPGRADE_TEXT_W > bx + bw - 1:
            c.err("cards need %d columns from layout.x, but the box ends sooner (make it wider or move x left)" % C.UPGRADE_TEXT_W)
        if y < by + 1 or y + spacing * (count - 1) + rows > by + bh - 1:
            c.err("%d cards do not fit inside the box (each uses %d rows)" % (count, rows))
    ups = upgrade_nodes(c.p)
    if not any(u.get("category") == "bonus" for u in ups.values()):
        c.warn("no 'bonus' upgrade exists: once everything is maxed out the level-up menu has nothing to offer")
    for uid, u in ups.items():
        c.text_in_font(str(u.get("title", "")) + " NEW! LV123456789 EVOLVE!", font, "upgrade '%s' title" % uid)
        for d in u.get("descriptions", []) if isinstance(u.get("descriptions"), list) else []:
            c.text_in_font(str(d), font, "upgrade '%s' descriptions" % uid)
    menu_common(c)


def V_upgrade(c):
    n = c.n
    cat = n.get("category")
    if cat not in C.UPGRADE_CATEGORIES:
        c.err("'category' must be one of %s" % ", ".join(C.UPGRADE_CATEGORIES))
        return
    title = c.get(n, "title", "str", default="")
    if len(title) > C.UPGRADE_TITLE_W:
        c.err("'title' is %d chars; the card fits %d" % (len(title), C.UPGRADE_TITLE_W))
    icon = c.ref(n.get("icon"), ["icon"], "icon")
    if icon and len(c.p.nodes.get(icon, {}).get("pixels", [])) != 16:
        c.err("'icon' must be a 16x16 icon")
    if cat == "bonus":
        if "max_level" in n or "requires" in n or "replaces" in n:
            c.err("a bonus upgrade can be picked any number of times: no max_level, requires or replaces")
        if n.get("var"):
            c.param_value("var", {}, n["var"], "var")
        levels = 1
    else:
        c.param_value("var", {"required": True}, n.get("var"), "var")
        levels = c.get(n, "max_level", "int", lo=1, hi=9) or 1
        if cat == "evolution" and levels != 1:
            c.err("an evolution has max_level 1")
    descs = c.get(n, "descriptions", "list", default=[])
    want = levels if cat in ("weapon", "item") else 1
    if len(descs) != want:
        c.err("'descriptions' needs %d text(s): one per level the card can offer%s" % (
            want, "" if want == 1 else " (the first is shown when it is new)"))
    for i, d in enumerate(descs):
        if not isinstance(d, str):
            c.err("descriptions[%d] must be text" % i)
            continue
        wrapped, problem = C.wrap_text(d, C.UPGRADE_TEXT_W)
        if problem:
            c.err("descriptions[%d]: %s" % (i, problem))
        elif len(wrapped) > C.UPGRADE_TEXT_LINES:
            c.err("descriptions[%d] needs %d lines; a card fits %d lines of %d chars" % (
                i, len(wrapped), C.UPGRADE_TEXT_LINES, C.UPGRADE_TEXT_W))
    reqs = c.get(n, "requires", "list", required=False, default=[])
    for i, r in enumerate(reqs):
        label = "requires[%d]" % i
        if not isinstance(r, dict):
            c.err("%s must be {upgrade, level}" % label)
            continue
        for k in r:
            if k not in ("upgrade", "level"):
                c.err("%s has unknown field '%s'" % (label, k))
        c.ref(r.get("upgrade"), ["upgrade"], label + ".upgrade")
        lv = r.get("level")
        if lv != "max" and not (is_int(lv) and 1 <= lv <= 9):
            c.err("%s.level must be 1-9 or \"max\"" % label)
    if cat == "evolution":
        if not reqs:
            c.err("an evolution needs 'requires' (the max-level weapon and the item that evolve it)")
        rep = c.ref(n.get("replaces"), ["upgrade"], "replaces")
        if rep and c.p.nodes.get(rep, {}).get("category") != "weapon":
            c.err("'replaces' must name a weapon upgrade")
    elif "replaces" in n:
        c.err("only an evolution can have 'replaces'")
    if "on_pick" in n:
        c.actions(n["on_pick"], "on_pick")


def V_dialog(c):
    n = c.n
    font = c.ref(n.get("font"), ["font"], "font")
    box_ok = c.box(n.get("box"), "box")
    c.get(n, "ticks_per_char", "int", required=False, lo=0, hi=10)
    lines = c.get(n, "lines", "list", default=[])
    if not lines:
        c.err("'lines' needs at least one line")
    for i, ln in enumerate(lines):
        label = "lines[%d]" % i
        if not isinstance(ln, dict):
            c.err("%s must be an object" % label)
            continue
        for k in ln:
            if k not in ("speaker", "portrait", "text"):
                c.err("%s has unknown field '%s'" % (label, k))
        text = c.get(ln, "text", "str", label + ".text", default="")
        sp = c.get(ln, "speaker", "str", label + ".speaker", required=False, default="")
        c.text_in_font(text + sp, font, label)
        if ln.get("portrait"):
            pid = c.ref(ln["portrait"], ["icon"], label + ".portrait")
            if pid and len(c.p.nodes.get(pid, {}).get("pixels", [])) != 16:
                c.err("%s.portrait must be a 16x16 icon" % label)
        if box_ok:
            lay = C.dialog_layout(n["box"], ln)
            if lay["text_w"] < 4 or lay["text_h"] < 1:
                c.err("%s: the box is too small for text" % label)
                continue
            if len(sp) > lay["inner_w"]:
                c.err("%s.speaker is too long for the box" % label)
            wrapped, problem = C.wrap_text(text, lay["text_w"])
            if problem:
                c.err("%s: %s" % (label, problem))
            elif len(wrapped) > lay["text_h"]:
                c.err("%s needs %d lines but the box fits %d; split it into more lines or enlarge the box"
                      % (label, len(wrapped), lay["text_h"]))
    choices = c.get(n, "choices", "list", required=False, default=[])
    if box_ok and choices:
        lay = C.dialog_layout(n["box"], {})
        if len(choices) > lay["inner_h"]:
            c.err("'choices' has %d options but the box fits %d" % (len(choices), lay["inner_h"]))
    for i, ch in enumerate(choices):
        label = "choices[%d]" % i
        if not isinstance(ch, dict):
            c.err("%s must be an object" % label)
            continue
        text = c.get(ch, "label", "str", label + ".label", default="")
        c.text_in_font(text, font, label + ".label")
        if box_ok and len(text) > C.dialog_layout(n["box"], {})["inner_w"] - 2:
            c.err("%s.label is too long for the box" % label)
        c.actions(ch.get("actions", []), label + ".actions")
    if "on_end" in n:
        c.actions(n["on_end"], "on_end")


# --------------------------------------------------------------------------------------
# Monster battles: species and move nodes, game.battle
# --------------------------------------------------------------------------------------
def species_nodes(p):
    """Every species node, in nodes.json order (its number for species_var / {var.name})."""
    return {e["id"]: p.nodes[e["id"]] for e in p.index
            if e.get("type") == "species" and e.get("id") in p.nodes}


def battle_types(p):
    b = p.game.get("battle")
    return b.get("types", []) if isinstance(b, dict) and isinstance(b.get("types"), list) else []


def monster_entry(node):
    """The params of an actor's 'monster' behavior, or None."""
    for e in node.get("logic", []) or []:
        if isinstance(e, dict) and e.get("behavior") == "monster":
            return e.get("params") or {}
    return None


def V_species(c):
    n = c.n
    title = c.get(n, "title", "str", default="")
    if not 1 <= len(title) <= C.SPECIES_TITLE_W:
        c.err("'title' must be 1 to %d characters (the name shown on screen)" % C.SPECIES_TITLE_W)
    if n.get("monster_type") not in battle_types(c.p):
        c.err("'monster_type' must be one of game.battle.types (%s)" % ", ".join(battle_types(c.p)))
    body = c.ref(n.get("body"), ["body"], "body")
    if body:
        if c.p.nodes[body].get("physics", {}).get("gravity"):
            c.warn("'body' has gravity; monsters walk top-down")
    pal = c.ref(n.get("alpha_palette"), ["palette"], "alpha_palette")
    if pal and body:
        sprites = set()
        for a in (c.p.nodes[body].get("animations") or {}).values():
            sprites.add(c.p.nodes.get(a, {}).get("sprite"))
        for s in sprites:
            sp_pal = c.p.nodes.get(s, {}).get("palette")
            if sp_pal and len(c.p.nodes.get(sp_pal, {}).get("colors", [])) > len(c.p.nodes[pal].get("colors", [])):
                c.err("'alpha_palette' has fewer colors than the sprite's palette '%s'" % sp_pal)
    c.ref(n.get("portrait"), ["sprite"], "portrait")
    c.ref(n.get("cry"), ["sfx"], "cry")
    moves = c.get(n, "moves", "list", default=[])
    if len(moves) != 3:
        c.err("'moves' must list exactly 3 moves (A, B, hold A)")
    for i, m in enumerate(moves):
        c.ref(m, ["move"], "moves[%d]" % i)


def V_move(c):
    n = c.n
    title = c.get(n, "title", "str", default="")
    if not 1 <= len(title) <= C.MOVE_TITLE_W:
        c.err("'title' must be 1 to %d characters" % C.MOVE_TITLE_W)
    if n.get("monster_type") not in battle_types(c.p):
        c.err("'monster_type' must be one of game.battle.types (%s)" % ", ".join(battle_types(c.p)))
    shape = n.get("shape")
    if shape not in C.MOVE_SHAPES:
        c.err("'shape' must be one of %s" % ", ".join(C.MOVE_SHAPES))
    c.get(n, "range", "int", required=shape not in ("screen", "self"), lo=0, hi=15)
    power = c.get(n, "power", "int", lo=0, hi=255, default=0)
    acc = c.get(n, "accuracy", "int", required=bool(power) and shape != "zone", lo=1, hi=100)
    c.get(n, "charge", "int", lo=1, hi=65535)
    st = c.get(n, "status", "dict", required=False)
    if st is not None:
        for k in st:
            if k not in ("kind", "chance", "ticks"):
                c.err("'status' has unknown field '%s'" % k)
        if st.get("kind") not in C.STATUSES[1:]:
            c.err("'status.kind' must be one of %s" % ", ".join(C.STATUSES[1:]))
        c.get(st, "chance", "int", "status.chance", lo=1, hi=100)
        t = c.get(st, "ticks", "list", "status.ticks")
        if t is not None and (len(t) != 2 or not all(is_int(v) and 1 <= v <= 65535 for v in t) or t[0] > t[1]):
            c.err("'status.ticks' must be [min, max] ticks")
    if shape == "dash":
        c.get(n, "dash", "int", lo=1, hi=8)
    elif "dash" in n:
        c.err("'dash' is only for the dash shape")
    z = n.get("zone")
    if shape == "zone":
        if not isinstance(z, dict):
            c.err("a zone move needs 'zone' {w, h, place, count, delay, ticks, every}")
        else:
            for k in z:
                if k not in ("w", "h", "place", "count", "delay", "ticks", "every"):
                    c.err("'zone' has unknown field '%s'" % k)
            c.get(z, "w", "int", "zone.w", lo=1, hi=8)
            c.get(z, "h", "int", "zone.h", lo=1, hi=8)
            if z.get("place") not in C.ZONE_PLACES:
                c.err("'zone.place' must be one of %s" % ", ".join(C.ZONE_PLACES))
            c.get(z, "count", "int", "zone.count", required=False, lo=1, hi=6)
            c.get(z, "delay", "int", "zone.delay", required=False, lo=0, hi=600)
            c.get(z, "every", "int", "zone.every", required=False, lo=0, hi=255)
            t = c.get(z, "ticks", "list", "zone.ticks")
            if t is not None and (len(t) != 2 or not all(is_int(v) and 1 <= v <= 65535 for v in t) or t[0] > t[1]):
                c.err("'zone.ticks' must be [min, max] ticks")
    elif z is not None:
        c.err("'zone' is only for the zone shape")
    bf = n.get("buff")
    if shape == "self":
        if not isinstance(bf, dict) or bf.get("kind") not in C.BUFFS[1:]:
            c.err("a self move needs 'buff' {kind: %s, ticks}" % " or ".join(C.BUFFS[1:]))
        else:
            for k in bf:
                if k not in ("kind", "ticks"):
                    c.err("'buff' has unknown field '%s'" % k)
            c.get(bf, "ticks", "int", "buff.ticks", lo=1, hi=3600)
    elif bf is not None:
        c.err("'buff' is only for the self shape")
    if not power and st is None and shape != "self":
        c.warn("this move has no power and no status: it does nothing but its effect")
    if acc is not None and not power:
        c.warn("'accuracy' does nothing on a move without power")
    eff = c.ref(n.get("effect"), ["particle"], "effect", allow_empty=True)
    at = n.get("effect_at", "target")
    if at not in C.EFFECT_AT:
        c.err("'effect_at' must be one of %s" % ", ".join(C.EFFECT_AT))
    elif eff:
        if at in ("tiles", "center") and shape != "zone":
            c.err("'effect_at' %s is only for zone moves" % at)
        if at in ("target", "line") and shape not in ("single", "dash", "cone", "circle", "screen"):
            c.err("'effect_at' %s needs a move that hits targets" % at)
    c.ref(n.get("sfx"), ["sfx"], "sfx", allow_empty=True)
    if "on_use" in n:
        c.actions(n["on_use"], "on_use")


def check_battle(p, r, b):
    where = "nodes/nodes.json"
    if not isinstance(b, dict):
        r.err(where, "game.battle must be an object")
        return
    for k in b:
        if k not in ("types", "chart", "font", "texts", "flash_palette", "par_palette", "sounds"):
            r.err(where, "game.battle has unknown field '%s'" % k)
    types = b.get("types")
    if not isinstance(types, list) or not 1 <= len(types) <= 32 or not all(isinstance(t, str) and C.ID_RE.match(t) for t in types):
        r.err(where, "game.battle.types must list 1 to 32 snake_case type names")
        types = []
    if len(set(types)) != len(types):
        r.err(where, "game.battle.types lists a type twice")
    chart = b.get("chart", {})
    if not isinstance(chart, dict):
        r.err(where, "game.battle.chart must map attacking type -> {defending type: multiplier}")
        chart = {}
    for att, row in chart.items():
        if att not in types:
            r.err(where, "game.battle.chart: '%s' is not one of the types" % att)
            continue
        if not isinstance(row, dict):
            r.err(where, "game.battle.chart.%s must map defending types to multipliers" % att)
            continue
        for dfn, mult in row.items():
            if dfn not in types:
                r.err(where, "game.battle.chart.%s: '%s' is not one of the types" % (att, dfn))
            elif not is_num(mult) or not 0 <= mult <= 2.55:
                r.err(where, "game.battle.chart.%s.%s must be a multiplier from 0 to 2.55" % (att, dfn))
            elif mult == 0:
                r.warn(where, "game.battle.chart.%s.%s is 0: that type can never be hurt by it" % (att, dfn))

    def ref(v, t, label):
        if not isinstance(v, str) or p.type_of(v) != t:
            r.err(where, "game.battle.%s must name a %s node" % (label, t))
            return None
        return v

    font = ref(b.get("font"), "font", "font")
    for k in ("flash_palette", "par_palette"):
        ref(b.get(k), "palette", k)
    texts = b.get("texts")
    if not isinstance(texts, dict):
        r.err(where, "game.battle.texts must be {%s: {text, palette}}" % ", ".join(C.BATTLE_TEXTS))
        texts = {}
    for k in C.BATTLE_TEXTS:
        t = texts.get(k)
        if not isinstance(t, dict):
            r.err(where, "game.battle.texts.%s is missing ({text, palette})" % k)
            continue
        if k != "damage":
            w = t.get("text")
            if not isinstance(w, str) or not 1 <= len(w) <= 15:
                r.err(where, "game.battle.texts.%s.text must be 1 to 15 characters" % k)
            elif font:
                glyphs = p.nodes.get(font, {}).get("glyphs", {})
                for ch in w:
                    if ch != " " and ch not in glyphs and ch.upper() not in glyphs:
                        r.err(where, "game.battle.texts.%s uses '%s' but font '%s' has no glyph for it" % (k, ch, font))
        ref(t.get("palette"), "palette", "texts.%s.palette" % k)
    for k in texts:
        if k not in C.BATTLE_TEXTS:
            r.err(where, "game.battle.texts has unknown word '%s' (use %s)" % (k, ", ".join(C.BATTLE_TEXTS)))
    if font:
        glyphs = p.nodes.get(font, {}).get("glyphs", {})
        if not all(str(d) in glyphs for d in range(10)):
            r.err(where, "game.battle.font '%s' needs the digits 0-9 (damage numbers)" % font)
    snd = b.get("sounds", {})
    if not isinstance(snd, dict):
        r.err(where, "game.battle.sounds must be {%s: sfx}" % ", ".join(C.BATTLE_SOUNDS))
        snd = {}
    for k, v in snd.items():
        if k not in C.BATTLE_SOUNDS:
            r.err(where, "game.battle.sounds has unknown event '%s' (use %s)" % (k, ", ".join(C.BATTLE_SOUNDS)))
        else:
            ref(v, "sfx", "sounds.%s" % k)
    if not species_nodes(p):
        r.warn(where, "game.battle is set but there are no species nodes")


CHECKS = {"scene": V_scene, "palette": V_palette, "sprite": V_sprite, "tileset": V_tileset,
          "tilemap": V_tilemap, "font": V_font, "icon": V_icon, "sfx": V_sfx, "music": V_music,
          "animation": V_animation, "particle": V_particle, "body": V_body, "hud": V_hud,
          "menu": V_menu, "dialog": V_dialog, "upgrade": V_upgrade, "species": V_species, "move": V_move}
for _t in C.ACTOR_TYPES:
    CHECKS[_t] = V_actor


# --------------------------------------------------------------------------------------
# Whole-project checks
# --------------------------------------------------------------------------------------
def check_game(p, r):
    where = "nodes/nodes.json"
    doc = p.index_doc
    if doc.get("format") != 1:
        r.err(where, "'format' must be 1")
    g = doc.get("game")
    if not isinstance(g, dict):
        r.err(where, "'game' settings are missing")
        return
    allowed = {"title", "game_code", "rom_name", "start_scene", "save", "variables", "gravity", "max_fall_speed",
               "battle"}
    for k in g:
        if k not in allowed:
            r.err(where, "game has unknown field '%s'" % k)
    t = g.get("title")
    if not isinstance(t, str) or not 1 <= len(t) <= 12 or not all(32 <= ord(ch) < 127 for ch in t):
        r.err(where, "game.title must be 1 to 12 plain characters")
    gc = g.get("game_code")
    if not isinstance(gc, str) or not re.match(r"^[A-Z0-9]{4}$", gc):
        r.err(where, "game.game_code must be exactly 4 capital letters or digits")
    rn = g.get("rom_name")
    if not isinstance(rn, str) or not C.ID_RE.match(rn):
        r.err(where, "game.rom_name must be snake_case")
    if not isinstance(g.get("save", False), bool):
        r.err(where, "game.save must be true or false")
    for k, lo, hi in (("gravity", 0.01, 2), ("max_fall_speed", 0.5, 8)):
        if k in g and (not is_num(g[k]) or not lo <= g[k] <= hi):
            r.err(where, "game.%s must be a number between %s and %s" % (k, lo, hi))
    ss = g.get("start_scene")
    if ss not in p.by_id or p.kind_of(ss) != "scene":
        r.err(where, "game.start_scene must name a scene")
    if "battle" in g:
        check_battle(p, r, g["battle"])
    seen = set()
    vars_ = g.get("variables", [])
    if not isinstance(vars_, list):
        r.err(where, "game.variables must be a list")
        return
    for i, v in enumerate(vars_):
        label = "game.variables[%d]" % i
        if not isinstance(v, dict):
            r.err(where, "%s must be an object" % label)
            continue
        name = v.get("name")
        if not isinstance(name, str) or not C.ID_RE.match(name):
            r.err(where, "%s.name must be snake_case" % label)
        elif name in seen:
            r.err(where, "variable '%s' is defined twice" % name)
        seen.add(name)
        if v.get("type") not in ("int", "flag"):
            r.err(where, "%s.type must be \"int\" or \"flag\"" % label)
        init = v.get("initial")
        if v.get("type") == "flag" and not isinstance(init, bool):
            r.err(where, "%s.initial must be true or false" % label)
        if v.get("type") == "int" and not is_int(init):
            r.err(where, "%s.initial must be a whole number" % label)
        if not isinstance(v.get("notes"), str) or not v.get("notes").strip():
            r.err(where, "%s needs 'notes' explaining it" % label)
        if not isinstance(v.get("persistent", False), bool):
            r.err(where, "%s.persistent must be true or false" % label)
        for k in v:
            if k not in ("name", "type", "initial", "notes", "persistent"):
                r.err(where, "%s has unknown field '%s'" % (label, k))


def check_index(p, r):
    where = "nodes/nodes.json"
    seen = set()
    listed_paths = set()
    for i, e in enumerate(p.index):
        nid = e.get("id")
        label = "nodes[%d]" % i
        if not isinstance(nid, str) or not C.ID_RE.match(nid):
            r.err(where, "%s.id must be snake_case" % label)
            continue
        if nid in seen:
            r.err(where, "node '%s' is listed twice" % nid)
        seen.add(nid)
        kind, t = e.get("kind"), e.get("type")
        if kind == "scene":
            if t not in C.SCENE_TYPES:
                r.err(where, "'%s': scene type must be one of %s" % (nid, ", ".join(C.SCENE_TYPES)))
                continue
        elif kind == "object":
            if t not in C.OBJECT_TYPES:
                r.err(where, "'%s': object type must be one of %s" % (nid, ", ".join(C.OBJECT_TYPES)))
                continue
        else:
            r.err(where, "'%s': kind must be \"scene\" or \"object\"" % nid)
            continue
        if not nid.startswith(C.prefix_for(kind, t)):
            r.err(where, "'%s' is a %s, so its id must start with '%s'" % (nid, t, C.prefix_for(kind, t)))
        exp = C.expected_path(kind, t, nid)
        if e.get("path") != exp:
            r.err(where, "'%s' path must be \"%s\"" % (nid, exp))
        listed_paths.add(e.get("path"))
        for k in e:
            if k not in ("id", "kind", "type", "path", "parent"):
                r.err(where, "'%s' entry has unknown field '%s'" % (nid, k))
        parent = e.get("parent")
        if kind == "scene" and parent is not None:
            r.err(where, "scene '%s' must be a root node (parent null)" % nid)
        if parent is not None and parent not in p.by_id:
            r.err(where, "'%s' has parent '%s', which does not exist" % (nid, parent))
    for rel in p.disk_files:
        if rel not in listed_paths:
            r.err("nodes/" + rel, "file is not listed in nodes.json (orphan file)")


def check_node_file(p, r, nid):
    e = p.by_id[nid]
    n = p.nodes.get(nid)
    if n is None:
        return
    where = "nodes/" + e.get("path", "?")
    c = Ctx(p, r, nid, n, where)
    if n.get("id") != nid:
        c.err("'id' is '%s' but the file/index says '%s'" % (n.get("id"), nid))
    if n.get("kind") != e.get("kind") or n.get("type") != e.get("type"):
        c.err("kind/type (%s/%s) do not match nodes.json (%s/%s)" % (
            n.get("kind"), n.get("type"), e.get("kind"), e.get("type")))
        return
    for k in ("name", "notes"):
        if not isinstance(n.get(k), str) or not n[k].strip():
            c.err("'%s' is required and must explain the node in plain words" % k)
    if "tags" in n and (not isinstance(n["tags"], list) or not all(isinstance(t, str) for t in n["tags"])):
        c.err("'tags' must be a list of text labels")
    key = "scene" if e["kind"] == "scene" else e["type"]
    if key not in CHECKS:
        return
    for k in n:
        if k not in COMMON_FIELDS and k not in TYPE_FIELDS[key]:
            c.err("unknown field '%s' for a %s" % (k, key))
    if "code" in n:
        if key != "scene" and key not in C.ACTOR_TYPES:
            c.err("'code' is only allowed on scenes and actors")
        elif n["code"] != "src/game/%s.c" % nid:
            c.err("'code' must be \"src/game/%s.c\"" % nid)
        elif not os.path.isfile(os.path.join(C.ROOT, n["code"])):
            c.err("'code' file %s does not exist" % n["code"])
    CHECKS[key](c)


def check_tree(p, r):
    for nid, e in p.by_id.items():
        n = p.nodes.get(nid)
        if n is None:
            continue
        where = "nodes/" + e.get("path", "?")
        kids = n.get("children", [])
        if not isinstance(kids, list):
            r.err(where, "'children' must be a list of ids")
            continue
        if kids and e.get("kind") == "scene":
            r.err(where, "scenes cannot have children; they place objects with 'instances'")
            continue
        allowed = C.OBJECT_TYPES.get(e.get("type"), ("", []))[1]
        for k in kids:
            if k not in p.by_id:
                r.err(where, "child '%s' does not exist" % k)
                continue
            ke = p.by_id[k]
            if ke.get("kind") == "scene":
                r.err(where, "child '%s' is a scene; scenes are always root nodes" % k)
            elif ke.get("type") not in allowed:
                r.err(where, "a %s cannot own a %s ('%s'). It may own: %s" % (
                    e.get("type"), ke.get("type"), k, ", ".join(allowed) or "nothing"))
            if ke.get("parent") != nid:
                r.err(where, "lists child '%s', but nodes.json says its parent is %s" % (k, ke.get("parent")))
        if len(set(kids)) != len(kids):
            r.err(where, "'children' lists a node twice")
    for nid, e in p.by_id.items():
        parent = e.get("parent")
        if parent in p.nodes and nid not in (p.nodes[parent].get("children") or []):
            r.err("nodes/nodes.json", "'%s' has parent '%s', but that node does not list it in 'children'" % (nid, parent))
        seen, cur = set(), nid
        while cur is not None and cur in p.by_id:
            if cur in seen:
                r.err("nodes/nodes.json", "parent chain of '%s' loops back on itself" % nid)
                break
            seen.add(cur)
            cur = p.by_id[cur].get("parent")


def check_catalog(p, r):
    for b in p.behaviors:
        if not C.behavior_source(b):
            r.err("catalog/behaviors.json", "behavior '%s' has no source file src/behaviors/%s.c" % (b, b))
        for pname, pdef in p.behaviors[b].get("params", {}).items():
            if "type" not in pdef or not pdef.get("description"):
                r.err("catalog/behaviors.json", "%s.%s needs a type and a description" % (b, pname))
            if not pdef.get("required") and "default" not in pdef:
                r.err("catalog/behaviors.json", "%s.%s needs a default (or required: true)" % (b, pname))
    for a, spec in p.actions.items():
        if not spec.get("description") or not spec.get("sentence"):
            r.err("catalog/actions.json", "action '%s' needs a description and a sentence" % a)


def check_usage(p, r):
    """Warn about nodes nothing uses (they still cost nothing in the ROM if unused)."""
    text = {nid: str(n) for nid, n in p.nodes.items()}
    used = set()
    for nid, t in text.items():
        for other in re.findall(r"'([a-z][a-z0-9_]*)'", t):
            if other != nid:
                used.add(other)
    used.update(re.findall(r"'([a-z][a-z0-9_]*)'", str(p.game.get("battle", {}))))   # battle font, colors, sounds
    start = p.game.get("start_scene")
    if any(p.type_of(m) == "menu" and n.get("upgrades") for m, n in p.nodes.items()):
        used.update(upgrade_nodes(p))       # every upgrade can be offered by the upgrade menus
    for nid, e in p.by_id.items():
        if nid not in used and nid != start and e.get("parent") is None:
            r.warn("nodes/" + e.get("path", "?"), "nothing uses this node yet")


def validate(project=None):
    """Run every check. Returns a Report with errors and warnings."""
    p = project or C.load_project()
    r = Report()
    for where, msg in p.load_errors:
        r.err(where, msg)
    if not p.index_doc:
        return r
    check_index(p, r)
    check_game(p, r)
    for nid in list(p.by_id):
        e = p.by_id[nid]
        if e.get("kind") in ("scene", "object") and (e.get("type") in C.OBJECT_TYPES or e.get("type") in C.SCENE_TYPES):
            check_node_file(p, r, nid)
    check_tree(p, r)
    check_catalog(p, r)
    check_usage(p, r)
    return r


def main():
    C.setup_console()
    r = validate()
    for w in r.warnings:
        print("  warning  " + w)
    for e in r.errors:
        print("  ERROR    " + e)
    if r.ok:
        print("✔ Validation passed (%d warnings)." % len(r.warnings))
        return 0
    print("✖ Validation failed: %d error(s)." % len(r.errors))
    return 1


if __name__ == "__main__":
    sys.exit(main())
