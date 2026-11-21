// [XCDE] T275: timestamped diagnostics log (build/last_game.log).
//
// Every line the runtime writes to stderr starts with its local wall-clock time, which is what a user report needs to
// tell when a hang, a stutter or a settings change happened:
//
//     [2026-11-21 18:36:13.123] [vk] t=399.8 frame 19374 60.0 fps max 19ms ...
//
// Cost is kept off the game's back: the clock read is one vDSO call (Linux) / GetSystemTimePreciseAsFileTime, the
// "YYYY-MM-DD HH:MM:SS." text is rebuilt only when the second changes (per thread), and a line is formatted once
// (vsnprintf) and written with one fwrite. The common path uses stack buffers; only a message over MSG_STACK bytes
// allocates. Callers that write a line in pieces ("x" then later "\n") keep one prefix, and a multi-line message gets
// a prefix at every line, through a thread-local "at line start" flag.
//
// XCDE_LOG_TIME=0|off|none|false turns the timestamps off: then this is fprintf(stderr) with no prefix (useful for
// byte-exact log comparisons and for measuring the overhead itself).
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"

#define STAMP_MAX 32   // "[YYYY-MM-DD HH:MM:SS." fits in 32 (20 chars used)
#define MSG_STACK 1024
#define OUT_STACK 1024

static _Atomic int g_enabled = -1;  // -1: XCDE_LOG_TIME not read yet, 0 off, 1 on

static bool log_enabled(void) {
    int e = atomic_load_explicit(&g_enabled, memory_order_relaxed);
    if (e < 0) {
        const char *v = getenv("XCDE_LOG_TIME");
        e = !(v && (!strcmp(v, "0") || !strcmp(v, "off") || !strcmp(v, "none") || !strcmp(v, "false")));
        atomic_store_explicit(&g_enabled, e, memory_order_relaxed);
    }
    return e != 0;
}

// Per thread: the cached calendar text for the current wall-clock second and whether the last write ended a line.
typedef struct {
    time_t sec;
    int len;  // length of text ("YYYY-MM-DD HH:MM:SS.")
    char text[STAMP_MAX];
    int at_line_start;
} LogState;
static _Thread_local LogState tl = {0, 0, {0}, 1};

// "[YYYY-MM-DD HH:MM:SS.mmm] ": the calendar part is cached; only the 3 millisecond digits are formatted per line.
static size_t stamp(char *out, uint64_t wall_ns) {
    time_t sec = (time_t)(wall_ns / 1000000000ull);
    unsigned ms = (unsigned)((wall_ns / 1000000ull) % 1000ull);
    if (sec != tl.sec || !tl.len) {
        struct tm tm;
        memset(&tm, 0, sizeof tm);
#ifdef _WIN32
        if (localtime_s(&tm, &sec) != 0) memset(&tm, 0, sizeof tm);
#else
        if (!localtime_r(&sec, &tm)) memset(&tm, 0, sizeof tm);
#endif
        tl.len = snprintf(tl.text, sizeof tl.text, "%04d-%02d-%02d %02d:%02d:%02d.", tm.tm_year + 1900, tm.tm_mon + 1,
                          tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        if (tl.len < 0) tl.len = 0;
        if ((size_t)tl.len >= sizeof tl.text) tl.len = (int)sizeof tl.text - 1;
        tl.sec = sec;
    }
    size_t n = 0;
    out[n++] = '[';
    memcpy(out + n, tl.text, (size_t)tl.len);
    n += (size_t)tl.len;
    out[n++] = (char)('0' + ms / 100);
    out[n++] = (char)('0' + ms / 10 % 10);
    out[n++] = (char)('0' + ms % 10);
    out[n++] = ']';
    out[n++] = ' ';
    return n;
}

// Writes one log line, prefixing every new line with the time. A partial line (no trailing newline) is continued by
// the next call without a second prefix. The whole call is one fwrite, so threads don't interleave inside a line.
void rt_logf(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    char msg_stack[MSG_STACK];
    int n = vsnprintf(msg_stack, sizeof msg_stack, fmt, ap);
    va_end(ap);
    if (n < 0) {  // encoding error (no bytes written)
        va_end(ap2);
        return;
    }
    size_t len = (size_t)n;
    char *heap_msg = NULL;
    const char *msg = msg_stack;
    if (len >= sizeof msg_stack) {  // longer than the stack buffer: format once more, exactly
        heap_msg = malloc(len + 1);
        if (heap_msg) {
            vsnprintf(heap_msg, len + 1, fmt, ap2);
            msg = heap_msg;
        } else {
            len = sizeof msg_stack - 1;  // out of memory: keep the beginning rather than lose the line
        }
    }
    va_end(ap2);

    if (!len) {
        free(heap_msg);
        return;
    }
    if (!log_enabled()) {
        fwrite(msg, 1, len, stderr);
        free(heap_msg);
        return;
    }

    char prefix[STAMP_MAX + 8];
    size_t plen = stamp(prefix, plat_wall_ns());

    // How large the prefixed text is: the message plus one timestamp at every line start.
    size_t need = len;
    int at = tl.at_line_start;
    for (size_t i = 0; i < len; i++) {  // blank lines stay unprefixed: a timestamp on a line with nothing is noise
        if (at && msg[i] != '\n') need += plen;
        at = msg[i] == '\n';
    }

    char out_stack[OUT_STACK];
    char *out = out_stack;
    bool heap_out = false;
    if (need >= sizeof out_stack) {
        out = malloc(need + 1);
        heap_out = out != NULL;
        if (!out) out = out_stack;  // out of memory: what fits in the stack buffer is written below
    }
    size_t room = heap_out ? need + 1 : sizeof out_stack;
    size_t o = 0;
    at = tl.at_line_start;
    for (size_t i = 0; i < len && o + 1 < room; i++) {
        if (at && msg[i] != '\n' && o + plen + 1 <= room) {
            memcpy(out + o, prefix, plen);
            o += plen;
        }
        out[o++] = msg[i];
        at = msg[i] == '\n';
    }
    tl.at_line_start = at;
    out[o] = 0;
    fwrite(out, 1, o, stderr);
    if (heap_out) free(out);
    free(heap_msg);
}
