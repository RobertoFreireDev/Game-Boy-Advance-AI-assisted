# SETUP.md — Installing and checking the toolchain

How to install, configure and verify every tool this repository needs on Windows 10/11.
`CLAUDE.md` §3 says *what* the toolchain is; this file says *how* to get it working and how
to prove it works. It is written from a real install (2026-10-02) and records the problems
found along the way.

**The AI does the install.** Run every step yourself from the Bash/PowerShell tools. The
human only has to accept the Windows admin (UAC) prompt that some installers show. Only if
an automatic step fails for good, tell the human which official installer to click (§6).

---

## 1. What gets installed and where

| Piece | Version tested | Installed to | How |
|-------|----------------|--------------|-----|
| Python | 3.12.10 | `%LOCALAPPDATA%\Programs\Python\Python312\python.exe`, launcher `%LOCALAPPDATA%\Programs\Python\Launcher\py.exe` | winget, per-user |
| mGBA | 0.10.5 | `C:\Program Files\mGBA\mGBA.exe` | winget (UAC prompt) |
| MSYS2 (devkitPro build) | 2.10.0 base, then updated | `C:\devkitPro\msys2` | manual extract (§2.3) |
| devkitARM | r68 (gcc 16.1.0) | `C:\devkitPro\devkitARM\bin\arm-none-eabi-*.exe` | `pacman -S gba-dev` |
| libtonc | 1.4.5 | `C:\devkitPro\libtonc\include`, `C:\devkitPro\libtonc\lib\libtonc.a` | part of `gba-dev` |
| gba-tools (gbafix…) | 1.2.0 | `C:\devkitPro\tools\bin\gbafix.exe` | part of `gba-dev` |
| GBA link specs | — | `C:\devkitPro\devkitARM\arm-none-eabi\lib\gba.specs` | part of `devkitARM` |

User environment variables (same values the official devkitPro installer sets):
`DEVKITPRO=/opt/devkitpro`, `DEVKITARM=/opt/devkitpro/devkitARM`. These are MSYS-style
paths, so `tools/build.py` must map `/opt/devkitpro` → `C:\devkitPro` (see `CLAUDE.md` §3).

---

## 2. Install steps

First check what already exists, and skip anything that is already there:

```powershell
Test-Path C:\devkitPro\devkitARM\bin\arm-none-eabi-gcc.exe
Test-Path "C:\Program Files\mGBA\mGBA.exe"
Test-Path "$env:LOCALAPPDATA\Programs\Python\Launcher\py.exe"
```

### 2.1 Python (winget)

```powershell
winget install --id Python.Python.3.12 -e --scope user --silent `
  --accept-package-agreements --accept-source-agreements `
  --override "/quiet InstallAllUsers=0 PrependPath=1 Include_launcher=1 Include_test=0"
```

