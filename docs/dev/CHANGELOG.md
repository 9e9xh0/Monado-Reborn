# Development changelog

New developer documentation is written in English. This file is the place for session prose that would otherwise be
appended to [`tasks.md`](tasks.md): what was done, what was measured and what is left, one section per work session.
Keep the ledger in `tasks.md` (task id + status) and the runtime edit log in [`../UPSTREAM.md`](../UPSTREAM.md)
(article X) separate from this narrative.

Older session notes live in the archived handoffs under [`archive/`](archive/) and in the historical session sections
of `tasks.md`; do not move them, they are the record.

## 2026-11-20 — documentation cleanup (Phase 1)

- Reorganized `docs/` into `adr/`, `contracts/`, `dev/`, `evidence/`, `guides/`, `history/`, `reference/` and
  `user/es/`; added the [`../README.md`](../README.md) index and the [`../adr/README.md`](../adr/README.md) index.
- English is canonical for developer documentation; the Spanish player guide is [`../user/es/README.md`](../user/es/README.md).
  The Spanish originals of `PERFORMANCE_UI`, `VULKAN_DLSS_FPS`, ADR 0011 and `UPSTREAM` are archived in
  [`../history/es/`](../history/es/).
- Archived the 2026-10-25 handoff (`archive/HANDOFF-2026-10-25.md`), the 2026-10-21 shader-pipeline handoff and the
  session 14 work log; the current status is in `tasks.md` and `../UPSTREAM.md`.
- Added `tools/check_docs.py` (documentation link/path checker), wired into `scripts/ci.sh` and CI.

## 2026-11-20 — code and tooling cleanup (Phase 2)

- Grouped `tools/` by purpose: `build/`, `gamebuild/`, `package/`, `lifter/tests/`, `tours/`, `diag/`, `media/`,
  `research/` and `setup/` (with a README in the groups that need one). The two repo-wide checkers stay at
  `tools/check_docs.py` and `tools/check_paths.py`.
- Deleted dead files: the tracked `*.bak` copies, the orphan swkbd HLE unit, the unused Vulkan 1.1 compatibility shim
  and its orphan test, the replaced menu stub, the uncompiled Metal sources, the superseded `make_dist`/`drive`
  scripts, the desktop-path image helpers and the legacy FSR 4 Lite preparation script (superseded by
  `tools/package/fsr4_prepare.py` + `tools/build/build_fsr4prep.py`). The exact list is in the commit message; the
  generated `docs/hle_audit.md` was regenerated.
- Pruned `third_party/imgui/` to the 17 files the overlay and launcher compile (removed examples, docs, misc, demo and
  unused backends: 225 files, ~3.8 MB) and added `third_party/README.md` with upstream, pin, licence and bump
  instructions. Added the missing FSR 4 Lite capture tooling row to `NOTICE.md`.
- Added `tools/check_paths.py` (repo path literals in scripts, workflows, Python tooling and `CMakeLists.txt`) with
  `tests/unit/test_check_paths.py`, wired into `scripts/ci.sh` and CI; added `docs/dev/repo-layout.md` (current tree);
  moved the GPU compatibility matrix to `docs/reference/gpu-compatibility-matrix.md`.
- Deferred: `tools/lifter/tests/test_simd.py` is still broken (it references a `test_simd_harness.c` that was never
  committed) and neither it nor `test_lift.py` can run here (no Unicorn/Capstone); Windows-only build paths were not
  executed locally.
