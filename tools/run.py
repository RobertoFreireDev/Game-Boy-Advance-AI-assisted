"""Play the game: build first if the ROM is missing or older than any source, then open mGBA.

Usage: python tools/run.py
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import build  # noqa: E402

WATCH = ("nodes", "catalog", "src", "tools")


def newest_source_time():
    t = 0
    for base in WATCH:
        for dirpath, _dirs, files in os.walk(os.path.join(C.ROOT, base)):
            if "__pycache__" in dirpath:
                continue
            for fn in files:
                t = max(t, os.path.getmtime(os.path.join(dirpath, fn)))
    return t


def find_emulator():
    """tools/config.json -> common install folders -> PATH."""
    cfg = C.load_config().get("emulator")
    cands = [cfg] if cfg else []
    for env, sub in (("ProgramFiles", "mGBA\\mGBA.exe"), ("ProgramFiles(x86)", "mGBA\\mGBA.exe"),
                     ("LOCALAPPDATA", "Programs\\mGBA\\mGBA.exe")):
        if os.environ.get(env):
            cands.append(os.path.join(os.environ[env], sub))
    cands.append("C:\\Program Files\\mGBA\\mGBA.exe")
    for c in cands:
        if c and os.path.isfile(c):
            return c
    for name in ("mGBA", "mgba-qt", "mgba"):
        w = shutil.which(name)
        if w:
            return w
    return None


def main():
    C.setup_console()
    p = C.load_project()
    rom_name = p.game.get("rom_name", "game")
    rom = os.path.join(C.ROOT, "dist", rom_name + ".gba")
    if not os.path.isfile(rom) or os.path.getmtime(rom) < newest_source_time():
        print("The ROM is missing or out of date - building first.")
        if build.build() != 0:
            print("✖ Not starting the emulator because the build failed.")
            return 1
    emu = find_emulator()
    if not emu:
        print("✖ mGBA was not found. Install it from https://mgba.io/downloads.html "
              "(or ask the AI to follow SETUP.md).")
        return 1
    flags = 0
    if os.name == "nt":
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
    subprocess.Popen([emu, rom], cwd=C.ROOT, creationflags=flags)
    print("✔ Started %s in mGBA." % os.path.relpath(rom, C.ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
