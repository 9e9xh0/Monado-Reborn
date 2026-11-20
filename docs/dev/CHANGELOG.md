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
