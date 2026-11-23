// [XCDE] T280: the game files from a .nup package (runtime/services/game_files.c), on a small package written here
// with the layout RecompLauncher's nup.rs documents: a base title and an update, nested romfs folders, a padded child
// list. No game data.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "services/game_files.h"

#define OUT "build/test-linux"
#define PKG OUT "/nup-test.nup"
#define BAD OUT "/nup-bad.nup"
#define DIR_DUMP OUT "/nup-folder"

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL %s: %s\n", __func__, what); failures++; } } while (0)

// Dirs: 0 base root, 1 base exefs, 2 base romfs, 3 update root, 4 update exefs, 5 update romfs, 6 update romfs/menu.
// Files (node 7 + i): 0 base exefs/main, 1 update exefs/main, 2 update exefs/main.npdm, 3 romfs/menu/a.bin,
// 4 romfs/top.txt, 5 base romfs/old.txt.
enum { NDIRS = 7, NFILES = 6 };
static const char *dir_names[NDIRS] = {"", "exefs", "romfs", "", "exefs", "romfs", "menu"};
static const char *file_names[NFILES] = {"main", "main", "main.npdm", "a.bin", "top.txt", "old.txt"};
static const char *file_data[NFILES] = {"BASE-MAIN", "UPDATE-MAIN", "META npdm", "hello romfs", "top", "old"};
static const uint32_t children[] = {1, 2, 7, 12, 4, 5, 8, 9, 6, 11, 10, 0xFFFFFFFFu, 0xFFFFFFFFu};  // (+2 padding)
static const uint32_t dir_first[NDIRS] = {0, 2, 3, 4, 6, 8, 10}, dir_count[NDIRS] = {2, 1, 1, 2, 2, 2, 1};

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }

// The package's bytes; codec_of_a: the codec byte of romfs/menu/a.bin (1: stored compressed).
static uint8_t *build(size_t *len, uint8_t codec_of_a) {
    const size_t titles_at = 0xb0, dirs_at = titles_at + 2 * 0x40, files_at = dirs_at + 24 * NDIRS;
    const size_t kids_at = files_at + 80 * NFILES, names_at = kids_at + sizeof children;
    size_t names_size = 0, data_size = 0;
    for (int i = 0; i < NDIRS; i++) names_size += strlen(dir_names[i]);
    for (int i = 0; i < NFILES; i++) names_size += strlen(file_names[i]), data_size += 16;
    size_t data_at = (names_at + names_size + 15) & ~(size_t)15, total = data_at + data_size;
    uint8_t *b = calloc(1, total);
    memcpy(b, "NUP\x01", 4);
    put64(b + 0x38, data_at), put64(b + 0x40, total);
    memcpy(b + 0x80, "TOC\x01", 4);
    put32(b + 0x84, 2), put64(b + 0x88, NDIRS), put64(b + 0x90, NFILES);
    put64(b + 0x98, names_at - 0x80), put64(b + 0xa0, names_size);
    uint32_t title_dirs[2][3] = {{0, 1, 2}, {3, 4, 5}}, versions[2] = {0, 65536};
    for (int t = 0; t < 2; t++) {
        uint8_t *e = b + titles_at + 0x40 * t;
        put64(e, 0x0100ff500e34a000ull);
        put32(e + 8, title_dirs[t][0]), put32(e + 12, title_dirs[t][1]), put32(e + 16, title_dirs[t][2]), put32(e + 20, versions[t]);
    }
    size_t name = 0;
    for (int i = 0; i < NDIRS; i++) {
        uint8_t *e = b + dirs_at + 24 * i;
        size_t n = strlen(dir_names[i]);
        put32(e, (uint32_t)name), put32(e + 4, (uint32_t)n), put32(e + 8, dir_first[i]), put32(e + 12, dir_count[i]);
        memcpy(b + names_at + name, dir_names[i], n), name += n;
    }
    for (int i = 0; i < NFILES; i++) {
        uint8_t *e = b + files_at + 80 * i;
        size_t n = strlen(file_names[i]), size = strlen(file_data[i]), at = data_at + 16 * (size_t)i;
        put32(e, (uint32_t)name), put32(e + 4, (uint32_t)n);
        put64(e + 8, size), put64(e + 16, at), put64(e + 24, size);
        e[64] = i == 3 ? codec_of_a : 0;
        memcpy(b + names_at + name, file_names[i], n), name += n;
        memcpy(b + at, file_data[i], size);
    }
    for (size_t i = 0; i < sizeof children / sizeof *children; i++) put32(b + kids_at + 4 * i, children[i]);
    *len = total;
    return b;
}

