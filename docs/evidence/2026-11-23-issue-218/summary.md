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

## Not covered here

- **The game's own paths are still ANSI.** `runtime/platform/windows.c` (`plat_exe_path` → `GetModuleFileNameA`),
  `runtime/paths.c` (`getcwd`, `stat`, `chdir`, `fopen`) and the runtime's other file calls would still read the install
  folder in the system code page, so the game itself is expected to stop at its data-root check under a non-ASCII
  folder. Tracked as `T-UTF8-RUNTIME` in [`tasks.md`](../../dev/tasks.md); this issue is the launcher half.
- **Only CI on real Windows, not the reporter's machine.** The Windows CI job (`windows-components`, run 45093578036)
  built the launcher with the same zig + ucrt64 SDK and ran the new native test on windows-latest:
  `test_utf8_paths_build: PASS`, `test_utf8_paths: PASS` (the `CreateProcessA` 267 is printed there too, not asserted).
  A run of the shipped package under `C:\Users\João\…` remains to be done by the reporter or the maintainer.
