"""Headless play-tests for the AI (Python stdlib only, Windows). The human never uses this.

A test (kept in the session scratchpad, NEW_GAME.md §5) builds a test ROM from the real sources
with a scripted input.c and a few patches, then runs it in mGBA under its GDB stub with a gdb
script that prints the game state at breakpoints. This module does the building and the running,
and turns every way a run can go wrong into a fast, clear error instead of a hang:

    import sys; sys.path.insert(0, r"<repo>/tools"); import emutest as E
    elf, gba = E.build_test_rom(out, "input_test.c", {"src/engine/core.c": [(anchor, new), ...]})
    try:
        log = E.run(elf, gba, script_lines, out, done=lambda line, log: line == "ALL DONE")
    except E.EmuError as e:
        print(e)            # e.kind, e.detail, e.log (the output so far)

`script_lines` is the gdb script after `target remote` (breakpoints, their commands, the last
`continue`); run() adds the connection before it and a crash report after it. Put SCENE_PRINT in
the checkpoint commands so a test that never leaves a menu is caught (kind "stuck").

EmuError kinds:
  start    mGBA didn't open its GDB port, gdb couldn't connect, or the port/lock is taken
  crash    the game crashed: mGBA's "The game has crashed" box, or a fatal line in its log
  signal   the CPU stopped on a signal (illegal instruction...): registers + backtrace attached
  gdb      a gdb command failed (unknown symbol, bad address): the rest of the script never ran
  exit     mGBA or gdb quit on its own
  stall    no output for `stall` seconds (the game hung): a second gdb shows where the CPU is
  stuck    the game sat on one non-level scene (menu, title, game over) for `stuck_frames`
           frames: the test's input never got it out (a locked entry, a missed press)
  stack    the stack came within STACK_MARGIN bytes of the IWRAM variables (needs STACK_PRINT)
  timeout  the whole run took longer than `timeout` seconds
mGBA runs hidden the whole time (its window and any crash box never reach the human's screen,
where a test emulator paused by gdb looks like a frozen game), writes its own log to
<out>/mgba.log, and dies with this process (a Windows job object), so an interrupted test never
leaves an emulator behind. One test at a time per machine: runs take a lock shared by every
session, because the GDB port (2345) is shared.

Usage: python tools/emutest.py selftest     (proves every error above is caught, ~2 minutes)
"""
import concurrent.futures
import ctypes
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import common as C  # noqa: E402
import build as B  # noqa: E402

PORT = 2345
GDB = "C:/devkitPro/devkitARM/bin/arm-none-eabi-gdb.exe"
MGBA = "C:/Program Files/mGBA/mGBA.exe"
LOCK = os.path.join(os.environ.get("TEMP") or tempfile.gettempdir(), "gba-gdb-2345.lock")
# Print at each checkpoint: lets run() see a test sitting on a menu (kind "stuck").
SCENE_PRINT = ("printf \"SCENE node=%d loads=%d f=%u\\n\", 'scene.c'::s_scene_node, "
               "'scene.c'::s_loads, 'core.c'::s_frame")