static void write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); exit(2); }
    fwrite(data, 1, len, f);
    fclose(f);
}

static void write_package(const char *path, uint8_t codec_of_a, size_t cut) {
    size_t len;
    uint8_t *b = build(&len, codec_of_a);
    write_file(path, b, len - cut);
    free(b);
}

static bool open_package(void) {
    char why[512];
    return gf_init(PKG, why, sizeof why);
}

static void test_a_package_is_opened_as_one(void) {
    CHECK(open_package(), "gf_init failed");
    CHECK(gf_is_package(), "not seen as a package");
}

static void test_the_newest_titles_exefs_is_read(void) {
    open_package();
    size_t n = 0;
    uint8_t *d = gf_read(PKG "/exefs/main", &n);
    CHECK(d && n == 11 && !memcmp(d, "UPDATE-MAIN", 11), "exefs/main is not the update's");
    free(d);
}

static void test_a_romfs_file_is_read_in_place(void) {
    open_package();
    GfSource src;
    CHECK(gf_source(PKG "/romfs/menu/a.bin", &src), "romfs/menu/a.bin not found");
    CHECK(!strcmp(src.path, PKG) && src.size == 11 && src.base > 0, "not a range of the package");
    char buf[8] = {0};
    CHECK(gf_pread(&src, buf, 5, 6) == 5 && !memcmp(buf, "romfs", 5), "pread at an offset");
}

static void test_the_romfs_lists_folders_and_files(void) {
    open_package();
    int romfs = gf_pkg_node(PKG "/romfs");
    const uint32_t *kids;
    int n = gf_pkg_children(romfs, &kids);
    CHECK(n == 2, "romfs should hold menu/ and top.txt");
    CHECK(n == 2 && gf_pkg_is_dir((int)kids[0]) && !strcmp(gf_pkg_name((int)kids[0]), "menu"), "menu/ first");
    CHECK(n == 2 && !gf_pkg_is_dir((int)kids[1]) && !strcmp(gf_pkg_name((int)kids[1]), "top.txt"), "then top.txt");
}

static void test_the_base_games_romfs_is_not_used(void) {
    open_package();
    GfSource src;
    CHECK(!gf_source(PKG "/romfs/old.txt", &src), "the base title's romfs leaked into the update's");
}

static void test_a_path_outside_the_package_is_a_host_file(void) {
    open_package();
    write_file(OUT "/nup-mod.txt", "mod", 3);
    size_t n = 0;
    uint8_t *d = gf_read(OUT "/nup-mod.txt", &n);
    CHECK(d && n == 3 && !memcmp(d, "mod", 3), "a mod's file must be read from the disk");
    free(d);
}

static void test_a_compressed_package_is_refused(void) {
    write_package(BAD, 1, 0);
    char why[512] = "";
    CHECK(!gf_init(BAD, why, sizeof why), "a compressed file was accepted");
    CHECK(strstr(why, "compressed") != NULL, "the reason should say compressed");
}

static void test_an_incomplete_package_is_refused(void) {
    write_package(BAD, 0, 8);
    char why[512] = "";
    CHECK(!gf_init(BAD, why, sizeof why), "a cut package was accepted");
    CHECK(strstr(why, "incomplete") != NULL, "the reason should say incomplete");
}

static void test_a_folder_dump_reads_host_files(void) {
    mkdir(DIR_DUMP, 0755);
    mkdir(DIR_DUMP "/exefs", 0755);
    write_file(DIR_DUMP "/exefs/main", "FOLDER", 6);
    char why[512];
    CHECK(gf_init(DIR_DUMP, why, sizeof why) && !gf_is_package(), "a folder is not a package");
    size_t n = 0;
    uint8_t *d = gf_read(DIR_DUMP "/exefs/main", &n);
    CHECK(d && n == 6 && !memcmp(d, "FOLDER", 6), "exefs/main from the folder");
    free(d);
}

int main(void) {
    mkdir("build", 0755);
    mkdir(OUT, 0755);
    write_package(PKG, 0, 0);
    test_a_package_is_opened_as_one();
    test_the_newest_titles_exefs_is_read();
    test_a_romfs_file_is_read_in_place();
    test_the_romfs_lists_folders_and_files();
    test_the_base_games_romfs_is_not_used();
    test_a_path_outside_the_package_is_a_host_file();
    test_a_compressed_package_is_refused();
    test_an_incomplete_package_is_refused();
    test_a_folder_dump_reads_host_files();
    printf(failures ? "test_nup_package: %d failed\n" : "test_nup_package: ok\n", failures);
    return failures != 0;
}
