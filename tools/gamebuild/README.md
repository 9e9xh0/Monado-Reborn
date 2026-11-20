# tools/gamebuild: the first-start game code builder

Release packages ship **no game code**. They carry this builder instead: on first start the launcher runs it with the
player's own dump, it lifts the guest code to C, compiles it with the bundled `zig cc` and links the **game library**
(`lasgame.so` / `lasgame.dll`) that the runtime loads.

The four stages are one pipeline, run at different times:

| Stage | File | When |
|---|---|---|
| Stage the resources | `gamebuild_stage.py` | On the release build machine: copies the lifter inputs (headers, game profile, patches), a trimmed zig and a compiler probe into `build/gamebuild/` and writes `link.json`. |
| Freeze the builder | `freeze_gamebuild.py` | On the release build machine: turns the builder into `build/gamebuild/monado-gamebuild[.exe]` with PyInstaller, so players need no Python. |
| Build the library | `gamebuild.py` | On the player's PC, when the launcher starts the game for the first time (or after a dump/lifter change): lifts, compiles, links, writes its progress to `build/gamebuild/progress.txt`. `gamebuild_entry.py` is the frozen binary's entry point. |
| Load it | `gamelib_entry.c` | Compiled into the library: it exports `lasgame_tables()`, the guest entry tables the runtime looks up after `dlopen`. |

**"gamebuild" vs "gamelib" are different layers:**

- *gamebuild* is the builder package that lives in `build/gamebuild/` (resources + frozen executable).
- *gamelib* is the product (`lasgame.so` / `lasgame.dll`) built from the player's dump, held in the writable data
  folder and loaded by the runtime. The runtime refuses a library whose `GAMELIB_ID` does not match its own
  (fingerprint from `tools/build/game_id.py`).

Windows release packages additionally export the executable's symbols for the library to link against:
`coff_exports.py` writes the `.drectve` export file and `pe_implib.py` writes `xcde_imports.a` from the linked
executable. They are build helpers of this pipeline too.

Run it by hand (the release workflow does the same):

```bash
.venv/bin/python tools/gamebuild/gamebuild_stage.py --dest build/gamebuild --platform linux --pgo build/pgo/monado.profdata
.venv/bin/python tools/gamebuild/freeze_gamebuild.py --python .venv/bin/python
python tools/gamebuild/gamebuild.py --root targets --resources build/gamebuild   # what the launcher runs
```