# Marker run() prints when the script's last `continue` returns: something stopped the game.
END_MARK = "EMUTEST_SCRIPT_END"
WHERE = ["info registers pc lr sp cpsr", "bt 12", "x/4i $pc"]
# extra_cflags for build_test_rom: a NULL pointer dereference stops the game on an illegal
# instruction (kind "signal", with a backtrace). mGBA logs a NULL read only as "Bad BIOS Load".
NULL_CHECK = ["-fsanitize=null", "-fsanitize-undefined-trap-on-error"]
MGBA_LOG_LEVEL = 0x47       # fatal, error, warn and game errors (bad memory accesses...)
# The stack meter compiled into every test ROM: the test input calls emutest_stack_paint() on its
# first frame (it fills the free IWRAM below the stack with a pattern) and emutest_stack_check()
# before each checkpoint; STACK_PRINT in the checkpoint commands lets run() see how deep the stack
# ever went (kind "stack" when it nearly reached the variables: an overflow corrupts them silently).
STACK_PRINT = 'printf "STACK peak=%u size=%u\\n", g_emutest_stack_peak, g_emutest_stack_size'
STACK_MARGIN = 128          # bytes: a peak closer than this to the end of the room is an error
STACK_C = r"""
// TEST BUILD ONLY (tools/emutest.py): how deep the stack has been. The room between the last IWRAM
// variable and the top of the stack is painted once; the lowest overwritten word is the peak.
#include <tonc_types.h>
extern char __iwram_overlay_end[], __sp_usr[];
#define EMUTEST_PAINT 0x5AC3A55Cu
volatile u32 g_emutest_stack_peak, g_emutest_stack_size;
static u32 *emutest_low(void) { return (u32 *)(((u32)__iwram_overlay_end + 3) & ~3u); }
void emutest_stack_paint(void) {
    u32 here = 0;
    u32 *top = (u32 *)(((u32)&here - 256) & ~3u);
    for (u32 *p = emutest_low(); p < top; p++) *p = EMUTEST_PAINT;
    g_emutest_stack_size = (u32)__sp_usr - (u32)emutest_low();
    g_emutest_stack_peak = 0;
}
void emutest_stack_check(void) {
    u32 *p = emutest_low();
    if (!g_emutest_stack_size) return;
    while ((u32)p < (u32)__sp_usr && *p == EMUTEST_PAINT) p++;
    g_emutest_stack_peak = (u32)__sp_usr - (u32)p;
}
"""


class EmuError(Exception):
    """A test run that went wrong: `kind` (see the module doc), a plain `detail`, the `log` so far."""

    def __init__(self, kind, detail, log=()):
        super().__init__("%s: %s" % (kind, detail))
        self.kind, self.detail, self.log = kind, detail, list(log)


def fw(path):
    return path.replace("\\", "/")


# ---- building ---------------------------------------------------------------------------------
def node_numbers(prefix="NODE_", repo=None):
    """{NAME: number} of the NODE_ / VAR_ enums in <repo>/generated/node_ids.h."""
    text = open(os.path.join(repo or C.ROOT, "generated", "node_ids.h"), encoding="utf-8").read()
    out = {}
    for block in re.findall(r"enum \{(.*?)\};", text, re.S):
        names = re.findall(r"^\s*(\w+),?", block, re.M)
        for k, n in enumerate(names):
            if n.startswith(prefix):
                out[n] = k
    return out


def patch_text(text, patches):
    """Apply (anchor, replacement) pairs; every anchor must be there (a renamed line fails loudly)."""
    for anchor, new in patches:
        if anchor not in text:
            raise EmuError("start", "patch anchor not found: %r" % anchor[:120])
        text = text.replace(anchor, new, 1)
    return text


