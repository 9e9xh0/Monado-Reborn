// The game's own files (exefs/, romfs/): an extracted folder or a .nup package (runtime/services/game_files.c).
// [XCDE] T280
//
// Every reader keeps building the same paths, "<root>/exefs/main" or "<root>/romfs/menu/...", where <root> is what
// the player picked. With a folder those are host files; with a .nup they are files inside the package, read in place.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Bytes [base, base + size) of the host file at path: the file itself (base 0), or its place inside a package.
typedef struct {
    const char *path;
    uint64_t base, size;
} GfSource;

// root: a folder holding exefs/ and romfs/, or a .nup file. A package that can't be used fails with the reason.
// Calling it again with the same root does nothing.
bool gf_init(const char *root, char *why, size_t why_size);
bool gf_is_package(void);

// Where a game path's bytes are. A path outside a package (a mod, a folder dump) is the host file. false: missing.
bool gf_source(const char *path, GfSource *out);
// The whole file, malloc'd (NULL when missing or unreadable).
uint8_t *gf_read(const char *path, size_t *len);
// n bytes at off of a source; short only at its end or on a read error.
int64_t gf_pread(const GfSource *src, void *dst, uint64_t n, uint64_t off);
// A descriptor on the source's host file, for many reads (gf_pread_at at src->base + offset); -1: can't be opened.
int gf_open(const GfSource *src);
// n bytes at a 64-bit offset of a descriptor (pread with a 32-bit off_t, as the Windows build has, stops at 2 GB).
// Short only at the end or on a read error; -1 when nothing could be read.
int64_t gf_pread_at(int fd, void *dst, uint64_t n, uint64_t off);

// The package's tree, for listing it (romfs_virtual.c, precompile.c). Nodes are dirs or files; -1: none.
int gf_pkg_node(const char *path);
int gf_pkg_children(int node, const uint32_t **children);
bool gf_pkg_is_dir(int node);
const char *gf_pkg_name(int node);
GfSource gf_pkg_source(int node);
