"""Build the ROM: validate -> codegen -> bundle -> compile -> link -> gbafix.

Usage: python tools/build.py        (result: dist/<rom_name>.gba)

Calls devkitARM's gcc directly (no Makefile, no MSYS shell). Only changed files are
recompiled. Any compiler warning fails the build: the engine must stay warning-free.
"""
import concurrent.futures
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import validate  # noqa: E402
import codegen  # noqa: E402
import bundle  # noqa: E402

CFLAGS = ["-mthumb", "-mcpu=arm7tdmi", "-mtune=arm7tdmi", "-O2", "-std=c11", "-Wall", "-Wextra",
          "-ffunction-sections", "-fdata-sections"]
LDFLAGS = ["-mthumb", "-mcpu=arm7tdmi", "-specs=gba.specs", "-Wl,--gc-sections"]
OBJ_DIR = "build/obj"


def msys_to_windows(path):
    """Map MSYS-style paths (/opt/devkitpro, /c/foo) to Windows paths."""
    if not path:
        return path
    p = path.replace("\\", "/")
    if p.lower().startswith("/opt/devkitpro"):
        return "C:\\devkitPro" + p[len("/opt/devkitpro"):].replace("/", "\\")
    m = re.match(r"^/([a-zA-Z])/(.*)$", p)
    if m:
        return "%s:\\%s" % (m.group(1).upper(), m.group(2).replace("/", "\\"))
    return path


def find_devkitpro():
    """tools/config.json -> env DEVKITPRO -> C:\\devkitPro. Returns the folder or None."""
    cands = []
    cfg = C.load_config().get("devkitpro")
    if cfg:
        cands.append(cfg)
    if os.environ.get("DEVKITPRO"):
        cands.append(msys_to_windows(os.environ["DEVKITPRO"]))
    cands.append("C:\\devkitPro")
    for c in cands:
        if os.path.isfile(os.path.join(c, "devkitARM", "bin", "arm-none-eabi-gcc.exe")) or \
           os.path.isfile(os.path.join(c, "devkitARM", "bin", "arm-none-eabi-gcc")):
            return c
    return None


class Tools:
    def __init__(self, dkp):
        b = os.path.join(dkp, "devkitARM", "bin")
        ext = ".exe" if os.name == "nt" else ""
        self.gcc = os.path.join(b, "arm-none-eabi-gcc" + ext)
        self.objcopy = os.path.join(b, "arm-none-eabi-objcopy" + ext)
        self.gbafix = os.path.join(dkp, "tools", "bin", "gbafix" + ext)
        self.tonc_inc = os.path.join(dkp, "libtonc", "include")
        self.tonc_lib = os.path.join(dkp, "libtonc", "lib")

    def missing(self):
        for path, what in ((self.gcc, "devkitARM compiler"), (self.objcopy, "devkitARM objcopy"),
                           (self.gbafix, "gbafix (gba-tools)"),
                           (os.path.join(self.tonc_inc, "tonc.h"), "libtonc headers"),
                           (os.path.join(self.tonc_lib, "libtonc.a"), "libtonc library")):
            if not os.path.exists(path):
                return what, path
        return None


