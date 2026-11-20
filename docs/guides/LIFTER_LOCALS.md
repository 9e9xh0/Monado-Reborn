# Function-local guest state in the lifted code

Ideas from TotK Recomp (`recomp/flags.py`, `regs.py`, `vlocals.py`, MIT), measured here one at a time.

## NZCV flags in a local (done)

`tools/lifter/flags.py` (ported from TotK Recomp) and the `Flags` helpers in `runtime/cpu.h`.

Before, every flag-setting instruction packed N, Z, C and V into `c->nzcv` (`subs64()` and friends) and every reader
unpacked them (`COND_xx(c)`). `Cpu *restrict c` already let clang keep `c->nzcv` in a register inside a function, but
not drop the packing or the store before each call and return. Now a generated function keeps its flags in a local
`Flags F`:

- A compare stores its operands; a reader whose definitions are all of one kind computes its condition from them, so
  `cmp x1, x2; b.lt` is one host compare.
- Definitions only known packed (CCMP, FCMP, MSR NZCV, ADCS) write `F.nz`, as do several kinds reaching one reader.
- `c->nzcv` is written only where the function leaves by a tail branch, a `br` or falling into the next function.
  Calls and returns publish nothing: AAPCS64 code neither passes flags to a callee nor reads a callee's.
- Functions re-entered through the all-instruction dispatch (unresolved computed `br`), patch and randomizer sites
  keep flags in `c->nzcv`, as before.

In the game code, about 95% of flag accesses now use `F` (main: 97,076 local, 2,820 still in the `Cpu`; sdk: 72,540
and 6,307). The executable shrank from 82.0 MB to 72.8 MB.

**Checked** (2026-11-18, deterministic mode, frame hashes): identical on every frame of the new-game route (59,969
frames: boot, title, opening cutscene) and of a save loaded into the field (44,993 frames).

**Measured** (RX 6900 XT, deterministic mode, -O2 without a profile, mean CPU time per frame, 2 runs each):

| | Boot | Title and menus | Field | Whole run |
|---|---|---|---|---|
| -O2 | 2.87 / 2.79 ms | 3.20 / 3.14 ms | 12.09 / 11.86 ms | 5.17 / 5.06 ms |
| -O2 + local flags | 2.84 / 2.79 ms | 3.18 / 3.15 ms | 11.47 / 11.38 ms | 4.99 / 4.94 ms |

Field -4.6%, whole run -2.5%.

The change alters the generated C and `cpu.h`, so it changes `GAMELIB_ID` (players rebuild their game code once) and
makes the published PGO profile stale: retrain it (docs/guides/PGO.md) before release; the release workflow's `pgo` job
fails on a stale profile.

## General registers in locals (done)

`tools/lifter/regs.py` (ported from TotK Recomp). Each function keeps its 6 most used X registers and SP in C locals,
loaded at entry, stored back only where they changed since the last sync, before a call, an SVC, a trap, a return or
a tail exit. After a call only X0-X18 and X30 are reloaded: AAPCS64 has the callee preserve X19-X29 and SP. The same
functions as for the flags take part; the others keep every register in the `Cpu`.

`Cpu *restrict c` already let clang keep registers across guest loads and stores, so what this adds is mostly that
callee-saved trust and fewer stores. Our earlier attempt with every register in locals (T165) cost 5.6% more
instructions; 6 per function is TotK's measured best.

**Why the trust holds in XC:DE** (checked 2026-11-18):

- Guest fibers: each has its own host fiber and `Cpu` (`runtime/hle/fibers.c`); switching never changes the caller's.
- Reboot and ExitThread longjmp out of the whole thread; the dropped frames never run again (`kernel.c`).
- HLE code that calls back into the guest restores the whole `Cpu` (`party.c` `call_guest`) or the link register
  (`heaps.c`, whose guest callees follow AAPCS64 themselves). The runtime writes no X19-X29 of a running thread;
  `c->sp` only on a new fiber's `Cpu` and inside those save / restore pairs.
- The generated code hands the `Cpu` to the runtime only through `rt_dispatch`, `rt_svc` and `rt_udf`; no SIMD / FP
  helper reads X registers.
- longjmp and the C++ unwinder exist in sdk, but nothing calls them directly and main imports neither. Their lifted
  entry points call `rt_trust_fault`, which stops the game with a message, so a call through a pointer cannot run on
  with stale registers. If that ever fires, regenerate with `XCDE_CALLEE_TRUST=0`.

`XCDE_REG_LOCALS=<n>` (0: off) and `XCDE_CALLEE_TRUST=0` change the generator for experiments; releases use the
defaults, since `GAMELIB_ID` hashes the generator's files, not the environment.

**Checked:** frame hashes identical to the original code on every frame of both routes (59,969 and 44,993 frames);
`rt_trust_fault` never fired. The executable shrank further, from 72.8 MB to 69.9 MB.

**Measured** (same setup, against flags only, 2 runs each):

| | Boot | Title and menus | Field | Whole run |
|---|---|---|---|---|
| Flags only | 2.80 / 2.81 ms | 3.12 / 3.14 ms | 11.39 / 11.58 ms | 4.94 / 5.00 ms |
| Flags + registers | 2.78 / 2.78 ms | 3.11 / 3.09 ms | 11.01 / 11.25 ms | 4.85 / 4.87 ms |

Field -3.1%, whole run -2.2%; with the flags, about -7% in the field against plain -O2.

## With the profile

The PGO profile was retrained for this code (scripted routes plus a play session; 12,528 game functions with counts).
Against what players ran before (-O1, no profile, packed flags, registers in the Cpu), field from a save, 2 runs each:

| | Boot | Title and menus | Field | Whole run |
|---|---|---|---|---|
| -O1 | 2.86 / 2.82 ms | 3.20 / 3.20 ms | 13.25 / 13.22 ms | 5.45 / 5.41 ms |
| PGO + flags + registers | 2.68 / 2.66 ms | 3.06 / 2.97 ms | 10.29 / 10.32 ms | 4.61 / 4.58 ms |

Field -22.1%, whole run -15.4% (PGO alone: -15.2% in the field). The field was part of the training, so this is a
best case for unseen scenes.

## V registers in locals (not done)

TotK also keeps each function's 8 most used V registers in a `u128 vr[32]` that clang holds in XMM registers
(`recomp/vlocals.py`). Not tried here yet.
