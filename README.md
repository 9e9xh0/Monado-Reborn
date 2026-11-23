# Monado-Reborn

<a href="https://recomplauncher.com/game/monado-fix-portable"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

A static recompilation of **Xenoblade Chronicles: Definitive Edition** (Nintendo Switch, 2020) into a native
application with a Vulkan renderer.

Monado is **not an emulator**: the game's ARM64 code is translated ahead of time (AOT) into C and compiled for
your PC, while the console's operating system and graphics API (NVN) are reimplemented on top of the host OS and
Vulkan. Windows x86-64 is the primary target; Linux builds are supported too.

> **Requirements.** You need your own legally obtained copy of the game, version 2.0.0 (the base game with the
> 2.0.0 update applied), extracted into plain `exefs/` and `romfs/` folders. No game files, updates, keys or
> packaged formats (`.nsp`, `.xci`, `.nca`) are included or downloaded by this project — see
> [`CONTRIBUTING.md`](CONTRIBUTING.md) (article I).

## Quick start

1. Put your extracted `exefs` and `romfs` folders inside `targets/`, so that `targets/exefs` and `targets/romfs`
   exist. The folder names are lowercase on every system.
2. **Portable build:** run `Play Monado.bat`. **From source:** run `Build Monado.bat` (Windows; it installs the
   toolchain, a few GB) or follow [`docs/guides/BUILD-CMAKE.md`](docs/guides/BUILD-CMAKE.md) /
   [`docs/guides/LINUX.md`](docs/guides/LINUX.md).
3. Start the game with `Play Monado.bat`: it opens the launcher (graphics, scaling, controls, language) and then
   the game. The first start precompiles shaders and can take a while; later starts use the cache.
4. From another launcher or a shortcut: `build/xcde.exe --game <folder with exefs/ and romfs/>`. The older
   `build/xcde.exe <folder>` form still works. On Linux: `Monado-x86_64.AppImage --game <folder>`.

**With [RecompLauncher](https://recomplauncher.com/game/monado-fix-portable)** (Windows and Linux): install Monado
there, pick the folder that holds your `exefs` and `romfs`, and press Play. It builds the game code once from your
copy and shows the video, upscaling, frame-rate and language settings on the game's page (`recomplauncher.json`).

Extra launchers: `Play Monado (performance metrics).bat` and `Play Monado (stutter diagnostics).bat` are
diagnostic variants; `Crear Monado Portable.bat` builds a movable folder for another PC / USB disk (for your own
use only), and `Limpiar Monado.bat` deletes this PC's shader cache, crash reports and logs before you move it.

## What this fork adds

Fixes and improvements on top of the original Monado project (details in the
[Releases](https://github.com/Ivanprestef2/Monado-Reborn/releases) page):

- **Stability and performance fixes** for heavy shaders, frame drops and crashes, with AMD/Intel GPU refinements.
- **Streamlined setup:** self-installing build script, a portable folder and dedicated diagnostics launchers.
- **Quality-of-life** improvements ported from Monado II (cheats, save-slot and menu extras) and a performance UI
  with dynamic resolution and low-end profiles.

## Documentation

| Start here | |
|---|---|
| [`docs/README.md`](docs/README.md) | Index of all documentation (guides, ADRs, evidence, dev record) |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Project rules ("the constitution"), build and test loop |
| [`docs/dev/tasks.md`](docs/dev/tasks.md) | Task ledger: what is done and what is open |
| [`docs/UPSTREAM.md`](docs/UPSTREAM.md) | Every edit made to the LASRecomp-derived runtime (article X) |
| [`docs/adr/`](docs/adr/) | Architecture decisions |
| [`NOTICE.md`](NOTICE.md) | Licences, attribution and third-party components |

## Credits

- **Original project:** Monado — TheMonadoGuy, continued by [vinitaison](https://github.com/vinitaison/Monado).
- **Recompilation framework:** [LASRecomp](https://github.com/peiikai/LASRecomp) by Peiikai (MIT) — the AArch64→C
  lifter and the runtime this project derives from.
- **Monado II** ([brendonbusker](https://github.com/brendonbusker/Monado-II), MIT) — cheats and menu / save mods.
- **Development of this fork:** [Ivanprestef2](https://github.com/Ivanprestef2), [Marval53](https://github.com/Marval53),
  [Cha1N1](https://github.com/Cha1N1), [9e9xh0](https://github.com/9e9xh0), with AI assistance where noted in the
  repository history.

## Español

Guía del usuario en español: [`docs/user/es/README.md`](docs/user/es/README.md).

---

Licensed under the MIT licence ([`LICENSE`](LICENSE)); third-party notices in [`NOTICE.md`](NOTICE.md).
