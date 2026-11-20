# Vulkan selector, DLSS and FPS counter

Local continuation of 2026-11-04. These changes need the game rebuilt with its 2.0.0 files; copying the DLLs alone
does not add the options to an old executable.

## Options

In **Graphics / GPU** of the launcher and **GPU / Vulkan** of the F1 menu:

- **Vulkan path:** Automatic, 1.2 (KHR), 1.3 and 1.4. Options incompatible with the selected GPU or the installed
  loader appear disabled. Automatic picks the highest supported path up to 1.4. If the machine or driver changes and
  a saved preference is no longer available, the fallback to a compatible path is logged. Requires a restart.
- The selector chooses the backend API path. The instance allows querying the driver capabilities; the active path
  is shown separately from the maximum version announced by the GPU. 1.4 uses the existing renderer and compatible
  SPIR-V; the option does not add effects exclusive to 1.4. The 1.2 path keeps spatial upscaling and disables DLSS
  and the temporal SDKs of this implementation.
- **Automatic upscaling:** default for new configurations. On the selected NVIDIA GPU, DLSS SR is enabled when the
  SDK confirms GPU, driver, feature and queue compatibility; otherwise FSR 1 is used. An earlier explicit
  bilinear/FSR/DLSS selection is respected. To enable this behavior in an existing configuration, choose
  **Automatic** and restart.
- **Manual DLSS:** disabled in the launcher if the selected GPU is not NVIDIA, the DLLs are missing or the 1.2 path
  is used. The definitive check is performed by `slIsFeatureSupported` on the active adapter. The menu shows the
  result/reason; detecting an NVIDIA by vendor is not enough to enable DLSS.
- **On-screen FPS counter:** enabled by default, visible inside the image even with F1 closed and in fullscreen. It
  shows native FPS and milliseconds per frame, updated every half second. The independent FPS checkbox in the title
  bar is kept. Repeated presents and frame generation do not increment the counter. Deterministic replays skip the
  overlay to preserve image comparisons.

New `xcde.ini` keys:

```ini
vulkan_api = auto
upscaler = auto
fps_overlay = 1
fps_overlay_style = detailed
```

`vulkan_api` also accepts `1.2`, `1.3`, `1.4`; `fps_overlay` accepts `0/1`; `fps_overlay_style` accepts `detailed`
(FPS, 1 % / 0.1 % lows, time graph, GPU time and waits, GPU / CPU / VRAM / RAM load and resolution; GitHub #39) or
`simple` (FPS and ms). It is changed live from F1. `fps_overlay_scale` (50–150, in %; 100 by default) and
`fps_overlay_position` (`top-left`, `top-right`, `bottom-left`, `bottom-right`) set its size and corner (GitHub
#147), also in F1 and in the launcher. Both interfaces keep these keys and those of other versions.

## Official NVIDIA runtime installed

Source: [Streamline SDK 2.14.16, NVIDIA-RTX release](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.16),
matching the project headers. The production x64 SDK was downloaded and its SHA-256 verified:

```text
32041e51db0cab709a271b72a352281b690c2abbd669a827809401a5dd23956b
```

Installed into `build/`: `sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`, `nvngx_dlss.dll` and
`NvLowLatencyVk.dll`. The first four DLLs gave an Authenticode signature **Valid**, with signer **NVIDIA
Corporation**. Streamline reports version 2.14.16.0; `nvngx_dlss.dll`, 310.9.16.0. The origin/hash manifest remains
in `build/dlss-runtime.json`; the original licences are kept in `sdk/sl/licenses/` and `build/dlss-licenses/`. The
binaries remain outside Git.

To reinstall the same official package:

```powershell
python tools/package/install_dlss.py
```

The installer verifies the file before deploying it, selects only the production `bin/x64` DLLs and rejects an
incomplete package, a duplicate or a wrong hash. The local packager keeps the DLLs, licences and manifest. The
Microsoft Visual C++ x64 dependencies imported by Streamline are present in System32 on this machine; a machine
without them receives the load error and the alternative upscaling.

## DLSS integration fixes

- The initial check uses the GPU chosen by UUID/index/env; an RTX present in another slot does not enable DLSS over
  a selected integrated GPU.
- The interposer signature/publisher is verified before loading it. Dependency search uses its own directory and
  System32. All SDK functions called afterwards are checked.
- If the SDK asks for zero additional queues, the graphics/compute indices use the real queue 0; previously indices
  1 were submitted even though the application had created only one queue.
- The Win32 surface creation hook is connected, in addition to the existing swapchain/present proxies. Limits and
  capabilities are checked before adding optional requirements.
- Zero-valued motion vectors are kept as valid; automatic exposure is enabled; resource tags use the evaluation
  lifetime; after the SDK the renderer command state is invalidated/restored. An orderly Streamline shutdown is
  added.
- Initialization, support or dispatch failures appear in the status and log, with alternative upscaling; DLSS is not
  reported active just because an RTX name is detected.

Implementation references: [official manual hooking guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.16/docs/ProgrammingGuideManualHooking.md),
[DLSS guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.16/docs/ProgrammingGuideDLSS.md).

## Verification and limits

- GPU-free tests were added for 1.2/1.3/1.4 selection, fallback, SL queue indices without extras and the counter at
  30/60/120/144/165 FPS. `test_display_options.exe` ran and passed.
- The INI round-trip tests and the official installer tests were extended. The C++ signature test compiles, but
  Windows blocked its execution with WinError 4551; the independent Authenticode check with PowerShell did run and
  was valid.
- The runtime modules and the launcher compile with `python tools/diag/validate_port.py --build-ui --compile-only`; this
  mode reports the native tests as **not run**, without counting them as passed. CI uses the normal mode to run them
  when the environment allows it.
- The available machine has an integrated AMD GPU. Running the rebuilt game on an RTX and checking the "DLSS Super
  Resolution available" status, the visual output at different resolutions and the HUD behavior during gameplay/FG
  is still pending. Real DLSS reconstruction and the whole game are not declared tested without that hardware and
  the game files.
