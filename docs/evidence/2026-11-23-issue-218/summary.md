# Issue #218 — launcher fails with error 267 under a non-ASCII install path (2026-11-23)

Report: [Ivanprestef2/Monado-Reborn#218](https://github.com/Ivanprestef2/Monado-Reborn/issues/218) (yRevooh,
João Santos, 2026-11-23). Windows 10/11, package under `C:\Users\João Santos\…`:
`Could not start the game code build (error 267)` and `Could not check for updates: curl.exe could not be started
(error 267)`. Moving the install to an ASCII folder is the workaround the report gives.

Root cause (as the report says): the launcher holds its paths in UTF-8 (`SDL_GetBasePath`) and hands them to the ANSI
Win32 APIs and the CRT. Windows reads those bytes in the system code page, so `João` (`C3 A3` in UTF-8) becomes
`JoÃ£o` (CP1252) — a folder that does not exist. `CreateProcessA`'s `lpCurrentDirectory`, `CreateFileA`, `MoveFileExA`,
`SetEnvironmentVariableA` and `fopen`/`stat` all fail on it.

## Fix

- New `runtime/host/utf8_path.h`: UTF-8 → UTF-16 conversion plus `_wfopen` / `_wremove` / `_wrename` / `MoveFileExW`
  wrappers (plain libc on other systems).
- `runtime/host/atomic_file.h`, `runtime/host/ini_writer.h`: the shared INI writer and the atomic replacement save
  through those wrappers.
- `tools/launcher/launcher.cpp`: all six child processes start with `CreateProcessW`, with the executable, the command
  line and the working folder converted (`W()`/`WPath()`); logs and progress files use the wrappers; `PATH` and the
  Diagnostics environment are read/written as UTF-16; mod and report listings use the wide `Find*File`; folders open
  with `ShellExecuteW`; `curl` writes to the relative `build/update_check.json` so a child that decodes argv in the
  system code page cannot mangle the path; `stat` became `SDL_GetPathInfo`.

## Reproduction and verification (Linux, Wine 11)

