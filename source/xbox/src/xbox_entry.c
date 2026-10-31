/*
 * xbox_entry.c - nxdk entry point for Sonic R.
 *
 * nxdk calls main() without arguments, so the game's own main() is renamed to
 * sonicr_main (see the Makefile) and invoked here with a fixed argv. When the
 * game returns we reboot to the dashboard instead of falling off the end.
 *
 * The desktop port expects SAVE and GHOST to exist (or be creatable) next to
 * the data. nothing in the game creates them, so create them here. Failure is
 * harmless: it just means D: is read-only (for example a disc image), and the
 * game then cannot save.
 */
#include <hal/xbox.h>
#include <stdio.h>
#include <windows.h>

extern int sonicr_main(int argc, char **argv);

int main(void)
{
    static char *argv[] = { "SONICR.XBE", 0 };
    CreateDirectoryA("D:\\SAVE", NULL);
    CreateDirectoryA("D:\\GHOST", NULL);
    /* The Xbox has no console: send stderr to D:\SONICR.LOG, unbuffered so the
     * last lines survive a crash. If D: is read-only this is silently skipped. */
    if (freopen("D:\\SONICR.LOG", "w", stderr)) {
        setvbuf(stderr, NULL, _IONBF, 0);
        fprintf(stderr, "Sonic R Xbox: started\n");
    }
    sonicr_main(1, argv);
    XReboot();
    return 0;
}
