// Profile-guided build (docs/guides/PGO.md). An instrumented build (cmake -DMONADO_PGO=gen) adds its counts to
// build/pgo/raw/default_<id>.profraw once a minute, so a training session that crashes or is killed still leaves
// them, and again when the process ends (plat_terminate: its _exit / TerminateProcess skip LLVM's atexit writer).
// After TotK Recomp's runtime/platform win32.c. In other builds these are empty.
#pragma once
#ifdef MONADO_PGO_GEN
#include <pthread.h>
#include <unistd.h>
int __llvm_profile_write_file(void);
void __llvm_profile_reset_counters(void);

static void *pgo_writer(void *arg) {
    (void)arg;
    for (;;) {
        sleep(60);
        // the file merges what it already holds (%m); the counters restart so the next write adds only what is new
        if (__llvm_profile_write_file() == 0) __llvm_profile_reset_counters();
    }
    return NULL;
}
static inline void pgo_start(void) {
    pthread_t t;
    if (pthread_create(&t, NULL, pgo_writer, NULL) == 0) pthread_detach(t);
}
static inline void pgo_flush(void) { __llvm_profile_write_file(); }
#else
static inline void pgo_start(void) {}
static inline void pgo_flush(void) {}
#endif