def _sh(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = (r.stdout or "") + (r.stderr or "")
    if r.returncode != 0 or "warning:" in out:
        raise EmuError("start", "build step failed: %s\n%s" % (os.path.basename(cmd[0]), out.strip()[-3000:]))
    return out


def stack_room(map_path):
    """Bytes of IWRAM between the last variable and the top of the stack (from a linker map), or None."""
    try:
        text = open(map_path, "r", encoding="utf-8", errors="replace").read()
    except OSError:
        return None
    end = re.search(r"0x([0-9a-f]+)\s+__iwram_overlay_end = ", text)
    sp = re.search(r"0x([0-9a-f]+)\s+__sp_usr = ", text)
    return int(sp.group(1), 16) - int(end.group(1), 16) if end and sp else None


def build_test_rom(out_dir, input_c, patches=None, repo=None, extra_sources=(), extra_cflags=(),
                   extra_cflags_iwram=(), min_stack=1024):
    """Validate + codegen the tree at `repo` (default: this repo; a `git archive` snapshot works
    too), then compile it with -g into <out_dir>/test.elf + test.gba: src/engine/input.c is
    replaced by `input_c`, `patches` {"src/...c": [(anchor, new), ...]} are applied to copies.
    extra_cflags go to the Thumb (ROM) files, extra_cflags_iwram to the *.iwram.c ones (e.g.
    NULL_CHECK: every NULL pointer read stops the game with a backtrace). The test's own variables
    take IWRAM from the stack: below min_stack bytes of room the build fails (the stack meter, see
    STACK_PRINT, measures what a run really needs). Returns (elf, gba)."""
    repo = repo or C.ROOT
    os.makedirs(out_dir, exist_ok=True)
    r = subprocess.run([sys.executable, os.path.join(repo, "tools", "codegen.py")], cwd=repo,
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise EmuError("start", "validation/codegen failed:\n" + ((r.stdout or "") + (r.stderr or "")).strip()[-3000:])
    tools = B.Tools(B.find_devkitpro())
    for d in ("obj", "patched"):
        os.makedirs(os.path.join(out_dir, d), exist_ok=True)
    cflags = B.CFLAGS + ["-g", "-Isrc", "-Isrc/engine", "-Isrc/behaviors", "-Igenerated", "-I" + tools.tonc_inc]
    jobs = [(fw(os.path.abspath(input_c)), fw(os.path.join(out_dir, "obj", "input_test.o")))]
    jobs += [(fw(os.path.abspath(s)), fw(os.path.join(out_dir, "obj", "extra_%d.o" % k))) for k, s in enumerate(extra_sources)]
    meter = fw(os.path.join(out_dir, "emutest_stack.c"))
    open(meter, "w", encoding="utf-8").write(STACK_C)
    jobs.append((meter, fw(os.path.join(out_dir, "obj", "emutest_stack.o"))))
    srcs = []
    for base in ("src", "generated"):
        for dirpath, _dirs, files in os.walk(os.path.join(repo, base)):
            srcs += [fw(os.path.relpath(os.path.join(dirpath, f), repo)) for f in files if f.endswith(".c")]
    patches = dict(patches or {})
    for src in sorted(srcs):
        if src == "src/engine/input.c":
            continue
        obj = fw(os.path.join(out_dir, "obj", src[:-2].replace("/", "_") + ".o"))
        if src in patches:
            copy = fw(os.path.join(out_dir, "patched", os.path.basename(src)))
            text = open(os.path.join(repo, src), encoding="utf-8").read()
            open(copy, "w", encoding="utf-8").write(patch_text(text, patches.pop(src)))
            src = copy
        jobs.append((src, obj))
    if patches:
        raise EmuError("start", "patched files not found: %s" % ", ".join(sorted(patches)))

    def flags_for(path):
        if path.endswith(".iwram.c"):
            return [f for f in cflags if f != "-mthumb"] + B.IWRAM_FLAGS + list(extra_cflags_iwram)
        return cflags + list(extra_cflags)
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as ex:
        list(ex.map(lambda j: _sh([tools.gcc] + flags_for(j[0]) + ["-c", j[0], "-o", j[1]], repo), jobs))
    elf, gba = fw(os.path.join(out_dir, "test.elf")), fw(os.path.join(out_dir, "test.gba"))
    mp = fw(os.path.join(out_dir, "test.map"))
    _sh([tools.gcc] + B.LDFLAGS + [j[1] for j in jobs] + ["-L" + tools.tonc_lib, "-ltonc",
                                                         "-Wl,-Map," + mp, "-o", elf], repo)
    room = stack_room(mp)                  # an over-full IWRAM hangs the ROM silently
    if room is not None and room < min_stack:
        raise EmuError("start", "test ROM: only %d bytes of IWRAM are left for the stack (needs %d): put the "
                                "test's big arrays in EWRAM (EWRAM_BSS, cleared on the first frame)" % (room, min_stack))
    game = stack_room(os.path.join(repo, "build", C.read_json(os.path.join(repo, "nodes", "nodes.json"))
                                   .get("game", {}).get("rom_name", "game") + ".map"))
    if room is not None and game is not None and room < game - 64:
        print("note: the test ROM has %d bytes of stack room, the game's own build %d (the test's variables "
              "take the rest)" % (room, game), flush=True)
    _sh([tools.objcopy, "-O", "binary", elf, gba], repo)
    _sh([tools.gbafix, gba, "-tTEST", "-cTEST", "-m01", "-r0"], repo)
    return elf, gba


# ---- Windows plumbing: processes, the job object, hidden windows ---------------------------------
_k32 = ctypes.WinDLL("kernel32", use_last_error=True)
_k32.CreateJobObjectW.restype = wintypes.HANDLE
_k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
_k32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
_k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
_k32.OpenProcess.restype = wintypes.HANDLE
_k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
_k32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
_k32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
_k32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
_k32.CloseHandle.argtypes = [wintypes.HANDLE]
_u32 = ctypes.WinDLL("user32", use_last_error=True)
_ENUM = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
_u32.EnumWindows.argtypes = [_ENUM, wintypes.LPARAM]
_u32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
_u32.IsWindowVisible.argtypes = [wintypes.HWND]
_u32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
_u32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]


class _Basic(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_longlong), ("PerJobUserTimeLimit", ctypes.c_longlong),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]


