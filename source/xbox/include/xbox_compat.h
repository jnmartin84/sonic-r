/*
 * xbox_compat.h - force-included into every Sonic R translation unit on nxdk.
 *
 * nxdk ships a small libc (PDCLib) without some POSIX bits the desktop port
 * assumes. Keep this file tiny: add a shim only when the compiler asks for it.
 */
#ifndef XBOX_COMPAT_H
#define XBOX_COMPAT_H

#include <stddef.h>

/* nxdk compiles for a win32 target and so predefines _WIN32, but this is not
 * Windows: the game's Windows branches expect the real <windows.h>/<dinput.h>.
 * Pull in SDL first (it keys its platform detection off _WIN32), then drop the
 * macro so the game's own code takes the portable branches. */
#include <SDL.h>
#undef _WIN32
#undef WIN32

/* pbgl's headers: GL 1.x entry points plus the ARB combine/packed-pixel tokens
 * the GL backend uses. */
#include <GL/gl.h>
#include <GL/glext.h>

/* usleep() for the (stubbed) network receive loop. */
static inline int usleep(unsigned int us)
{
    SDL_Delay(us / 1000u);
    return 0;
}

/* The game data lives next to default.xbe, which the kernel maps to D:, so
 * there is no directory to change into. */
static inline int chdir(const char *path) { (void)path; return 0; }

#endif /* XBOX_COMPAT_H */
