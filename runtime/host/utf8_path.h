// [XCDE] T277: paths are UTF-8 everywhere in this project (SDL_GetBasePath, argv, the settings files), but Windows'
// ANSI (…A) APIs and the CRT read narrow path strings in the system code page. Under "C:\Users\João\" the bytes for
// 'ã' (C3 A3 in UTF-8) were taken as CP1252 "JoÃ£o", a folder that does not exist: CreateProcessA failed with
// error 267 and fopen() could not find xcde.ini either (GitHub #218). These wrappers convert the path to UTF-16, the
// encoding Windows stores paths in, and call the wide API; on other systems they are the libc call they replace.
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>  // _wrename (in stdio.h only on some CRTs; _wfopen/_wremove are there too)

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// UTF-16 code units for one path: MAX_PATH is 260 and the wide Win32 APIs accept longer paths than the narrow ones.
#define XCDE_PATH_W 1024

// UTF-8 -> UTF-16 into out (cap code units, terminator included). Returns the units written without the terminator,
// or -1 when it does not fit. out == NULL with cap == 0 returns the needed length (as snprintf does). Bytes that are
// not UTF-8 become U+FFFD, as MultiByteToWideChar(CP_UTF8) defines.
static inline int xcde_utf8_to_wide(const char *s, wchar_t *out, size_t cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap);
    return n > 0 ? n - 1 : -1;
}
// The reverse: UTF-16 -> UTF-8 (the file names a wide directory listing returns).
static inline int xcde_utf8_from_wide(const wchar_t *s, char *out, size_t cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, out, (int)cap, NULL, NULL);
    return n > 0 ? n - 1 : -1;
}

// fopen() / remove() / rename() with a UTF-8 path (same returns as the CRT's).
static inline FILE *xcde_utf8_fopen(const char *path, const char *mode) {
    wchar_t wpath[XCDE_PATH_W], wmode[8];
    size_t i;
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return NULL;
    for (i = 0; mode[i] && i + 1 < sizeof wmode / sizeof wmode[0]; i++) wmode[i] = (wchar_t)(unsigned char)mode[i];
    wmode[i] = 0;
    return _wfopen(wpath, wmode);
}
static inline int xcde_utf8_remove(const char *path) {
    wchar_t wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return -1;
    return _wremove(wpath);
}
static inline int xcde_utf8_rename(const char *from, const char *to) {
    wchar_t wfrom[XCDE_PATH_W], wto[XCDE_PATH_W];
    if (xcde_utf8_to_wide(from, wfrom, XCDE_PATH_W) < 0 || xcde_utf8_to_wide(to, wto, XCDE_PATH_W) < 0) return -1;
    return _wrename(wfrom, wto);
}
// [XCDE] T247: the same-directory replacement, 0 when the new file took the old one's place.
static inline int xcde_utf8_replace(const char *tmp, const char *path) {
    wchar_t wtmp[XCDE_PATH_W], wpath[XCDE_PATH_W];
    if (xcde_utf8_to_wide(tmp, wtmp, XCDE_PATH_W) < 0 || xcde_utf8_to_wide(path, wpath, XCDE_PATH_W) < 0) return -1;
    return MoveFileExW(wtmp, wpath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}
#else
static inline FILE *xcde_utf8_fopen(const char *path, const char *mode) { return fopen(path, mode); }
static inline int xcde_utf8_remove(const char *path) { return remove(path); }
static inline int xcde_utf8_rename(const char *from, const char *to) { return rename(from, to); }
static inline int xcde_utf8_replace(const char *tmp, const char *path) { return rename(tmp, path); }
#endif
