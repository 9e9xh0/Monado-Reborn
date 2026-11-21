#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform.h"
#include "rt.h"

// Minimal stubs for symbols referenced by runtime/platform/linux.c
void rt_logf(const char *fmt, ...) { (void)fmt; }
bool rt_fault_watch(uint64_t fa, uint64_t pc) { (void)fa; (void)pc; return false; }
void rt_fault_report(const char *what, uint64_t fa, uint64_t pc, uint64_t lr) {
    fprintf(stderr, "FATAL test fault: %s fa=%llx pc=%llx lr=%llx\n",
            what, (unsigned long long)fa, (unsigned long long)pc, (unsigned long long)lr);
    abort();
}
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset) { (void)pc; (void)offset; return NULL; }
void pgo_start(void) {}
void pgo_flush(void) {}

typedef struct {
    uint64_t base;
    size_t npages;
} ThreadArg;

static void *writer_thread(void *arg) {
    ThreadArg *ta = arg;
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)ta->base;
    for (size_t i = 0; i < ta->npages; i++) {
        p[i * 4096 + 17] = (uint8_t)(i + 1);
    }
    return NULL;
}

int main(void) {
    // Test 1: reserve guest window and probe write-watch
    assert(plat_reserve_guest(GUEST_LO, GUEST_HI));
    uint64_t pg = 0;
    uint64_t pages[64];
    int n = plat_written_pages(HEAP_BASE, 4096, pages, 64, &pg);
    const char *env = getenv("XCDE_LINUX_WRITE_WATCH");
    if (env && atoi(env) == 0) {
        assert(n == -1);
        printf("test_linux_write_watch: disabled mode passed\n");
        return 0;
    }
    assert(n == 1 && pg == 4096 && pages[0] == HEAP_BASE);

    // Steady-state clean query: 0 dirty pages
    n = plat_written_pages(HEAP_BASE, 4096, pages, 64, &pg);
    assert(n == 0);

    // Test 2: single-thread write to specific pages in a 16-page range
    uint64_t range_base = HEAP_BASE + 0x100000;
    uint64_t range_size = 16 * 4096;
    n = plat_written_pages(range_base, range_size, pages, 64, &pg);
    assert(n == 16);  // first touch: all WW_UNTRACKED -> dirty & armed
    n = plat_written_pages(range_base, range_size, pages, 64, &pg);
    assert(n == 0);   // now clean

    volatile uint8_t *mem = (volatile uint8_t *)(uintptr_t)range_base;
    mem[2 * 4096 + 8] = 0xAA;
    mem[9 * 4096 + 100] = 0xBB;
    n = plat_written_pages(range_base, range_size, pages, 64, &pg);
    assert(n == 2);
    assert(pages[0] == range_base + 2 * 4096);
    assert(pages[1] == range_base + 9 * 4096);
    assert(mem[2 * 4096 + 8] == 0xAA);
    assert(mem[9 * 4096 + 100] == 0xBB);

    // Clean again
    assert(plat_written_pages(range_base, range_size, pages, 64, &pg) == 0);

    // Test 3: multi-thread concurrent writes to watched pages
    uint64_t mt_base = HEAP_BASE + 0x200000;
    assert(plat_written_pages(mt_base, 8 * 4096, pages, 64, &pg) == 8);
    assert(plat_written_pages(mt_base, 8 * 4096, pages, 64, &pg) == 0);
    pthread_t th[2];
    ThreadArg a0 = {mt_base, 4};
    ThreadArg a1 = {mt_base + 4 * 4096, 4};
    pthread_create(&th[0], NULL, writer_thread, &a0);
    pthread_create(&th[1], NULL, writer_thread, &a1);
    pthread_join(th[0], NULL);
    pthread_join(th[1], NULL);
    n = plat_written_pages(mt_base, 8 * 4096, pages, 64, &pg);
    assert(n == 8);

    // Test 4: plat_will_write + pread() into a watched page (no EFAULT)
    uint64_t io_base = HEAP_BASE + 0x300000;
    assert(plat_written_pages(io_base, 2 * 4096, pages, 64, &pg) == 2);
    assert(plat_written_pages(io_base, 2 * 4096, pages, 64, &pg) == 0);
    int fd = open("/dev/zero", O_RDONLY);
    assert(fd >= 0);
    plat_will_write(io_base, 2 * 4096);
    ssize_t r = pread(fd, (void *)(uintptr_t)io_base, 2 * 4096, 0);
    close(fd);
    assert(r == 2 * 4096);
    n = plat_written_pages(io_base, 2 * 4096, pages, 64, &pg);
    assert(n == 2);

    // Test 5: WW_HOT promotion after 16 consecutive dirty queries, then cooldown after 240 queries
    uint64_t hot_base = HEAP_BASE + 0x400000;
    assert(plat_written_pages(hot_base, 4096, pages, 64, &pg) == 1);
    for (int i = 0; i < 16; i++) {
        *(volatile uint8_t *)(uintptr_t)hot_base = (uint8_t)i;
        assert(plat_written_pages(hot_base, 4096, pages, 64, &pg) == 1);
    }
    // Now WW_HOT: reports dirty without writing for 240 queries, then cools down to WW_WATCHED
    for (int i = 0; i < 240; i++) {
        assert(plat_written_pages(hot_base, 4096, pages, 64, &pg) == 1);
    }
    // After cooldown, the page is WW_WATCHED again so a clean query returns 0
    assert(plat_written_pages(hot_base, 4096, pages, 64, &pg) == 0);

    // Test 6: plat_reset_guest clears tracking
    assert(plat_reset_guest(GUEST_LO, GUEST_HI));
    assert(plat_written_pages(hot_base, 4096, pages, 64, &pg) == 1);
    assert(plat_written_pages(hot_base, 4096, pages, 64, &pg) == 0);

    printf("test_linux_write_watch: all assertions passed\n");
    return 0;
}
