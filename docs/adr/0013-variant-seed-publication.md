# ADR 0013 — Publish the precompilation variant seed despite Article I

- **Status:** Proposed · **Date:** 2026-11-20
- **Requirements:** Art. I, SC-005, T211, T240

## Decision (proposed)

`data/variants_seed.bin` may stay in the repository and in published packages as a narrow, explicit exception to
Article I.1 ("anything generated from [the game] ... caches"). The exception covers only pipeline-state keys recorded
by our own runtime, and is limited to:

- numeric records only: three guest code hashes, a geometry-stage flag, the vertex/fragment `ShaderKey`s and a
  pipeline key (`runtime/nvn/vulkan/exec.c`, `struct Variant`);
- no shader code, translated shaders, assets, text, keys, firmware, memory dumps or game tables;
- produced from real play sessions by this runtime and reproducible with `tools/research/merge_variants.py`;
- accepted by `tools/package/package_guard.py` (Constitution art. I, SC-012).

Current contents: `XCVC` header (magic `0x56435843`, record size in the header) plus 30,338 records of 904 bytes =
27,425,560 bytes, one revision since the initial import. Readers: `runtime/nvn/vulkan/precompile.c`
(`data/variants_seed.bin`, `$XCDE_VARIANTS_SEED`, and the player's own `cache/variants.bin`) and
`runtime/nvn/vulkan/exec.c` (`vk_precompile_variant`). It is shipped on purpose: `tools/package/make_appimage.py`
bundles it in the AppImage and `tools/package/make_portable.py` copies `data/` into release packages.

## Reason

The seed is what makes first-run shader precompilation useful: without it every player rediscovers thousands of
pipeline variants, with visible hitches. The project plan already describes `data/` as "no game content", the file
compresses to ~859 KiB in git, and it has been published in every release so far. Article I.1 as written (which names
"caches") would forbid it, so leaving the exception unstated makes the constitution inaccurate.

## Alternatives considered

- Remove it from git and serve it as a release asset / download at run time: loses offline prewarm and adds a network
  dependency to the first run.
- Generate the variants only from the player's own play: exactly the cost the seed exists to avoid.
- Keep the current silent exception: leaves a known Article I deviation undocumented, which is worse than a rule.

## Consequences

- Accepting this ADR requires a one-line amendment of `CONTRIBUTING.md` Article I and the changelog entry that
  Article I requires for a change of rules; this ADR does not amend the constitution by itself.
- Budget: one record-layout revision per release train. Changing the record format or adding a field class requires a
  new ADR, and the file size/format must stay reproducible with the merge tool.
- If game-derived content (strings, assets, shaders) is ever added to a seed, this exception does not apply.
