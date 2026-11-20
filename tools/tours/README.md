# tools/tours: tour and route tooling

The runtime can play scripted debugger tours (`XCDE_TOUR=<file>`, see `runtime/hle/party.c`) and recorded input
routes (`XCDE_INPUT_REPLAY=<file>`, see `tests/routes/README.md`). These tools generate, run and convert them. None of
them runs in CI: they need the game's dump (`targets/`) or a built executable.

## Generators and runner

| File | What it does |
|---|---|
| `tour_gen.py` | **Multi-mode generator**: `maps`, `full`, `sev` and `events` tours from the game's own BDAT tables. This is the general generator. |
| `battle_tour_gen.py` | **A separate generator**, not a `tour_gen` mode: one fight per enemy species for `tests/tours/battle_types.txt`. |
| `boss_tour_gen.py` | **A separate generator** for story-gated spawns (`tests/tours/battle_story.txt`); it reuses the table-reading prologue of `battle_tour_gen.py`. |
| `tour_batch.py` | Runs a tour as many short parallel game runs, one per scene/map, with a private save and cache per slot. |
| `gen_soak_route.py` | Builds the long soak input route (`tests/tours`) from `tests/routes/load_walk.txt`. |

## Helpers

- `bdat1.py`: the BDAT table reader used by the generators (`python tools/tours/bdat1.py <file.bdat> <table> <count>`).
- `timed_route.py`: converts a frame-based route to elapsed milliseconds (`XCDE_INPUT_REPLAY` v2); covered by
  `tests/unit/test_timed_route.py`.

The committed tour files live in `tests/tours/`; regenerate them only when the game tables or the generator change,
and say so in the commit message.