class _Extended(ctypes.Structure):
    _fields_ = [("Basic", _Basic), ("Io", ctypes.c_ulonglong * 6), ("ProcessMemoryLimit", ctypes.c_size_t),
                ("JobMemoryLimit", ctypes.c_size_t), ("PeakProcessMemoryUsed", ctypes.c_size_t),
                ("PeakJobMemoryUsed", ctypes.c_size_t)]


_JOB = None


def _adopt(proc):
    """Put a child process in this process's kill-on-close job: it dies when we do (even when the
    test is interrupted), so no paused test emulator is ever left running."""
    global _JOB
    if _JOB is None:
        job = _k32.CreateJobObjectW(None, None)
        info = _Extended()
        info.Basic.LimitFlags = 0x2000                      # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if job and _k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
            _JOB = job
        else:
            _JOB = 0
    if _JOB:
        _k32.AssignProcessToJobObject(_JOB, wintypes.HANDLE(int(proc._handle)))


def _pid_alive(pid):
    h = _k32.OpenProcess(0x1000, False, pid)              # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return False
    code = wintypes.DWORD()
    ok = _k32.GetExitCodeProcess(h, ctypes.byref(code))
    _k32.CloseHandle(h)
    return bool(ok) and code.value == 259                 # STILL_ACTIVE


def _pid_image(pid):
    h = _k32.OpenProcess(0x1000, False, pid)
    if not h:
        return ""
    buf, n = ctypes.create_unicode_buffer(1024), wintypes.DWORD(1024)
    ok = _k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n))
    _k32.CloseHandle(h)
    return buf.value if ok else ""


def _pid_kill(pid):
    h = _k32.OpenProcess(0x0001 | 0x1000, False, pid)     # PROCESS_TERMINATE
    if h:
        _k32.TerminateProcess(h, 1)
        _k32.CloseHandle(h)


def _listeners(port):
    """PIDs listening on a local TCP port (netstat; its state word may be translated, so a listener
    is told by its foreign address :0)."""
    out = subprocess.run(["netstat", "-ano", "-p", "TCP"], capture_output=True, text=True, errors="replace").stdout
    pids = set()
    for ln in out.splitlines():
        p = ln.split()
        if len(p) >= 5 and p[0] == "TCP" and p[1].endswith(":%d" % port) and p[2].endswith(":0"):
            pids.add(int(p[-1]))
    return pids


class _Windows(threading.Thread):
    """Keeps every window of the test emulator hidden for the whole run (Qt ignores the start-hidden
    flag) and notices mGBA's crash box."""

    def __init__(self, pid):
        super().__init__(daemon=True)
        self.pid, self.stop, self.titles, self.crash = pid, threading.Event(), [], None

    def run(self):
        def cb(hwnd, _):
            owner = wintypes.DWORD()
            _u32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == self.pid:
                buf = ctypes.create_unicode_buffer(256)
                _u32.GetWindowTextW(hwnd, buf, 256)
                title = re.sub(r" \([\d.]+ fps\)", "", buf.value)
                if _u32.IsWindowVisible(hwnd):
                    _u32.ShowWindow(hwnd, 0)              # SW_HIDE
                if title in ("mGBA", "Default IME", "MSCTFIME UI"):
                    title = ""                            # Qt's and the IME's helper windows
                if title and title not in self.titles:
                    self.titles.append(title)
                if "crash" in title.lower() and not self.crash:
                    self.crash = title
            return True
        proc = _ENUM(cb)
        while not self.stop.is_set():
            _u32.EnumWindows(proc, 0)
            time.sleep(0.05)


