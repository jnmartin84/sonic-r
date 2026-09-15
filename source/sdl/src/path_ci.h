#ifndef SONICR_PATH_CI_H
#define SONICR_PATH_CI_H

#include <stdio.h>

/* Case-insensitive file access for case-sensitive platforms.
 *
 * The original PC data ships in mixed case; all game paths are built
 * UPPERCASE (see sonicr_paths.h). On case-sensitive filesystems (Linux,
 * and macOS set to case-sensitive) an exact-case fopen fails for data
 * copied straight from a Windows CD-ROM. These helpers fall back to a
 * directory scan that matches names case-insensitively so a mixed/lowercase
 * data folder still loads. The Dreamcast build compiles path_ci.c to an
 * empty unit: its ISO9660 disc is always uppercase and saves route through
 * the VMU-aware sr_fOpen shim. */

/* Rebuild 'path' with the real casing present on disk, resolving each
 * segment against the directory entries of the previous one. Returns a
 * malloc'd string the caller must free, or NULL when no case-insensitive
 * match exists. */
char *sr_resolve_case(const char *path);

/* fopen(path, mode) with a case-insensitive read fallback. Direct opens are
 * tried first; only a failed READ (never a write — that would silently
 * redirect the write to a differently-cased file) triggers a lookup. */
FILE *sr_fOpenCI(const char *path, const char *mode);

#endif /* SONICR_PATH_CI_H */