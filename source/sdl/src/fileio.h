#ifndef SONICR_FILEIO_H
#define SONICR_FILEIO_H

#include <stdio.h>

#ifdef SONICR_DC
#include <kos.h>

/* extern mutex_t io_lock; */

/* Save files (SONICR.INF, JOYSTICK.INF, SAVE/R0N.SAV) route to the VMU;
 * all other paths fall through to fopen with the /pc dcload fallback.
 * Implemented in dc/src/save_vmu.c. */
FILE  *sr_fOpen(const char *path, const char *mode);
size_t sr_fRead(void *buffer, size_t elementSize, size_t elementCount, FILE *file);
size_t sr_fWrite(const void *buffer, size_t elementSize, size_t elementCount, FILE *file);
int    sr_fClose(FILE *file);
int    sr_fSeek(FILE *file, long offset, int whence);
long   sr_fTell(FILE *file);
int    sr_fError(FILE *file);

#define fOpen(path, mode)                               sr_fOpen((path), (mode))
#define fRead(buffer, elementSize, elementCount, file)  sr_fRead((buffer), (elementSize), (elementCount), (file))
#define fSeek(file, offset, whence)                     sr_fSeek((file), (offset), (whence))
#define fTell(file)                                     sr_fTell(file)
#define fClose(file)                                    sr_fClose(file)
#define fWrite(buffer, elementSize, elementCount, file) sr_fWrite((buffer), (elementSize), (elementCount), (file))
#define fError(file)                                    sr_fError(file)
#else
/* Desktop read opens fall back to a case-insensitive lookup when the exact
 * (UPPERCASE) name isn't on disk, so mixed-case data copied from a Windows
 * CD-ROM loads on case-sensitive filesystems. Writes keep the canonical
 * names. See path_ci.c. */
#include "path_ci.h"
#define fOpen(path, mode)                               sr_fOpenCI((path), (mode))
#define fRead(buffer, elementSize, elementCount, file)  fread(buffer, elementSize, elementCount, file)
#define fSeek(file, offset, whence)                     fseek(file, offset, whence)
#define fTell(file)                                     ftell(file)
#define fClose(file)                                    fclose(file)
#define fWrite(buffer, elementSize, elementCount, file) fwrite(buffer, elementSize, elementCount, file)
#define fError(file)                                    ferror(file)
#endif

#endif
