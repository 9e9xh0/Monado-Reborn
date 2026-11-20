# Architecture decision records

One decision per file (`NNNN-short-name.md`). Format: `Status` / `Date` (and `Requirements` or `Tasks` where they
apply), then `Decision`, `Reason` and `Alternatives considered`. Changing an accepted decision means a new ADR that
supersedes it.

| ADR | Title | Status | Date |
|---|---|---|---|
| [0001](0001-base-lasrecomp.md) | Base: LASRecomp; ssbu-recomp only as a reference | Accepted | 2026-10-17 |
| [0002](0002-version-2-0-0.md) | Supported version: 2.0.0 | Accepted | 2026-10-17 |
| [0003](0003-lift-main-sdk-hle-movie.md) | Lift `main` and `sdk`; HLE of `movie::`; no `subsdk0` or `rtld` | Accepted | 2026-10-17 |
| [0004](0004-memory-layout.md) | LAS's memory layout | Accepted | 2026-10-17 |
| [0005](0005-vulkan-first.md) | Vulkan 1.3 first, Metal later | Accepted | 2026-10-17 |
| [0006](0006-common-runtime-game-profile.md) | Common runtime + `games/xcde/` + game profile | Accepted | 2026-10-17 |
| [0007](0007-clang-o1-atomics.md) | clang, `-O1` by default, atomics with `__atomic` | Accepted | 2026-10-17 |
| [0008](0008-audio-wwise.md) | Audio: lifted Wwise; `audren` / `audout` and `hwopus` | Accepted | 2026-10-17 |
| [0009](0009-video-ffmpeg.md) | Video: HLE of `movie::` with FFmpeg | Accepted | 2026-10-17 |
| [0010](0010-diagnostics-from-m1.md) | Diagnostics from M1 | Accepted | 2026-10-17 |
| [0011](0011-performance-quality-ui.md) | Conservative optimization and explicit quality | Implemented | 2026-11-04 |
| [0012](0012-native-fsr4-lite.md) | Optional native FSR 4.1.2 Lite INT8 (Linux) | Accepted | 2026-11-13 |
| [0013](0013-variant-seed-publication.md) | Publish the precompilation variant seed despite Article I | Proposed | 2026-11-20 |