Machine: Linux, CachyOS kernel, NVIDIA GTX 1050 Ti. Windows layer: Proton-CachyOS 11.0's Wine (`wine-11.0
(CachyOS)`), prefix `WINEARCH=win64`, system code page **1252**. Real Windows launcher built with the project's zig +
`tools/diag/setup_validation.py` SDK (ucrt64 SDL3/Vulkan), exactly as the Windows CI does. Install folder:
`Z:\home\ilya\.cache\wine-utf8\João-😀\…` (`João`: U+00E3; the emoji is a UTF-16 surrogate pair).

1. `tests/native/test_utf8_paths.c` (also wired into `scripts/native_tests_linux.sh`, `scripts/native_tests.sh` and
   `tools/diag/validate_port.py`), run from the `João-😀` folder:

   ```
   fopen() with the UTF-8 path: failed (system code page 1252)
   CreateProcessA with the UTF-8 folder: failed (267, system code page 1252)
   utf8_paths: all passed
   ```

   The A call is the reporter's `ERROR_DIRECTORY`; the wrappers and the `CreateProcessW` call open, rename, replace and
   remove the file, round-trip the shared INI writer (`xcde_ini_begin`/`xcde_ini_commit`, the launcher's `xcde.ini` and
   the game's settings) and start `cmd.exe` in that folder.

2. Real launcher, `--shot` (renders 5 frames and quits), install `João-😀\Monado`, fixed build vs the pre-fix
   `launcher.cpp` from `HEAD`:

   | build | `build/launcher.log` (opened through `fopen`) |
   |---|---|
   | pre-fix | absent (the UTF-8 path was read as CP1252) |
   | fixed | `launcher start, root Z:\home\ilya\.cache\wine-utf8\João-😀\Monado`, `renderer opengl`, window size |

3. Update check (`MONADO_VERSION=14.0.0`, a stand-in `C:\windows\system32\curl.exe` that records its argv and working
   folder), install `João-😀\Upd-<build>`:

   | build | child started | argv passed |
   |---|---|---|
   | pre-fix | no (`CreateProcessA` 267) | — |
   | fixed | yes, working folder `Z:\…\João-😀\Upd-new` | `-o build/update_check.json` (no non-ASCII bytes) |

4. Game code build (a stand-in `build/gamebuild/monado-gamebuild.exe` that records its argv and working folder, plus
   the link.json / gamelib-id files the launcher expects), install `João-😀\Gc-<build>` — the report's first symptom:

   | build | the stand-in's recording | `build/last_gamebuild.log` |
   |---|---|---|
   | pre-fix | absent ("could not start the game code build (error 267)") | absent |
   | fixed | written, working folder `Z:\…\João-😀\Gc-new` | written |

The real `monado-gamebuild.exe` is a PyInstaller one-dir package: its bootloader and CPython read the wide command
line, so the absolute `--root/--out/--resources` paths arrive correctly. The stand-in above has a narrow `argv`, which
is why its recorded arguments show CP1252 loss (`Jo�o-??`): that is the stand-in's own decoding, not the launcher's
command line (`curl`'s output path was made relative for exactly this reason).

# Second half - the game itself (T278)

## Test bed

The launcher half (#220) made the packaged `monado-gamebuild.exe` start under `João-😀`, but the bundled Zig 0.17.1
cannot create its cache under this Wine (`checking cache failed: manifest_create Unexpected`, also with an ASCII
one-file test), so the game code was built **on Linux** instead, and the Windows runtime **cross-built** from this tree -
the two routes below give a real Windows game that runs under Wine without waiting for a Windows machine.

- Runtime: `cmake -B build/cmake/win -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/zig.cmake
  -DZIG_EXECUTABLE=<zig> -DZIG_TARGET=x86_64-windows-gnu -DMONADO_RELEASE=ON -DMSYS2_PREFIX=$PWD/build/deps/ucrt64`;
  the MSYS2 UCRT64 prefix holds SDL3, Vulkan, shaderc, zlib, winpthreads (`tools/diag/setup_validation.py`) plus the
  `opus` and `ffmpeg 9.0-2` packages. The link writes `build/xcde.exe` (3.1 MB, 808 exports) and
  `build/gamebuild/xcde_imports.a`.
- Game code: `tools/gamebuild/gamebuild_stage.py --dest build/gamebuild --platform windows --zig <linux zig>
  --link-file xcde_imports.a` stages the resources (its probe link works on Linux), then
  `tools/gamebuild/gamebuild.py --root targets --out build/game-lib --resources build/gamebuild --jobs 8` builds
  `lasgame.dll` (61 MB, 8m40s, same `GAMELIB_ID`). `build/xcde.exe`, the release package's DLLs, `gamebuild/lasgame.dll`
  and a hard-linked copy of `targets/` make the install at `Z:\…\João-😀\Built`.

## The failure found

With the real layout (relative `targets/` in the João folder) the game stopped at
`[fs] no romfs folder at targets/romfs` -> `FATAL: cannot open targets/romfs.bin`, although `loaded main`/`loaded sdk`
and `gamebuild/lasgame.dll` worked. Root cause: mingw's `opendir()` makes the path absolute with
`_tfullpath`/`GetFullPathNameA`; in a non-ASCII working folder the search runs in "JoÃ£o" and returns no entries.

| call in `Z:\…\João-😀\Built` | result |
|---|---|
| `opendir("targets/romfs")` + `readdir` (CRT) | handle, **0 entries** (also for a 2-file dir) |
| the same dir from an ASCII folder | 5 entries |
| the same dir via an absolute ASCII path from the João folder | 5 entries |
| `FindFirstFileW(L"targets\\romfs\\*")` in the João folder | works (bdat, chr, config, ...) |
| `GetFullPathNameA("targets\\romfs")` | `…\JoM-co-??\Built\targets\romfs` |

## Fix and verification

The fix (T278): `runtime/platform/dir_utf8.h` enumerates with `FindFirstFileW`, and `runtime/host/utf8_path.h` gained
`open`/`stat`/`mkdir`/`rmdir`/`chdir`/`getcwd`/`getenv`/`LoadLibraryW` wrappers used by the loader, romfs, gamelib,
settings, FSR 4, quickstep saves and the path-valued env vars; the T135 relaunch and the minidump helper use
`CreateProcessW`; `plat_argv_utf8` parses the wide command line; `lasrecomp.log` uses `_wfreopen`. Rebuilding only
`xcde.exe` (the game library is untouched: no ABI/id change) and rerunning in the same folder:

| route | result |
|---|---|
| `build\xcde.exe targets` (relative, João cwd) | `virtual romfs over targets/romfs: image 14609810396 bytes`, renders (first run 5,838 frames; after the last change 2,055 frames at 30 fps) |
| `XCDE_TEST_RESERVE_FAIL=1` (forces the T135 collision once) | `[boot] ... is not free (try 1): starting again`, then the child loads the game and renders |
| `XCDE_TEST_CRASH_AT=150` | `crash/crash_20261123_052824.txt` + `crash_20261123_052824.dmp` (296 KB), `minidump ... (written, helper exit 0)` |
| detached launch, no standard handles (`STARTF_USESTDHANDLES` with NULL) | `build/lasrecomp.log` in the João folder (148 KB, timestamped) |
| `--game Z:\…\João-😀\Built\targets` from `/tmp` | virtual romfs over the absolute path, renders (3,463 frames) |
| `tests/native/test_utf8_paths.c` under Wine | `opendir/readdir in the non-ASCII folder: CRT 0 entries, wide found file.txt`; all checks pass |

`--game`/the minidump/log routes above are the recorded route for `T-UTF8-RUNTIME`; the Wine bed is outside the
repository (`build/` and `~/.cache/wine-utf8/…` are private).

## Not covered here

- **Leftovers off the play path** (`T278b` in [`tasks.md`](../../dev/tasks.md)): `services/kbicons.c` opens its cache/
  mod icon files with the narrow CRT, and `platform/win_compat.h`'s `realpath` shim (`_fullpath`) is unused by the game.
- **Only Wine here, no Windows machine.** The fix compiles for the Windows target with the same SDK as CI; the
  windows-latest job runs the native test. A run of the shipped package under `C:\Users\João\…` (launcher builds the
  game code there and the game plays) is still worth doing with the reporter.
- **Only CI on real Windows, not the reporter's machine.** The Windows CI job (`windows-components`, run 45093578036)
  built the launcher with the same zig + ucrt64 SDK and ran the new native test on windows-latest:
  `test_utf8_paths_build: PASS`, `test_utf8_paths: PASS` (the `CreateProcessA` 267 is printed there too, not asserted).
  A run of the shipped package under `C:\Users\João\…` remains to be done by the reporter or the maintainer.
