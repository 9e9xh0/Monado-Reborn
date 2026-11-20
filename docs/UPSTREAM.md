# Local changes to the inherited runtime

LASRecomp attribution is kept in LICENSE/NOTICE. This continuation does not incorporate code from GPL projects or
game data.

## 2026-11-04 — T247–T255

- T247: `runtime/host/settings_ini.c`, `runtime/hle/bindings.c`; shared writer in `runtime/host/ini_writer.h` and `atomic_file.h`.
- T248: `runtime/settings.h`, `runtime/nvn/vulkan/device.c`, `runtime/ui/menu_vk.cpp`; policy/probe/selector shared with the launcher.
- T249: `runtime/hle/framepace.c`, `runtime/hle/party.c`, `runtime/frame_clock.h`; millisecond paths in `runtime/diag/diag.c`/`.h`.
- T250: `runtime/nvn/vulkan/vk.h`, `dispatch.h`, `device.c`, `imgui_glue.cpp`, `shaders.c`, `dlss.cpp`, `temporal.c`, `temporal_fg.inc`.
- T251: `runtime/nvn/output_scale.c`, `runtime/nvn/vulkan/exec.c`, `precompile.c`, `runtime/resource_policy.h`.
- T252–T254: `runtime/nvn/vulkan/exec.c`, `device.c`, `textures.c`, `shaders.c`, `runtime/cache_validation.h`, `staging_copy.h`; file diagnostics in `runtime/services/fs.c`.
- T255: `runtime/nvn/objects.c`, `nvn.h`, `runtime/nvn/vulkan/exec.c`, `runtime/window_reuse.h`: per-window-image reuse wait.

Tests and limits: `docs/history/port/PORT_VALIDATION.md`. The game-timer-specific changes keep the existing hooks and
patches of `games/xcde`; no addresses were invented and no game code was generated.

## T256–T258 — Vulkan paths, DLSS and FPS HUD

- `runtime/settings.h`, `host/settings_ini.c`, `host/settings_boot.c`, `gpu_policy.h`, `vulkan_policy.h`, `nvn/vulkan/gpu_probe.h`, `device.c`, `vk.h`: API path preference, capabilities and automatic upscaler.
- `runtime/nvn/vulkan/dlss.cpp`, `exec.c`, `temporal.c`, `runtime/dlss_policy.h`, `host/nvidia_signature.h`: official NVIDIA runtime, per-GPU/SDK gating, queues, surface hook, resources/state and signature verification.
- `runtime/ui/menu_vk.cpp`, `menu.h`, `gpu_picker.h`, `runtime/fps_meter.h`: on-screen counter and options. (the earlier host menu stub was removed in the 2026-11 cleanup; `runtime/ui/menu_vk.cpp` owns the overlay.) The launcher shares the options and preserves the new keys.

Details and tests in `docs/VULKAN_DLSS_FPS.md`. The NVIDIA binaries live only in `sdk/` and `build/`, excluded from
version control.

## T259–T261 — conservative performance, profiles and interface

- `runtime/settings.h`, `quality_policy.h`, `host/settings_ini.c`, `services/romfs_virtual.c`: profiles, compatible keys, C/C++ bindings and optional in-memory effects.
- `runtime/nvn/output_scale.c`, `nvn/vulkan/exec.c`, `device.c`, `textures.c`, `vk.h`: presentation budget, asynchronous GPU time, descriptor cache, index scratch, texture index and GPU properties.
- New helpers `render_policy.h`, `descriptor_cache.h`, `native_intervals.h`, `services/romfs_fd_cache.h`, `nvn/vulkan/texture_index.h`; they share logic with the tests.
- `runtime/ui/menu_vk.cpp`, new UI helpers, `tools/launcher/launcher.cpp`: theme, navigation and shared configuration. `scripts/native_tests.sh` and `tools/diag/validate_port.py`: checks without game data.

Justification in ADR 0011. Attribution is kept and no code from other projects is incorporated. No change has been
sent upstream nor has a distribution been published.

## T271 — HUD size (GitHub #121)

- `runtime/nvn/vulkan/exec.c`: the 16:9 box classification also runs in 16:9 with the HUD scaled; per-draw anchoring and scaling folded into the viewport mapping. `runtime/settings.h`, `host/settings_ini.c`, `ui/menu_vk.cpp`, `tools/launcher/launcher.cpp`: `ui_scale` setting. New helper `runtime/ui_scale.h` (shared with `tests/native/test_ui_scale.c`). T271b: only the edge HUD is scaled (popups, menus, dialogs and markers stay as the game draws them), stable groups across frames (hysteresis anchors) and `ui_backdrop_view` in `exec.c` (shifted copy of the window texture for the scaled HUD draws that read the scene).

## Vertex and effect corruption with async on Windows (2026-11-10)

Two distinct failures of the Windows asynchronous renderer produced the same class of symptom: geometry and effects
drawn with another frame's vertex data. Both are inert on Linux, where the vertex cache and frame lag are not active
(RADV uses the dma-buf GTT path and `pt_init()` returns false without `MEM_WRITE_WATCH`).

- **T-VCACHE-ASYNC:** `vcache_lookup` (`nvn/vulkan/exec.c`) compared eviction eligibility against
  `vk_pending_seq()`. With the asynchronous worker, frame N's entries looked evictable before their sequence had been
  submitted, and their device-local buffers could be reused or freed while the worker's command buffer still
  referenced them: wrong geometry/textures and graphical corruption. Now, with `backend_async()`,
  `vk_completed_seq()` is used; the synchronous path does not change.
- **T-FRAME-LAG-VCACHE:** with async + vertex cache + more than 60 FPS, `vc_flush` recorded the refresh as a GPU-time
  copy from guest memory; the copy could run after the game had rewritten those pages, so the effect/particle streams
  drew another frame's contents (stretched large quads). Now, while frame lag is active, the pages are copied to the
  transient ring at submit time; `XCDE_VC_STAGE=0` restores the previous behavior for an A/B. Host reads that frame
  lag still performs one frame late (SSBOs and the compute path's `gfast` copy) are not covered.

Details, symptoms, diagnostics and limits:
[summary.md](evidence/2026-11-10-vertex-corruption/summary.md).
