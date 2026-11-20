# Repository layout

Where things live **now** (2026-11 cleanup). This file is maintained; `docs/dev/plan.md` is the frozen phase-0 plan and
describes the project at its start. The documentation index is [`docs/README.md`](../README.md).

## Top level

| Path | What it holds |
|---|---|
| `runtime/` | The portable C runtime linked into the game executable: guest loader/dispatch/kernel, Horizon services and HLE, the NVN→Vulkan renderer, diagnostics and platform layers. |
| `tools/` | Build and release tooling, the AArch64→C lifter, the launcher and ad-hoc analysis tools, grouped by purpose (below). |
| `tests/` | Automated tests and fixtures: `unit/` (no game data; the only suite CI runs), `native/` (C/C++ runtime tests), `routes/` (recorded input) and `tours/` (debugger tours). |
| `scripts/` | Local build, run, soak and measurement scripts (mostly Git Bash on Windows); CMake is the canonical build path for releases. |
| `third_party/` | Vendored, pinned third-party code pruned to what is built (see [`third_party/README.md`](../../third_party/README.md)). |
| `docs/` | All documentation; start at [`docs/README.md`](../README.md). |
| `games/xcde/` | The Xenoblade Chronicles: Definitive Edition game profile: build ids, patches, HLE classes, compile costs. |
| `data/` | Committed data without game content: the pipeline variant seed and the PGO profile. |
| `cmake/` | CMake helpers (zig toolchain file, upscaler DLL copy script). |
| `packaging/linux/` | AppImage glue: `AppRun`, desktop entry and icon. |
| `.github/workflows/` | `ci.yml` (unit tests, guards, Windows component build) and `release.yml` (release packages via CMake). |
| root `*.bat` | Windows double-click entry points for players: building, playing, diagnostics, portable packaging, cache cleanup. |
| `CMakeLists.txt`, `CMakePresets.json` | Canonical build for releases and for an incremental local build. |

## runtime/

| Path | Contents |
|---|---|
| `runtime/*.c`, `runtime/*.h` | Core: `loader.c` (module load), `dispatch.c` (guest address → function), `kernel.c` (SVCs, threads, memory), `ipc.c`, `fnprof.c`, `paths.c`, plus shared headers (`cpu.h`, `simd.h`, `settings.h`, policy headers). |
| `runtime/hle/` | High-level emulation: NVN boot, `movie::` (FFmpeg), `pl`, `vi`, heaps, input, party/tours, framepace, foliage, graphics options, cheats, quick-step UI, save expansion. |
| `runtime/nvn/` | NVN and Maxwell: command buffer, objects, formats, ASTC/BCn decode, texture patching, SMAA data, `nvn/vulkan/` (device, executor, shaders, precompile, temporal/FSR, VMA) and `nvn/maxwell/` (instruction translation). |
| `runtime/services/` | Horizon IPC services: `fs`, `romfs_virtual`, `am`, `acc`, `time`, `hid_set`, `kbicons`, `misc`, `audio`, `hwopus`. |
| `runtime/host/` | Process entry, settings INI, stubs and the static game library glue. |
| `runtime/ui/` | In-game overlay (ImGui/Vulkan), menus, HUD scaling helpers and theme. |
| `runtime/diag/` | Crash dumps, watchdog, sampler, diagnostics sink. |
| `runtime/platform/` | `windows.c`, `linux.c` and their compatibility headers. |

Headers are kept flat in `runtime/` and `runtime/nvn/` on purpose: the build uses `-Iruntime -Iruntime/nvn` and the
includes are by bare header name. Do not move headers without a build-wide rename.

## tools/

