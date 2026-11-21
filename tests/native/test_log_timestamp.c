// [XCDE] T275: the timestamped diagnostics log (runtime/log.c). Checks that every line gets one timestamp, that a
// line written in pieces keeps a single prefix and that a multi-line message is prefixed at every line. No game data.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../runtime/platform.h"

// runtime/log.c asks the platform layer for the wall clock; this test does not link it and uses a fixed instant.
uint64_t plat_wall_ns(void) { return 1700000000123456789ull; }  // any date; only the shape is checked

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

// "[YYYY-MM-DD HH:MM:SS.mmm] " in local time.
static int stamped(const char *s) {
    if (strlen(s) < 26) return 0;
    if (s[0] != '[' || s[5] != '-' || s[8] != '-' || s[11] != ' ' || s[14] != ':' || s[17] != ':' || s[20] != '.' ||
        s[24] != ']' || s[25] != ' ')
        return 0;
    for (int i = 0; i < 26; i++) {
        int digit = (i >= 1 && i <= 4) || (i >= 6 && i <= 7) || (i >= 9 && i <= 10) || (i >= 12 && i <= 13) ||
                    (i >= 15 && i <= 16) || (i >= 18 && i <= 19) || (i >= 21 && i <= 23);
        if (digit && (s[i] < '0' || s[i] > '9')) return 0;
    }
    return 1;
}

int main(void) {
#ifdef _WIN32
    _putenv_s("XCDE_LOG_TIME", "");  // never inherit a disabled logger from the caller
#else
    unsetenv("XCDE_LOG_TIME");
#endif
    const char *path = "test_log_timestamp.out";
    FILE *out = freopen(path, "w", stderr);
    CHECK(out != NULL);
    if (!out) return 1;

    rt_logf("first line\n");
    rt_logf("partial");  // continued by the next call: one prefix, not two
    rt_logf(" line\n");
    rt_logf("multi a\nmulti b\n");  // one timestamp per line
    rt_logf("blank follows:\n\nend\n");  // a blank line is left alone (no empty timestamp)
    rt_logf("last line without newline");
    fflush(stderr);
    fclose(stderr);

    const char *expect[] = {"first line", "partial line", "multi a", "multi b", "blank follows:", "",
                            "end", "last line without newline"};
    int n = 0, nlines = (int)(sizeof expect / sizeof expect[0]);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (f) {
        char line[512];
        while (n < nlines && fgets(line, sizeof line, f)) {
            size_t len = strlen(line);
            if (len && line[len - 1] == '\n') line[--len] = 0;
            if (!expect[n][0]) {
                CHECK(len == 0);  // blank lines are not timestamped
            } else {
                CHECK(stamped(line));
                if (n == 0 && stamped(line)) CHECK(strncmp(line + 21, "123", 3) == 0);  // the stub clock's .123
                if (stamped(line)) CHECK(strcmp(line + 26, expect[n]) == 0);
                else printf("  line %d: '%s'\n", n + 1, line);
            }
            n++;
        }
        CHECK(fgets(line, sizeof line, f) == NULL);  // and nothing after the last partial line
        fclose(f);
    }
    CHECK(n == nlines);
    remove(path);
    printf("log timestamps: %s\n", fails ? "FAIL" : "OK");
    return fails ? 1 : 0;
}
