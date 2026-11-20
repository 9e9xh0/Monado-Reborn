# third_party: vendored third-party code

Everything here is committed so builds work offline and are reproducible (no configure-time downloads, no git
submodules). Each component is pruned to the files this repository compiles or ships plus its licence; see the
individual READMEs and `NOTICE.md` for attribution.

| Component | Upstream / pin | Licence | What Monado uses |
|---|---|---|---|
| `imgui/` | [ocornut/imgui](https://github.com/ocornut/imgui) v1.91.9 | MIT (`imgui/LICENSE.txt`) | The four core `.cpp`, the SDL3, SDL-renderer3 and Vulkan backends and the headers they need. Used by the in-game settings overlay (`runtime/ui/menu_vk.cpp`) and the launcher (`tools/launcher/launcher.cpp`). The upstream `examples/`, `docs/`, `misc/`, demo and the unused backends are deliberately not vendored. |
| `smaa/` | [iryoku/smaa](https://github.com/iryoku/smaa) reference distribution | MIT-style (`smaa/LICENSE.txt`) | `SMAA.hlsl` (turned into a C string by `tools/build/gen_smaa_inc.py`) and the precomputed `Textures/AreaTex.h` / `Textures/SearchTex.h` lookup tables, compiled into `runtime/nvn/smaa_data.c`. |
| `streamline/` | NVIDIA Streamline **2.14.16** headers (`include/sl_version.h`; downloaded by `tools/package/install_dlss.py`) | MIT (`streamline/license.txt`) | `runtime/nvn/vulkan/dlss.cpp` includes `sl.h`, `sl_consts.h`, `sl_dlss.h`, `sl_hooks.h` and `sl_helpers_vk.h` plus their transitive headers. |
| `ffx_api/` | AMD FidelityFX API headers from FidelityFX SDK **1.1.4** | MIT | `runtime/nvn/vulkan/temporal.c` (FSR 3 / frame generation interface), `fsr3_native/adapter.cpp` and `tests/native/test_fsr3_temporal.cpp`. |
| `fsr3_native/` | AMD FSR **3.1.4** adapter, SDK 1.1.4 pinned at `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55` (the SDK itself is fetched at build time) | MIT adapter + AMD SDK terms (`fsr3_native/LICENSE-AMD.txt`) | `adapter.cpp` / `compat.h`, compiled into `build/linux/libmonado_fsr3.so` by `scripts/build_fsr3_linux.sh`. |
| `fsr4_native/` | [bbport](https://github.com/deadinside28/bloodborne_pc) FSR 4.1.2 Lite at `0fc2921c45f8ff3900f6bfa43a299e9a4f532af4`, locally adapted | GPL-2.0-or-later (`fsr4_native/LICENSE`, `LICENSE-AMD-SDK.md`) | The native FSR 4 Lite component (`monado_fsr4.dll` / `libmonado_fsr4.so`). |
| `fsr4cap/` | bbport `tools/fsr4cap` at `0fc2921c45f8ff3900f6bfa43a299e9a4f532af4`, unmodified | GPL-2.0-or-later (`fsr4cap/LICENSE`) | Recording/translating AMD's FSR 4.1.2 model files on the player's PC: built and shipped by `tools/build/build_fsr4prep.py`, run through `tools/package/fsr4_prepare.py`. |
| `bcdec/` | bcdec v0.985 ([iOrange/bcdec](https://github.com/iOrange/bcdec)) | MIT or Unlicense | BC1–BC7 block decoding in `runtime/services/kbicons.c`. |
| `weeping-of-the-empire/` | [daniel-madrid-07/weeping-of-the-empire](https://github.com/daniel-madrid-07/weeping-of-the-empire) | MIT (`weeping-of-the-empire/LICENSE`) | Licence only: the Linux AMDGPU guest-heap and ordered Vulkan queue designs adapted in `runtime/platform/linux_gpu.h` and `runtime/nvn/vulkan/vkq.c`. |

## Pruning and bumping

- Vendored trees contain only the files that are compiled/shipped plus the licence. Do not add upstream examples,
  demos, documentation or unused backends: they are large and never built.
- To bump a component: copy/replace the used files from the pinned upstream release, update this table (and
  `NOTICE.md` if the licence or pin changed), then rebuild on Windows and Linux and run `scripts/ci.sh`.
- `tools/package/package_guard.py` and the packagers only ship licence notices from these trees; anything derived
  from AMD's or NVIDIA's SDKs is downloaded on the build machine or the player's PC, never committed.