- `PrependPath=1` adds Python to the **user** PATH, and `Include_launcher=1` installs `py`.
- **Gotcha:** shells that were already open (including the AI's own session) keep the old
  PATH, so `py` or `python` there give "not found", or the Microsoft Store stub message
  *"Python não foi encontrado…"*. Call Python by full path in that session
  (`%LOCALAPPDATA%\Programs\Python\Python312\python.exe`). New windows and double-clicked
  `.bat` files get the new PATH.

### 2.2 mGBA (winget)

```powershell
winget install --id JeffreyPfau.mGBA -e --silent --accept-package-agreements --accept-source-agreements
```

The installer asks for admin rights, so the human must accept the UAC prompt. It installs
to `C:\Program Files\mGBA\` and does not add mGBA to PATH. `tools/run.py` must look in that
folder.

### 2.3 devkitPro + GBA toolchain (manual, no GUI)

devkitPro is **not on winget**. Its official installer (`devkitProUpdater-3.0.3.exe` from
`github.com/devkitPro/installer/releases`) is a GUI NSIS installer. **`/S` silent mode does
not work:** it exits with code 0 and installs nothing. The installer only does the steps
below, so run them directly.

**a. Download the MSYS2 base archive.** The archive name is in the update manifest.
`downloads.devkitpro.org` returns **403** to PowerShell's `Invoke-WebRequest`, so use
`curl` with `-L` (it redirects) and a browser-like user agent:

```bash
curl -sSL -A "NSIS_Inetc (Mozilla)" https://downloads.devkitpro.org/devkitProUpdate.ini
# → [msys2] File=msys-2.10.0.1.7z   (use whatever name it lists)
curl -sSL -A "NSIS_Inetc (Mozilla)" https://downloads.devkitpro.org/msys-2.10.0.1.7z -o "$SCRATCH/msys.7z"
```

**b. Extract it to `C:\devkitPro`.** There is no 7-Zip on the machine. Use Windows' own
`C:\Windows\System32\tar.exe` (bsdtar, which reads `.7z`). Git Bash's GNU `tar` can't read
`.7z`. The archive contains a top-level `msys2/` folder.

```powershell
New-Item -ItemType Directory -Force C:\devkitPro | Out-Null
& C:\Windows\System32\tar.exe -xf "<scratch>\msys.7z" -C C:\devkitPro
```

**c. Fill in the placeholders in `C:\devkitPro\msys2\etc\fstab`.** Write the file as UTF-8
**without BOM**:

```powershell
$f = "C:\devkitPro\msys2\etc\fstab"
$t = [IO.File]::ReadAllText($f).Replace("#{DEVKITPRO}", "C:\devkitPro").Replace("#{PROFILES_ROOT}", "C:\Users")
[IO.File]::WriteAllText($f, $t, (New-Object Text.UTF8Encoding $false))
```

This maps `/opt/devkitpro` inside MSYS2 to `C:\devkitPro`.

**d. First login.** This creates the pacman keyring and imports the devkitPro keys. Lots of
`gpg: error retrieving … via WKD` lines are normal.

```powershell
& C:\devkitPro\msys2\usr\bin\bash.exe --login -c exit
```

**e. Update, then install `gba-dev`. Always run pacman inside a login shell** (`bash --login -c "…"`).
**Gotcha:** if you call `pacman.exe` directly, its post-install hooks fail
(`error: command failed to execute correctly`). The CA bundle then ends up empty, and every
later download fails with
`error adding trust anchors from file: /usr/ssl/certs/ca-bundle.crt`. If that happens, run
`update-ca-trust` in a login shell to fix it.

```powershell
& C:\devkitPro\msys2\usr\bin\bash.exe --login -c "pacman -Syu --noconfirm"   # 1st: updates pacman/msys2-runtime itself
& C:\devkitPro\msys2\usr\bin\bash.exe --login -c "pacman -Syu --noconfirm"   # 2nd: updates the rest
& C:\devkitPro\msys2\usr\bin\bash.exe --login -c "update-ca-trust; pacman -S gba-dev --noconfirm --needed"
& C:\devkitPro\msys2\usr\bin\bash.exe --login -c "pacman -Syu --noconfirm"   # should say "nothing to do"
```

The first `-Syu` stops with *"all MSYS2 processes … will be closed"*, which is expected, so
run it again. `gba-dev` installs 23 packages, about 96 MB to download and 382 MB on disk:
devkitARM, libtonc, libgba, gba-tools, grit, maxmod, examples…

**f. Environment variables.** User level, so no admin is needed:

```powershell
[Environment]::SetEnvironmentVariable('DEVKITPRO','/opt/devkitpro','User')
[Environment]::SetEnvironmentVariable('DEVKITARM','/opt/devkitpro/devkitARM','User')
```

The build does **not** depend on these variables or on PATH. `tools/build.py` falls back to
`C:\devkitPro` (`CLAUDE.md` §3).

---

## 3. Verify everything works

Run all checks after an install, and whenever a build fails with a "not found" error. Every
check below passed on the reference install.

### 3.1 Versions

```powershell
& "$env:LOCALAPPDATA\Programs\Python\Launcher\py.exe" -3 --version          # Python 3.12.x (need ≥ 3.10)
& C:\devkitPro\devkitARM\bin\arm-none-eabi-gcc.exe --version                # arm-none-eabi-gcc.exe (devkitARM) 16.1.0
(Get-Item "C:\Program Files\mGBA\mGBA.exe").VersionInfo.ProductVersion      # 0.10.x
& C:\devkitPro\msys2\usr\bin\bash.exe --login -c "pacman -Q devkitARM libtonc gba-tools"
Test-Path C:\devkitPro\libtonc\include\tonc.h, C:\devkitPro\libtonc\lib\libtonc.a, `
          C:\devkitPro\tools\bin\gbafix.exe, C:\devkitPro\devkitARM\arm-none-eabi\lib\gba.specs
```

### 3.2 Smoke ROM (whole compile → link → objcopy → gbafix chain)

Use the scratchpad and never the repo, because these files are throwaway. This uses the
same flags as `CLAUDE.md` §3. **Note:** when gcc is called directly (no devkitPro Makefile),
libtonc's include and lib folders must be passed **explicitly** with `-I` and `-L`.

```bash
cat > t.c <<'EOF'
#include <tonc.h>
int main(void){ REG_DISPCNT = DCNT_MODE3|DCNT_BG2; m3_plot(120,80,RGB15(31,0,0)); while(1) VBlankIntrWait(); }
EOF
B=/c/devkitPro/devkitARM/bin
$B/arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -mtune=arm7tdmi -O2 -std=c11 -Wall -Wextra \
    -I/c/devkitPro/libtonc/include -c t.c -o t.o
$B/arm-none-eabi-gcc -mthumb -mcpu=arm7tdmi -specs=gba.specs t.o \
    -L/c/devkitPro/libtonc/lib -ltonc -Wl,--gc-sections -o t.elf
$B/arm-none-eabi-objcopy -O binary t.elf t.gba
/c/devkitPro/tools/bin/gbafix t.gba -tTEST -cTEST      # prints "ROM fixed!"
```

Pass means zero warnings, `ROM fixed!`, and a `t.gba` of a few hundred bytes (908 bytes on
the reference install).

### 3.3 Emulator launch

```powershell
$p = Start-Process "C:\Program Files\mGBA\mGBA.exe" -ArgumentList '"<scratch>\t.gba"' -PassThru
Start-Sleep 4; $p.Refresh(); "mGBA running: $(-not $p.HasExited)"; Stop-Process -Id $p.Id
```

Pass means `mGBA running: True`. A human watching would see a single red pixel in the
middle of a black screen. Quote the ROM path: the repo path contains spaces and `Á`.

### 3.4 Project-level check (once the tools exist)

`python tools/build.py` must end with `dist/<rom_name>.gba` and zero warnings, and
`python tools/run.py` must open it in mGBA. That is the real "toolchain OK" signal for
everyday work.

---

## 4. Known pitfalls (quick reference)

| Symptom | Cause | Fix |
|---------|-------|-----|
| `py`/`python` not found right after installing | Shell started before the PATH change | Use the full path, or open a new shell |
| devkitPro installer `/S` exits 0, nothing in `C:\devkitPro` | NSIS silent mode doesn't select or download packages | Manual install, §2.3 |
| `403 Forbidden` from `downloads.devkitpro.org` | Server blocks PowerShell's user agent | `curl -sSL -A "NSIS_Inetc (Mozilla)"` |
| `error adding trust anchors from file: /usr/ssl/certs/ca-bundle.crt` | pacman run outside a login shell, so hooks skipped | `bash --login -c "update-ca-trust"`, and always run pacman via `bash --login -c` |
| `pacman -Syu` stops asking to close MSYS2 | Core packages updated first | Run `-Syu` again |
| `tonc.h: No such file` / `cannot find -ltonc` | gcc called without a Makefile | Pass `-I C:\devkitPro\libtonc\include -L C:\devkitPro\libtonc\lib` |
| Paths with spaces or accents break commands | Repo lives in `…\Área de Trabalho\…` | Always quote paths; Python `subprocess` with list args (no `shell=True`) |
| `pacman` prompts in Portuguese | System locale is pt-BR | Harmless; the `--noconfirm` answers are the same |

---

## 5. Updating later

```powershell
& C:\devkitPro\msys2\usr\bin\bash.exe --login -c "pacman -Syu --noconfirm"
winget upgrade --id JeffreyPfau.mGBA -e --silent
winget upgrade --id Python.Python.3.12 -e --silent
```

After any update, run §3 again and write down new versions in §1 if they changed.

---

## 6. Manual fallback for the human (only if automation fails)

1. **Python**: https://www.python.org/downloads/. Tick **"Add python.exe to PATH"**.
2. **mGBA**: https://mgba.io/downloads.html → Windows installer (64-bit).
3. **devkitPro**: https://github.com/devkitPro/installer/releases → `devkitProUpdater-*.exe`.
   Keep the folder `C:\devkitPro` and tick **GBA Development** only.

Afterwards the AI runs §3 to confirm.
