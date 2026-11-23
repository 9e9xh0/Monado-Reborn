// [XCDE] T282 review: the Maxwell translator must be re-entrant. Since T281 the precompile translates programs on
// several worker threads at once (exec.c xlat_lock skips its global lock for tl_inline_jobs), so the scratch rings
// (RSC2/img_coord), the lazy getenv caches and the guest-heap range must not be shared between threads. This test
// translates two synthetic programs (a texture op and a surface op, the two scratch-buffer users) on N threads and
// compares every GLSL source with the single-threaded output.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// [XCDE] T275: runtime/log.c is not linked into this focused test.
void rt_logf(const char *fmt, ...) { (void)fmt; }

#include "../../runtime/nvn/maxwell/shader.h"

ShaderInfo *maxwell_translate(const uint8_t *blob, size_t max_size, int stage, const ShaderKey *key);
void maxwell_free(ShaderInfo *s);

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

#define EXIT_W 0xe30000000007000full
#define NOP_W 0x50b0000000070f00ull

// The same field encoders as tests/native/test_translate_tex.c (no game shaders).
static uint64_t tld4s(int d0, int x, int y, int d1, int tex, int comp) {
    return 0xdf00000000000000ull | ((uint64_t)tex << 36) | ((uint64_t)d1 << 28) | ((uint64_t)y << 20) | (7ull << 16) |
           ((uint64_t)x << 8) | (uint64_t)d0 | ((uint64_t)comp << 52);
}
static uint64_t suld32(int d, int x, int tex) {  // SULD, bound, 2D, 32-bit data, field at 8
    return 0xeb00000000000000ull | (8ull << 48) | (2ull << 52) | ((uint64_t)tex << 36) | (3ull << 33) | (8ull << 20) |
           (7ull << 16) | ((uint64_t)x << 8) | (uint64_t)d;
}

// which 0: a texture fetch through the integer-coordinate scratch ring (RSC2); 1: a surface load through img_coord().
static ShaderInfo *translate_one(int which, const ShaderKey *key) {
    uint8_t blob[0x80 + 8 * 16];
    memset(blob, 0, sizeof blob);
    uint64_t *code = (uint64_t *)(blob + 0x80);
    code[0] = 0;
    code[1] = which ? suld32(0, 8, 2) : tld4s(0, 8, 9, 4, 3, 0);
    code[2] = EXIT_W;
    code[3] = NOP_W;
    return maxwell_translate(blob, sizeof blob, STAGE_FRAGMENT, key);
}

enum { NTHREADS = 8, ITERS = 48 };
static char *ref_src[2];
static _Atomic int thread_fails;

static void *worker(void *arg) {
    (void)arg;
    ShaderKey key;
    memset(&key, 0, sizeof key);
    key.glsl = 1;
    for (int it = 0; it < ITERS; it++) {
        for (int which = 0; which < 2; which++) {
            ShaderInfo *s = translate_one(which, &key);
            if (!s || !s->msl) { atomic_fetch_add(&thread_fails, 1); if (s) maxwell_free(s); continue; }
            if (strcmp(s->msl, ref_src[which])) atomic_fetch_add(&thread_fails, 1);
            maxwell_free(s);
        }
    }
    return NULL;
}

int main(void) {
    ShaderKey key;
    memset(&key, 0, sizeof key);
    key.glsl = 1;
    for (int which = 0; which < 2; which++) {
        ShaderInfo *s = translate_one(which, &key);
        CHECK(s && s->msl);
        if (!s || !s->msl) { if (s) printf("  error: %s\n", s->error); if (s) maxwell_free(s); return 1; }
        ref_src[which] = strdup(s->msl);
        maxwell_free(s);
    }
    for (int round = 0; round < 3; round++) {
        pthread_t th[NTHREADS];
        atomic_store(&thread_fails, 0);
        for (int i = 0; i < NTHREADS; i++) CHECK(pthread_create(&th[i], NULL, worker, NULL) == 0);
        for (int i = 0; i < NTHREADS; i++) pthread_join(th[i], NULL);
        CHECK(atomic_load(&thread_fails) == 0);
    }
    free(ref_src[0]);
    free(ref_src[1]);
    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    puts("translate threads: parallel output matches the serial translation");
    return 0;
}