def run(cmd):
    """Run a command from the repo root (relative paths keep accents/spaces out of gcc args)."""
    r = subprocess.run(cmd, cwd=C.ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def sources():
    out = []
    for base in ("src", "generated"):
        for dirpath, _dirs, files in os.walk(os.path.join(C.ROOT, base)):
            for fn in sorted(files):
                if fn.endswith(".c"):
                    out.append(os.path.relpath(os.path.join(dirpath, fn), C.ROOT).replace(os.sep, "/"))
    return sorted(out)


def read_deps(dfile):
    """Dependency list from a gcc -MMD .d file."""
    try:
        with open(os.path.join(C.ROOT, dfile), "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    text = text.replace("\\\n", " ")
    first = text.split("\n", 1)[0]
    if ":" not in first:
        return []
    body = first.split(": ", 1)[1] if ": " in first else first.split(":", 1)[1]
    deps = re.split(r"(?<!\\)\s+", body.strip())
    return [d.replace("\\ ", " ") for d in deps if d]


def up_to_date(src, obj, dfile, flags_changed):
    if flags_changed:
        return False
    op = os.path.join(C.ROOT, obj)
    if not os.path.isfile(op):
        return False
    t = os.path.getmtime(op)
    deps = read_deps(dfile)
    if deps is None:
        return False
    for d in [src] + deps:
        dp = d if os.path.isabs(d) else os.path.join(C.ROOT, d)
        if not os.path.exists(dp) or os.path.getmtime(dp) > t:
            return False
    return True


def compile_all(tools):
    """Compile changed sources. Returns (objects, ok, problems_text)."""
    os.makedirs(os.path.join(C.ROOT, OBJ_DIR), exist_ok=True)
    cflags = CFLAGS + ["-Isrc", "-Igenerated", "-I" + tools.tonc_inc]
    stamp_path = os.path.join(C.ROOT, OBJ_DIR, "flags.txt")
    stamp = " ".join(cflags)
    try:
        flags_changed = open(stamp_path, "r", encoding="utf-8").read() != stamp
    except OSError:
        flags_changed = True
    jobs, objs = [], []
    for src in sources():
        obj = "%s/%s.o" % (OBJ_DIR, src[:-2])
        dfile = obj[:-2] + ".d"
        objs.append(obj)
        if not up_to_date(src, obj, dfile, flags_changed):
            os.makedirs(os.path.dirname(os.path.join(C.ROOT, obj)), exist_ok=True)
            jobs.append((src, obj, [tools.gcc] + cflags + ["-MMD", "-MP", "-MF", dfile, "-c", src, "-o", obj]))
    ok, problems = True, []
    if jobs:
        print("  compiling %d file(s)..." % len(jobs))
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, (os.cpu_count() or 2))) as ex:
        results = list(ex.map(lambda j: (j, run(j[2])), jobs))
    for (src, obj, _cmd), (rc, out) in results:
        if rc != 0 or "warning:" in out:
            ok = False
            problems.append(out.strip())
            try:
                os.remove(os.path.join(C.ROOT, obj))      # force a recompile next time
            except OSError:
                pass
    with open(stamp_path, "w", encoding="utf-8") as f:
        f.write(stamp)
    return objs, ok, "\n".join(problems)


def build():
    """Full build. Returns 0 on success."""
    C.setup_console()
    t0 = time.time()
    p = C.load_project()
    r = validate.validate(p)
    bundle.bundle(p, r)                                   # the visualizer shows errors too
    for w in r.warnings:
        print("  warning  " + w)
    if not r.ok:
        for e in r.errors:
            print("  ERROR    " + e)
        print("✖ Build stopped: %d validation error(s). Paste them to the AI." % len(r.errors))
        return 1
    print("✔ Validation passed (%d warnings)." % len(r.warnings))
    codegen.generate(p)
    print("✔ Codegen done.")

    dkp = find_devkitpro()
    if not dkp:
        print("✖ devkitPro was not found. Install devkitPro with \"GBA Development\" to C:\\devkitPro "
              "(https://github.com/devkitPro/installer/releases) - or ask the AI to follow SETUP.md.")
        return 1
    tools = Tools(dkp)
    miss = tools.missing()
    if miss:
        print("✖ Missing %s (%s). Ask the AI to fix the toolchain (SETUP.md)." % miss)
        return 1

    objs, ok, problems = compile_all(tools)
    if not ok:
        print(problems)
        print("✖ Compile failed (errors or warnings above). Paste them to the AI.")
        return 1

    rom = p.game["rom_name"]
    os.makedirs(os.path.join(C.ROOT, "dist"), exist_ok=True)
    elf = "build/%s.elf" % rom
    gba = "dist/%s.gba" % rom
    rc, out = run([tools.gcc] + LDFLAGS + objs + ["-L" + tools.tonc_lib, "-ltonc",
                                                  "-Wl,-Map,build/%s.map" % rom, "-o", elf])
    if rc != 0 or "warning:" in out:
        print(out.strip())
        print("✖ Link failed. Paste the messages above to the AI.")
        return 1
    rc, out = run([tools.objcopy, "-O", "binary", elf, gba])
    if rc != 0:
        print(out.strip())
        print("✖ objcopy failed.")
        return 1
    rc, out = run([tools.gbafix, gba, "-t" + p.game["title"], "-c" + p.game["game_code"], "-m01", "-r0"])
    if rc != 0:
        print(out.strip())
        print("✖ gbafix failed.")
        return 1
    size = os.path.getsize(os.path.join(C.ROOT, gba))
    print("✔ Built %s (%d KB) in %.1f s." % (gba, (size + 1023) // 1024, time.time() - t0))
    return 0


if __name__ == "__main__":
    sys.exit(build())
