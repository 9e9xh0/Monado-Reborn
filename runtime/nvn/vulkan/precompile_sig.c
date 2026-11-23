// [XCDE] T282: the "are the precompiled shaders current?" signature.
//
// What the precompile writes (cache/spirv, the driver's shader and pipeline caches warmed by the program/seed/pipeline
// passes) is decided by the translator, the SPIR-V compiler options and the game files - not by the size and time of
// the executable. The done stamp used to name the executable, so every rebuild precompiled the shaders again; an edit
// to the launcher, the UI or gameplay code has no effect on the shaders, but cost a full warm run (the scan, every
// translation and driver pipeline creation).
//
// The signature (FNV-1a 64) covers:
//  - XCDE_SHADER_SIG: the build's fingerprint of the translator and SPIR-V compiler sources (tools/build/shader_sig.py;
//    <build>/inc/shader_sig.h, or -DXCDE_SHADER_SIG for a native test).
//  - the environment options that change the compiled SPIR-V (runtime/nvn/vulkan/shaders.c stage_opt): LARECOMP_VK_OPT
//    and XCDE_FS_OPT, by value.
//  - the game files: every file under <root>/romfs by relative name, size and mtime (for a .nup package: its own size
//    and mtime). A new, changed or removed shader container can add or change programs.
//  - the shipped variant seed (XCDE_VARIANTS_SEED or data/variants_seed.bin): size and mtime. New recorded variants
//    mean new jobs to warm.
// Not covered: cache/variants.bin (the player's own recordings: it grows while playing, so it must not invalidate the
// cache; the next precompile warms it) and the graphics driver (its shader caches are its own: after a driver update
// press "Precompile shaders").
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "../../host/utf8_path.h"
#include "../../platform/dir_utf8.h"
#include "precompile_sig.h"

#ifndef XCDE_SHADER_SIG
#include "shader_sig.h"  // <build>/inc: tools/build/shader_sig.py --out
#endif

static uint64_t mix(uint64_t h, const void *data, size_t n) {
    const uint8_t *b = data;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001B3ull; }
    return h;
}
static uint64_t mix_stat(uint64_t h, const struct stat *sb) {
    uint64_t v[2] = {(uint64_t)sb->st_size, (uint64_t)sb->st_mtime};
    return mix(h, v, sizeof v);
}
// Every file under dir, by relative name (rel): added to *sum, so the folder enumeration order does not matter.
static bool mix_dir(const char *dir, const char *rel, uint64_t *sum, uint64_t *files) {
    XcdeDir *dd = xcde_opendir(dir);
    if (!dd) return false;
    bool ok = true;
    XcdeDirent *e;
    while ((e = xcde_readdir(dd))) {
        if (e->d_name[0] == '.') continue;
        char p[2048], r[2048];
        int np = snprintf(p, sizeof p, "%s/%s", dir, e->d_name), nr = snprintf(r, sizeof r, "%s/%s", rel, e->d_name);
        struct stat sb;
        if (np < 0 || (size_t)np >= sizeof p || nr < 0 || (size_t)nr >= sizeof r || xcde_utf8_stat(p, &sb)) { ok = false; break; }
        if (S_ISDIR(sb.st_mode)) {
            if (!mix_dir(p, r, sum, files)) { ok = false; break; }
        } else if (S_ISREG(sb.st_mode)) {
            *sum += mix_stat(mix(0xcbf29ce484222325ull, r, strlen(r)), &sb);
            (*files)++;
        }
    }
    xcde_closedir(dd);
    return ok;
}

bool vk_precompile_sig(const char *root, char out[17]) {
    struct stat sb;
    if (xcde_utf8_stat(root, &sb)) return false;
    uint64_t h = mix(0xcbf29ce484222325ull, XCDE_SHADER_SIG, strlen(XCDE_SHADER_SIG)), files = 0;
    const char *const opt[2] = {getenv("LARECOMP_VK_OPT"), getenv("XCDE_FS_OPT")};
    for (int i = 0; i < 2; i++) { h = mix(h, opt[i] ? opt[i] : "", strlen(opt[i] ? opt[i] : "")); h = mix(h, "|", 1); }
    if (S_ISDIR(sb.st_mode)) {
        char romfs[2048];
        if (snprintf(romfs, sizeof romfs, "%s/romfs", root) >= (int)sizeof romfs || !mix_dir(romfs, "", &h, &files)) return false;
    } else {  // (a .nup package: the file itself is the game files)
        h = mix_stat(h, &sb);
        files = 1;
    }
    char seed_env[1100];
    const char *seed = xcde_utf8_getenv("XCDE_VARIANTS_SEED", seed_env, sizeof seed_env) ? seed_env : "data/variants_seed.bin";
    if (!xcde_utf8_stat(seed, &sb)) { h = mix(h, "seed", 4); h = mix_stat(h, &sb); }
    h = mix(h, &files, sizeof files);
    snprintf(out, 17, "%016llx", (unsigned long long)h);
    return true;
}
