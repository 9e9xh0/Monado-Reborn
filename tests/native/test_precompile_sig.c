// [XCDE] T282: vk_precompile_sig - the signature that decides whether the precompiled shader cache is current. The
// inputs are the build's translator/SPIR-V compiler fingerprint (XCDE_SHADER_SIG), the options that change the SPIR-V
// and the game files; the build generates shader_sig.h, this test passes the macro instead.
#ifndef XCDE_SHADER_SIG
#error "test_precompile_sig needs -DXCDE_SHADER_SIG=... (precompile_sig.c takes it when the build header is absent)"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#define PID _getpid()
static void set_env(const char *name, const char *value) {
    char line[1200];
    snprintf(line, sizeof line, "%s=%s", name, value ? value : "");
    _putenv(line);
}
#else
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#define RMDIR(p) rmdir(p)
#define PID getpid()
static void set_env(const char *name, const char *value) {
    if (value) setenv(name, value, 1);
    else unsetenv(name);
}
#endif

#include "../../runtime/nvn/vulkan/precompile_sig.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static char root[1024];

static void path_of(char *out, size_t cap, const char *rel) { snprintf(out, cap, "%s/%s", root, rel); }
static void write_file(const char *rel, const char *data, size_t n) {
    char p[1200];
    path_of(p, sizeof p, rel);
    FILE *f = fopen(p, "wb");
    CHECK(f != NULL);
    if (f) { CHECK(fwrite(data, 1, n, f) == n); fclose(f); }
}
static bool sig_of(const char *rel, char out[17]) {
    char p[1200];
    path_of(p, sizeof p, rel);
    return vk_precompile_sig(p, out);
}
static bool make_dir(const char *rel) {
    char p[1200];
    path_of(p, sizeof p, rel);
    return MKDIR(p) == 0;
}
static void remove_file(const char *rel) {
    char p[1200];
    path_of(p, sizeof p, rel);
    remove(p);
}
static bool hex16(const char *s) {
    if (strlen(s) != 16) return false;
    for (int i = 0; i < 16; i++) if (!isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i])) return false;
    return true;
}

int main(void) {
    char seed[1200];
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    snprintf(root, sizeof root, "%s/monado-precompile-sig-%ld", tmp && *tmp ? tmp : ".", (long)PID);
#else
    snprintf(root, sizeof root, "/tmp/monado-precompile-sig-%ld", (long)PID);
#endif
    RMDIR(root);  // (a leftover from a killed run)
    CHECK(MKDIR(root) == 0);
    CHECK(make_dir("dump"));
    CHECK(make_dir("dump/romfs"));
    CHECK(make_dir("dump/romfs/sub"));
    write_file("dump/romfs/a.wimdo", "one", 3);
    write_file("dump/romfs/sub/b.wismt", "two", 3);
    write_file("dump/romfs/c.wishp", "three", 5);
    write_file("seed.bin", "seed", 4);
    write_file("dump.nup", "package", 7);
    path_of(seed, sizeof seed, "seed.bin");
    set_env("XCDE_VARIANTS_SEED", seed);
    set_env("XCDE_FS_OPT", NULL);
    set_env("LARECOMP_VK_OPT", NULL);

    // Stable, 16 lowercase hex.
    char a[17], b[17];
    CHECK(sig_of("dump", a));
    CHECK(hex16(a));
    CHECK(sig_of("dump", b));
    CHECK(!strcmp(a, b));

    // A changed file (same name, other content/size).
    write_file("dump/romfs/a.wimdo", "a longer content", 15);
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);

    // An added file, and a renamed one (the relative name is part of the signature: same bytes, other program set).
    write_file("dump/romfs/sub/d.wiefb", "four", 4);
    CHECK(sig_of("dump", a));
    CHECK(strcmp(a, b) != 0);
    remove_file("dump/romfs/sub/d.wiefb");
    char from[1200], to[1200];
    path_of(from, sizeof from, "dump/romfs/a.wimdo");
    path_of(to, sizeof to, "dump/romfs/e.wimdo");
    CHECK(rename(from, to) == 0);
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);

    // A .nup package is one file: its size (and time) is the fingerprint.
    char p1[17], p2[17];
    CHECK(sig_of("dump.nup", p1));
    write_file("dump.nup", "package with more", 17);
    CHECK(sig_of("dump.nup", p2));
    CHECK(strcmp(p1, p2) != 0);

    // The environment options that change the compiled SPIR-V.
    CHECK(sig_of("dump", a));
    set_env("XCDE_FS_OPT", "0");
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);
    set_env("XCDE_FS_OPT", NULL);
    set_env("LARECOMP_VK_OPT", "0");
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);
    set_env("LARECOMP_VK_OPT", NULL);

    // The shipped variant seed: new recorded variants mean new jobs to warm.
    CHECK(sig_of("dump", a));
    write_file("seed.bin", "a bigger seed file", 18);
    CHECK(sig_of("dump", b));
    CHECK(strcmp(a, b) != 0);

    // Unreadable game files: nothing can be said about the cache.
    char missing[1200];
    path_of(missing, sizeof missing, "no-such-dump");
    CHECK(!vk_precompile_sig(missing, a));

    // Clean up (best effort: the test must not leave files behind).
    remove_file("dump/romfs/a.wimdo");
    remove_file("dump/romfs/e.wimdo");
    remove_file("dump/romfs/c.wishp");
    remove_file("dump/romfs/sub/b.wismt");
    remove_file("dump.nup");
    remove_file("seed.bin");
    {
        char p[1200];
        path_of(p, sizeof p, "dump/romfs/sub"); RMDIR(p);
        path_of(p, sizeof p, "dump/romfs"); RMDIR(p);
        path_of(p, sizeof p, "dump"); RMDIR(p);
    }
    RMDIR(root);

    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    puts("precompile signature: OK");
    return 0;
}
