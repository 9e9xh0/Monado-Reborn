// [XCDE] T282: the signature of the inputs of the precompiled shader cache. `--precompile-status` (precompile.c)
// compares it with the `sig` line of cache/precompile_done.txt: a build, SPIR-V option or game-file change that can
// alter the precompiled shaders makes them stale, anything else keeps them. See precompile_sig.c.
#pragma once
#include <stdbool.h>

// Writes 16 lowercase hex digits + NUL. false: the game files are not readable (nothing can be said about the cache).
bool vk_precompile_sig(const char *root, char out[17]);
