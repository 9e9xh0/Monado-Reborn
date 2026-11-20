# tools/research: one-off research tools

These tools were written to answer a specific question while reversing or lifting the game. They read the user's own
dump or build output, are not part of the build, and most are not run in CI. Their outputs (counts, addresses,
classifications, variant seeds) feed the documents under `docs/reference/` and `docs/evidence/`.

| File | Question it answered | Output |
|---|---|---|
| `analyze_nso.py` | Instruction/symbol counts and imported API names per module | `docs/reference/inventory.md`, `docs/reference/nso_stats.json` |
| `classify_br.py` | How indirect `br` sites resolve (T020) | counts on stdout, `runtime/nvn/maxwell/translate.c` research |
| `spch_attrs.py` | Vertex attributes and control-section layout of every SPCH shader program (T211) | survey on stdout |
| `glsl_dataflow.py` | Symbolic dataflow of translated vertex shaders (`build/glsl/…_vs.glsl`) | comparison report |
| `merge_variants.py` | Merge `cache/variants.bin` recordings into the shipped seed `data/variants_seed.bin` (T211) | deduplicated seed |

The bar-crop and frost-check ad-hoc image scripts that used to live here were removed in the 2026-11 cleanup: they
hard-coded the author's desktop paths and had served their purpose. Use `tools/media/` for capture analysis.
