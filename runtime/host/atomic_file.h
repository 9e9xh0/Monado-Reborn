// [XCDE] T247: same-directory replacement keeps the previous file on any failure.
// [XCDE] T277: the paths are UTF-8; utf8_path.h replaces the file with the wide API on Windows (GitHub #218).
#pragma once
#include "utf8_path.h"

static inline int xcde_atomic_replace(const char *tmp, const char *path) { return xcde_utf8_replace(tmp, path) == 0; }