# ---- the shared emulator lock ------------------------------------------------------------------
def _lock_owner():
    try:
        return open(os.path.join(LOCK, "owner"), encoding="utf-8").read().strip()
    except OSError:
        return ""


def take_lock(name, wait=1800):
    """Take the machine-wide test lock (one emulator on port 2345 at a time). A lock whose owner
    process is gone is stale and taken over."""
    t0 = time.time()
    while True:
        try:
            os.mkdir(LOCK)
            open(os.path.join(LOCK, "owner"), "w", encoding="utf-8").write("%s pid=%d" % (name, os.getpid()))
            return
        except FileExistsError:
            owner = _lock_owner()
            m = re.search(r"pid=(\d+)", owner)
            age = time.time() - os.path.getmtime(LOCK)
            # Old harnesses wrote no pid: their lock is stale once the port is free for a while.
            stale = (not _pid_alive(int(m.group(1)))) if m else (age > 600 and not _listeners(PORT))
            if stale:
                release_lock(force=True)
                continue
            if time.time() - t0 > wait:
                raise EmuError("start", "another test holds the emulator lock (%s) since %.0f s" % (owner or "?", age))
            time.sleep(3)


def release_lock(force=False):
    if not force and "pid=%d" % os.getpid() not in _lock_owner():
        return
    try:
        os.remove(os.path.join(LOCK, "owner"))
    except OSError:
        pass
    try:
        os.rmdir(LOCK)
    except OSError:
        pass


def _free_port():
    """With the lock held, a listener on 2345 is a test emulator a dead harness left behind: kill it."""
    for _ in range(20):
        pids = _listeners(PORT)
        if not pids:
            return
        for pid in pids:
            img = _pid_image(pid)
            if os.path.basename(img).lower() != "mgba.exe":
                raise EmuError("start", "port %d is used by %s (pid %d), not a test emulator" % (PORT, img or "?", pid))
            _pid_kill(pid)
        time.sleep(0.5)
    raise EmuError("start", "port %d is still in use" % PORT)


# ---- running -------------------------------------------------------------------------------
_GDB_ERRORS = [
    ("start", r"(Connection refused|Connection timed out|could not connect|Remote replied unexpectedly)"),
    ("exit", r"(Remote connection closed|Remote communication error|Connection reset|"
             r"Program terminated|The program is not being run)"),
    ("signal", r"^(Program|Thread \S+) received signal"),
    ("gdb", r"^(No symbol .*in current context|Cannot access memory at address|There is no member named|"
            r"A syntax error in expression|Invalid (number|character|data type|cast)|Undefined command|"
            r"Function \".*\" not defined|No source file named|Attempt to |No breakpoint number|"
            r"Argument required|Junk at end of arguments|Bad format string)|Error in sourced command file"),
]


def _scene_watch(repo):
    """Node numbers of the scenes where a test can sit forever waiting for input (all but levels)."""
    try:
        index = C.read_json(os.path.join(repo, "nodes", "nodes.json"))
        nums = node_numbers("NODE_", repo)
    except (OSError, ValueError):
        return {}
    out = {}
    for n in index.get("nodes", []):
        if n.get("kind") == "scene" and n.get("type") != "level":
            k = nums.get("NODE_" + n["id"].upper())
            if k is not None:
                out[k] = n["id"]
    return out


def _where(elf, extra=(), seconds=25):
    """Attach a second gdb to the running emulator and say where the CPU is (a hang)."""
    lines = ["set pagination off", "set confirm off", "set height 0", "target remote localhost:%d" % PORT,
             'printf "WHERE\\n"'] + WHERE + list(extra)
    path = elf + ".where.gdb"
    open(path, "w").write("\n".join(lines) + "\n")
    try:
        r = subprocess.run([GDB, "-q", "-batch", "-x", path, elf], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=seconds)
        return [ln for ln in (r.stdout + r.stderr).splitlines() if ln.strip()]
    except subprocess.TimeoutExpired:
        return ["(a second gdb could not attach within %d s)" % seconds]


def _mgba_log_news(path, pos):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            f.seek(pos)
            text = f.read()
            return text.splitlines(), f.tell()
    except OSError:
        return [], pos


