/**
 * path_ci.c — case-insensitive file open for case-sensitive platforms.
 *
 * The game builds every asset path in ALL CAPS (sonicr_paths.h) to match the
 * original data layout, but a data folder copied from a Windows install keeps
 * whatever casing the CD shipped with. On Linux (and case-sensitive macOS
 * volumes) that mix no longer resolves. These helpers reproduce the
 * case-insensitive behaviour those platforms get for free: when an exact-case
 * open fails, each path segment is matched against the parent directory's
 * entries case-insensitively and the real on-disk name is used.
 */
#include "path_ci.h"

#ifndef SONICR_DC

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

/* ASCII-only case-insensitive compare. Game data filenames are plain ASCII,
 * so no locale or Unicode case-folding is needed. */
static int sr_ascii_casecmp(const char *a, const char *b)
{
    for (;;) {
        int ca = (unsigned char)*a++;
        int cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') {
            ca += 'a' - 'A';
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 'a' - 'A';
        }
        if (ca != cb || ca == '\0') {
            return ca - cb;
        }
    }
}

/* Find the real on-disk name of 'segment' inside 'dir', matched
 * case-insensitively. Returns a malloc'd string, or NULL on an exact
 * case-sensitive match only (callers already know that path doesn't open). */
static char *sr_find_ci_dir_entry(const char *dir, const char *segment)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        return NULL;
    }

    struct dirent *entry;
    char *match = NULL;
    while ((entry = readdir(d)) != NULL) {
        if (sr_ascii_casecmp(entry->d_name, segment) == 0) {
            match = strdup(entry->d_name);
            break;
        }
    }
    closedir(d);
    return match;
}

struct sbuf {
    char *data;
    size_t len;
    size_t cap;
};

static int sbuf_push(struct sbuf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 64;
        while (ncap < b->len + n + 1) {
            ncap *= 2;
        }
        char *nd = realloc(b->data, ncap);
        if (nd == NULL) {
            return -1;
        }
        b->data = nd;
        b->cap = ncap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

char *sr_resolve_case(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    struct sbuf out = { NULL, 0, 0 };
    const char *scanDir;
    const char *cursor = path;

    if (path[0] == '/') {
        if (sbuf_push(&out, "/", 1) != 0) {
            return NULL;
        }
        scanDir = out.data;
        cursor = path + 1;
    }
    else {
        scanDir = ".";
        cursor = path;
    }

    for (;;) {
        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        const char *seg = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        size_t segLen = (size_t)(cursor - seg);
        if (segLen == 0) {
            break;
        }

        char *segStr = malloc(segLen + 1);
        if (segStr == NULL) {
            free(out.data);
            return NULL;
        }
        memcpy(segStr, seg, segLen);
        segStr[segLen] = '\0';

        char *real = sr_find_ci_dir_entry(scanDir, segStr);
        free(segStr);
        if (real == NULL) {
            free(out.data);
            return NULL;
        }

        int needSep = out.len > 0 && out.data[out.len - 1] != '/';
        if ((needSep && sbuf_push(&out, "/", 1) != 0) ||
            sbuf_push(&out, real, strlen(real)) != 0) {
            free(real);
            free(out.data);
            return NULL;
        }
        scanDir = out.data;
        free(real);
    }

    if (out.len == 0) {
        free(out.data);
        return NULL;
    }
    return out.data;
}

FILE *sr_fOpenCI(const char *path, const char *mode)
{
    FILE *fp = fopen(path, mode);
    if (fp != NULL) {
        return fp;
    }

    /* Case-insensitive fallback for reads only. Resolving the real case of a
     * path used for writing would redirect the write to a differently-cased
     * existing file, or to a name the directory listing can't confirm. */
    if (mode == NULL || mode[0] != 'r' || mode[1] == '+') {
        return NULL;
    }

    char *resolved = sr_resolve_case(path);
    if (resolved == NULL) {
        return NULL;
    }
    fp = fopen(resolved, mode);
    free(resolved);
    return fp;
}

#else /* SONICR_DC */

/* The Dreamcast disc is laid out UPPERCASE per ISO9660 (uppercase_tree.sh)
 * and saves route through the VMU-aware sr_fOpen shim in fileio.h, so there
 * is no case-folding to do here — this compiles to an empty translation unit. */

#endif /* SONICR_DC */