| Directory | Files | Purpose |
|---|---|---|
| `tools/build/` | `gen_all.py`, `gen_smaa_inc.py`, `pgo.py`, `game_id.py`, `build_unicorn.py`, `build_fsr3_linux.py`, `build_fsr4prep.py` | Code generation, PGO, the generator fingerprint, and building external components (patched unicorn, FSR 3 / FSR 4 Lite preparation). |
| `tools/gamebuild/` | `gamebuild.py`, `gamebuild_entry.py`, `gamebuild_stage.py`, `freeze_gamebuild.py`, `coff_exports.py`, `pe_implib.py`, `gamelib_entry.c` | The first-start game code builder pipeline (see its `README.md`). |
| `tools/package/` | `make_appimage.py`, `make_portable.py`, `package_guard.py`, `install_dlss.py`, `fsr4_prepare.py` | Release packaging, vendor DLL installation and the on-PC FSR 4 Lite preparation tool. |
| `tools/lifter/` | `gen.py`, `lifter.py`, `simd_c.py`, `analyze.py`, `nso.py`, `sym.py`, `flags.py`, `regs.py`, `patchfmt.py` | The AArch64→C generator (adapted from LASRecomp). `tools/lifter/tests/` has the differential tests (`test_lift.py`, `test_simd.py`, `test_harness.c`); they need Unicorn/Capstone and are not run in CI. |
| `tools/extract/` | `check_dump.py`, `manifest.py` | Validate the user's dump and build the expected-file manifest. |
| `tools/launcher/` | `launcher.cpp`, `update_check.h`, `build.sh`, `build_linux.sh` | The SDL3 + Dear ImGui launcher and its direct build scripts. |
| `tools/tours/` | `tour_gen.py`, `battle_tour_gen.py`, `boss_tour_gen.py`, `tour_batch.py`, `gen_soak_route.py`, `timed_route.py`, `bdat1.py` | Tour/route generation and batch execution (see its `README.md`). |
| `tools/diag/` | `performance_report.py`, `soak.py`, `soak_report.py`, `benchmark_native.py`, `hash_diff.py`, `hle_audit.py`, `report.py`, `validate_port.py`, `setup_validation.py`, `rt_summary.py`, `gpu_diagnostics.c`, `vulkan_smoke.h`, `collect_gpu_diagnostics.bat`, `exe_sym.py` | Performance and correctness measurement, the generated HLE audit, and the Windows component/validation build used by CI. |
| `tools/media/` | `ref_compare.py`, `contact_sheet.py`, `shot_sheet.py`, `video_flicker.py`, `flicker_scan.py`, `flicker_view.py`, `wav_report.py`, `wav_compare.py` | Compare captures and audio against the reference recording; find rendering glitches. |
| `tools/research/` | `analyze_nso.py`, `classify_br.py`, `spch_attrs.py`, `glsl_dataflow.py`, `merge_variants.py` | One-off reversing/research tools whose outputs feed `docs/reference/` and `docs/evidence/` (see its `README.md`). |
| `tools/setup/` | `setup_windows.ps1`, `setup_game_files.ps1` | One-time Windows build-tool setup and game-file picker (called by the root `.bat` files). |
| `tools/unicorn/` | two `.patch` files | Patches applied when building the unicorn library shipped in release packages. |
| `tools/check_docs.py`, `tools/check_paths.py` | — | Repo-wide documentation and path-literal checks, run by `scripts/ci.sh` and CI. |

## tests/

- `tests/unit/`: Python and small C/C++ tests, no game data — discovered by
  `python -m unittest discover -s tests/unit` and run by CI on Linux and Windows.
- `tests/native/`: C/C++ tests of runtime units, compiled by `tools/diag/validate_port.py` (CI Windows job) or
  `scripts/native_tests.sh`. Some older tests are still only compiled by the shell script.
- `tests/routes/` and `tests/tours/`: input fixtures (routes are recorded controller input, tours are debugger scripts);
  they need a dump, so they are used locally through `scripts/drive_env.sh` and `tools/tours/tour_batch.py`.
- The GPU compatibility matrix is documentation now: [`docs/reference/gpu-compatibility-matrix.md`](../reference/gpu-compatibility-matrix.md).

## scripts/

Windows/Git-Bash helpers around the local build (`build_all.sh`, `build_boot.sh`, `build_rt_gfx.sh`, `link_gfx.sh`,
`build_fsr4_windows.sh`), Linux helpers (`build_linux.sh`, `build_fsr3_linux.sh`, `build_fsr4_linux.sh`,
`build_ffmpeg.sh`), run/measure helpers (`drive_env.sh`, `rt_run.sh`, `soak.sh`, `pgo_train.sh`, `play.sh`,
`repro_stability.sh`), `copy_upscaler_dlls.sh`, worktree setup (`wt_setup.sh`) and `ci.sh` (the local equivalent of
CI). `scripts/link_boot.sh` builds the development boot executable, not the game.

## third_party/, data/, games/, cmake/, packaging/

- `third_party/`: pinned and pruned; README in that directory states upstream, pin, licence and what is built.
- `data/`: `variants_seed.bin` (pipeline variants recorded in play, read by the precompiler and `exec.c`) and
  `data/pgo/` (committed profile; see [`docs/guides/PGO.md`](../guides/PGO.md)).
- `games/xcde/`: game profile, `patches.py` (with expected bytes), HLE classification, compile costs.
- `cmake/`: `zig.cmake` (toolchain file for the Windows preset and `linux-zig`) and `copy_upscaler_dlls.cmake`.
- `packaging/linux/`: AppImage files; the packagers are `tools/package/make_appimage.py` and `make_portable.py`.

## Where does a new file go?

| I am adding… | Put it in |
|---|---|
| A runtime unit | `runtime/` (or its subsystem); add it to `RT_UNITS`/`NVN_UNITS` in `CMakeLists.txt` and the matching `scripts/` lists. |
| A Python tool used by the build or release | The matching `tools/` group; update `CMakeLists.txt`, the workflows and scripts that call it. |
| A unit test (no game data) | `tests/unit/`; it runs in CI automatically. |
| A native runtime test | `tests/native/`; wire it into `tools/diag/validate_port.py` (CI) or `scripts/native_tests.sh`. |
| A guide or record | `docs/guides/` for maintained guides, `docs/history/` for archival material, `docs/dev/` for the development record. |
| A vendored dependency | `third_party/<name>/` pruned to the used files + licence, and a row in `NOTICE.md`. |

`scripts/ci.sh` runs the unit tests, the packaging guard, `tools/check_docs.py` and `tools/check_paths.py`. A CMake
runtime-only configure (`cmake -S . -B build/cmake/check -DMONADO_RUNTIME_ONLY=ON`) is the quick build-side smoke test.