def run(elf, gba, script_lines, out_dir, done=None, timeout=3600, stall=90, stuck_frames=3600,
        fps=600, save="fresh", echo=True, name="emutest", where=(), repo=None):
    """Run the test ROM in a hidden mGBA under gdb and return gdb's output lines.

    done(line, log) -> True ends the run (the test has what it needs). timeout = most seconds for
    the whole run; stall = most seconds without any output; stuck_frames = most game frames on one
    non-level scene (needs SCENE_PRINT lines; None = no check). save: "fresh" (no save file),
    "keep" (the one next to the ROM, e.g. a second boot) or a .sav path to start from. where =
    extra gdb lines printed when the game hangs (e.g. a frame counter). Raises EmuError."""
    C.setup_console()
    take_lock(name)
    try:
        return _run_locked(elf, gba, script_lines, out_dir, done, timeout, stall, stuck_frames, fps, save,
                           echo, where, repo or C.ROOT)
    finally:
        release_lock()


def _run_locked(elf, gba, script_lines, out_dir, done, timeout, stall, stuck_frames, fps, save, echo,
                where, repo):
    _free_port()
    sav = os.path.splitext(gba)[0] + ".sav"
    if save == "fresh":
        if os.path.exists(sav):
            os.remove(sav)
    elif save != "keep":
        shutil.copy(save, sav)
    mlog = fw(os.path.join(out_dir, "mgba.log"))
    if os.path.exists(mlog):
        os.remove(mlog)
    _k32.SetErrorMode(0x0001 | 0x0002 | 0x8000)          # no Windows "stopped working" boxes
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    emu = subprocess.Popen([MGBA, "-g", "-C", "fpsTarget=%d" % fps, "-C", "logToFile=1", "-C", "logFile=" + mlog,
                            "-C", "logLevel=%d" % MGBA_LOG_LEVEL, gba], startupinfo=si)
    _adopt(emu)
    win = _Windows(emu.pid)
    win.start()
    log, g, stack = [], None, [0, 0]

    def fail(kind, detail):
        tail = [x for x in log[-12:]]
        if win.titles:
            detail += "\n  mGBA windows: %s" % ", ".join(win.titles)
        raise EmuError(kind, detail + ("\n  last output:\n    " + "\n    ".join(tail) if tail else ""), log)

    try:
        t0 = time.time()
        while emu.pid not in _listeners(PORT):
            if emu.poll() is not None:
                fail("start", "mGBA quit at start (exit code %s)" % emu.returncode)
            if time.time() - t0 > 20:
                fail("start", "mGBA did not open its GDB port %d within 20 s" % PORT)
            time.sleep(0.25)
        script = ["set pagination off", "set confirm off", "set height 0", "target remote localhost:%d" % PORT]
        script += list(script_lines) + ['printf "%s\\n"' % END_MARK] + WHERE
        path = fw(os.path.join(out_dir, "cmds.gdb"))
        open(path, "w").write("\n".join(script) + "\n")
        g = subprocess.Popen([GDB, "-q", "-batch", "-x", path, elf], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        _adopt(g)
        lines = queue.Queue()

        def reader():
            for ln in g.stdout:
                lines.put(ln.rstrip())
            lines.put(None)
        threading.Thread(target=reader, daemon=True).start()
        watch = _scene_watch(repo) if stuck_frames else {}
        scene, scene_f0, mpos, start, last = None, 0, 0, time.time(), time.time()
        eof = False
        while True:
            try:
                ln = lines.get(timeout=0.5)
            except queue.Empty:
                ln = ""
            now = time.time()
            if ln is None:
                eof = True
            elif ln:
                last = now
                log.append(ln)
                if echo:
                    print(ln, flush=True)
                if done and done(ln, log):
                    return log
                if ln == END_MARK:                         # the script's last continue returned
                    _drain(lines, log, echo, 5)
                    sig = [x for x in log if re.search(_GDB_ERRORS[2][1], x)]
                    if sig:
                        fail("signal", "the game stopped on a signal: %s\n  where:\n    %s" % (
                            sig[-1], "\n    ".join(log[log.index(END_MARK) + 1:][:14])))
                    fail("gdb", "the gdb script stopped early (a breakpoint command failed?)")
                for kind, pat in _GDB_ERRORS:
                    if re.search(pat, ln) and kind != "signal":
                        if kind == "exit":
                            time.sleep(1.0)                # the crash box or log line may follow
                            _check_crash(win, mlog, fail)
                        elif kind == "gdb":
                            _drain(lines, log, echo, 1)    # the line saying what was wrong follows
                            ln = " / ".join(x for x in log[log.index(ln):][:3])
                        fail(kind, "gdb: " + ln)
                m = re.match(r"STACK peak=(\d+) size=(\d+)", ln)
                if m and int(m.group(2)):
                    stack[:] = [max(stack[0], int(m.group(1))), int(m.group(2))]
                    if stack[0] > stack[1] - STACK_MARGIN:
                        fail("stack", "the stack went %d bytes deep: only %d of its %d bytes of IWRAM were "
                                      "left (an overflow overwrites variables, then the game crashes)"
                             % (stack[0], stack[1] - stack[0], stack[1]))
                m = re.match(r"SCENE node=(-?\d+) loads=(\d+) f=(\d+)", ln)
                if m and watch:
                    key, f = (int(m.group(1)), int(m.group(2))), int(m.group(3))
                    if key != scene:
                        scene, scene_f0 = key, f
                    elif key[0] in watch and f - scene_f0 > stuck_frames:
                        fail("stuck", "the game sat on %s for %d frames (%.0f s of game time) without leaving it: "
                                      "the test's input never got past it (a locked entry? a press on the wrong "
                                      "frame?)" % (watch[key[0]], f - scene_f0, (f - scene_f0) / 60.0))
            new, mpos = _mgba_log_news(mlog, mpos)
            for x in new:
                if re.search(r"\bFATAL\b|Jumped to invalid address", x):
                    fail("crash", "the game crashed (mGBA log): " + x.strip())
            _check_crash(win, mlog, fail)
            if emu.poll() is not None:
                fail("exit", "mGBA quit on its own (exit code %s)" % emu.returncode)
            if eof:
                code = g.wait()
                fail("exit", "gdb quit on its own (exit code %s)" % code)
            if now - start > timeout:
                fail("timeout", "the run took more than %d s" % timeout)
            if now - last > stall:
                g.kill()
                time.sleep(0.5)
                spot = _where(elf, where)
                fail("stall", "no output for %d s: the game (or the emulator) hung.\n  where the CPU is:\n    %s"
                     % (stall, "\n    ".join(spot)))
    finally:
        win.stop.set()
        for p in (g, emu):
            if p and p.poll() is None:
                p.kill()
        for p in (g, emu):
            if p:
                try:
                    p.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    pass
        open(os.path.join(out_dir, "log.txt"), "w", encoding="utf-8").write("\n".join(log) + "\n")
        report_mgba_log(mlog)
        if stack[1]:
            print("stack: at most %d of %d bytes used (%d never touched)" % (stack[0], stack[1], stack[1] - stack[0]),
                  flush=True)


def report_mgba_log(mlog):
    """Print what mGBA complained about during the run (only warnings and worse are logged; the level
    words are in the system's language, the messages in English): bad memory reads, illegal opcodes."""
    C.setup_console()
    counts = {}
    for x in _mgba_log_news(mlog, 0)[0]:
        msg = re.sub(r"^\[[^\]]*\]\s*", "", x.strip())
        # Byte reads of 0-3 are gdb looking at the PC (0) as it attaches; a NULL read by the game
        # shows up with NULL_CHECK instead.
        if not msg or re.search(r"Bad BIOS Load8: 0x0000000[0-3]$", msg):
            continue
        counts[msg] = counts.get(msg, 0) + 1
    if counts:
        top = sorted(counts.items(), key=lambda kv: -kv[1])[:4]
        print("⚠ mGBA logged %d warning/error line(s) (%s): %s" % (
            sum(counts.values()), mlog, "; ".join("%s x%d" % kv for kv in top)), flush=True)
    return counts


def _drain(lines, log, echo, seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            ln = lines.get(timeout=0.25)
        except queue.Empty:
            continue
        if ln is None:
            return
        log.append(ln)
        if echo:
            print(ln, flush=True)


def _check_crash(win, mlog, fail):
    if win.crash:
        found = [x.strip() for x in _mgba_log_news(mlog, 0)[0] if re.search(r"\bFATAL\b|Jumped to invalid", x)]
        fail("crash", "the game crashed: mGBA showed its '%s' box%s" % (win.crash, (": " + found[-1]) if found else ""))


# ---- self-test ----------------------------------------------------------------------------------
SELFTEST_INPUT = r"""
// TEST BUILD ONLY (tools/emutest.py selftest): no buttons, a checkpoint every 30 frames, and a
// failure the gdb script picks at checkpoint 2 (g_fail): 1 jump to an invalid address, 2 hang,
// 3 illegal instruction, 4 the stack's lowest words written (as an overflow would), 0 nothing (the
// game sits on its start scene).
#include <tonc_types.h>
#include "engine/input.h"
extern char __iwram_overlay_end[];
void emutest_stack_paint(void);
void emutest_stack_check(void);
volatile u32 g_test_frames, g_fail;
__attribute__((noinline)) void test_checkpoint(void) { __asm__ volatile("" ::: "memory"); }
void input_update(void) {
    if (++g_test_frames == 1) emutest_stack_paint();
    if (g_fail == 1 && g_test_frames > 100) ((void (*)(void))0x0E000001)();
    if (g_fail == 2 && g_test_frames > 100) for (;;) __asm__ volatile("" ::: "memory");
    if (g_fail == 3 && g_test_frames > 100) __asm__ volatile(".hword 0xDE00");
    if (g_fail == 4 && g_test_frames > 100) *(volatile u32 *)((((u32)__iwram_overlay_end + 3) & ~3u) + 32) = 0;
    if (g_test_frames % 30 == 0) { emutest_stack_check(); test_checkpoint(); }
}
int input_held(u16 mask) { (void)mask; return 0; }
int input_pressed(u16 mask) { (void)mask; return 0; }
int input_released(u16 mask) { (void)mask; return 0; }
int input_dir_x(void) { return 0; }
int input_dir_y(void) { return 0; }
"""


def selftest():
    """Run the current game once per failure kind and check each is reported as it should be."""
    C.setup_console()
    out = fw(os.path.join(tempfile.gettempdir(), "emutest_selftest"))
    os.makedirs(out, exist_ok=True)
    src = os.path.join(out, "input_selftest.c")
    open(src, "w", encoding="utf-8").write(SELFTEST_INPUT)
    elf, gba = build_test_rom(out, src)
    print("✔ test ROM built")

    def script(fail, more=()):
        return ["break test_checkpoint", "commands", "silent", 'printf "CP f=%u\\n", g_test_frames', SCENE_PRINT,
                STACK_PRINT, "if g_test_frames == 60", "set var g_fail = %d" % fail, "end"] + list(more) + [
                "continue", "end", "continue"]
    cases = [
        ("normal run, done after 10 checkpoints", script(0), dict(done=lambda ln, log: sum(x.startswith("CP ") for x in log) >= 10), None),
        ("jump to an invalid address", script(1), {}, "crash"),
        ("hang (endless loop)", script(2), dict(stall=15, where=['printf "frames=%u\\n", g_test_frames']), "stall"),
        ("illegal instruction", script(3), {}, "signal"),
        ("stack nearly overflowing", script(4), {}, "stack"),
        ("unknown symbol in a breakpoint command", script(0, ['printf "%d\\n", g_no_such_thing']), {}, "gdb"),
        ("never leaves the start scene", script(0), dict(stuck_frames=600), "stuck"),
    ]
    fails = 0
    for what, lines, kw, want in cases:
        t0 = time.time()
        kw.setdefault("timeout", 120)
        try:
            run(elf, gba, lines, out, echo=False, name="emutest-selftest", **kw)
            got, detail = None, "finished"
        except EmuError as e:
            got, detail = e.kind, e.detail
        ok = got == want
        fails += not ok
        print("%s %-40s -> %-8s in %4.1f s   %s" % ("PASS" if ok else "FAIL", what, got or "ok", time.time() - t0,
                                                   detail.splitlines()[0][:110]))
    print("\nRESULT:", "ALL PASS" if not fails else "%d FAIL" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["selftest"]:
        sys.exit(selftest())
    print(__doc__)
