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
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

extern int sonicr_main(int argc, char **argv);

/* Log free physical memory, for tracking down out-of-memory failures. */
void xbox_log_mem(const char *where)
{
    MM_STATISTICS st;
    st.Length = sizeof(st);
    if (NT_SUCCESS(MmQueryStatistics(&st))) {
        fprintf(stderr, "mem[%s]: free %lu KB of %lu KB, heap committed %lu KB\n", where,
                (unsigned long)st.AvailablePages * 4UL,
                (unsigned long)st.TotalPhysicalPages * 4UL,
                (unsigned long)st.VirtualMemoryBytesCommitted / 1024UL);
    }
}


/* ------------------------------------------------------------------ */
/* Saves in E:\UDATA\<TitleID>\ like a retail Xbox game                 */
/* ------------------------------------------------------------------ */
#include <string.h>
#include <ctype.h>
#include <nxdk/mount.h>
#include "xbox_udata_assets.h"

#define UD_SAVES    "000000000001"   /* SAVE\R01..R08.SAV */
#define UD_SETTINGS "000000000002"   /* SONICR.INF, JOYSTICK.INF */
#define UD_GHOSTS   "000000000003"   /* GHOST\... */

static char s_ud[48];      /* "E:\UDATA\53520001" or "" when E: is unavailable */

static void xb_mkdirs(const char *path)
{
    char tmp[512];
    size_t n = strlen(path);
    if (n >= sizeof(tmp)) return;
    memcpy(tmp, path, n + 1);
    for (size_t i = 3; i < n; i++) {
        if (tmp[i] == '\\') {
            tmp[i] = 0;
            CreateDirectoryA(tmp, NULL);
            tmp[i] = '\\';
        }
    }
    CreateDirectoryA(tmp, NULL);
}

static int xb_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void xb_write_always(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, len, f);
    fclose(f);
}

static void xb_write_if_missing(const char *path, const void *data, size_t len)
{
    if (xb_exists(path)) return;
    xb_write_always(path, data, len);
}

/* UTF-16LE text file with BOM, as the dashboard expects for *.xbx metadata. */
static void xb_write_meta(const char *path, const char *text)
{
    unsigned char buf[256];
    size_t n = 0;
    if (xb_exists(path)) return;
    buf[n++] = 0xFF; buf[n++] = 0xFE;
    for (; *text && n < sizeof(buf) - 2; text++) { buf[n++] = (unsigned char)*text; buf[n++] = 0; }
    xb_write_if_missing(path, buf, n);
}

static void xb_copy_if_missing(const char *src, const char *dst)
{
    if (xb_exists(dst) || !xb_exists(src)) return;
    FILE *in = fopen(src, "rb");
    if (!in) return;
    FILE *out = fopen(dst, "wb");
    if (out) {
        char b[4096];
        size_t k;
        while ((k = fread(b, 1, sizeof(b), in)) > 0) fwrite(b, 1, k, out);
        fclose(out);
    }
    fclose(in);
}

static void xb_make_save_folder(const char *id, const char *name)
{
    char p[160], meta[96];
    snprintf(p, sizeof(p), "%s\\%s", s_ud, id);
    xb_mkdirs(p);
    snprintf(meta, sizeof(meta), "Name=%s\r\n", name);
    snprintf(p, sizeof(p), "%s\\%s\\SaveMeta.xbx", s_ud, id);
    xb_write_meta(p, meta);
    snprintf(p, sizeof(p), "%s\\%s\\saveimage.xbx", s_ud, id);
    xb_write_always(p, xb_saveimage_xbx, sizeof(xb_saveimage_xbx));
}

/* TitleMeta.xbx is UTF-16LE; true if it already names this game. */
static int xb_folder_is_ours(const char *folder)
{
    char p[160];
    unsigned char b[256];
    snprintf(p, sizeof(p), "%s\\TitleMeta.xbx", folder);
    if (!xb_exists(p)) return 1;                 /* unused folder: free to take */
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    size_t n = fread(b, 1, sizeof(b), f);
    fclose(f);
    static const char name[] = "Sonic R";
    for (size_t i = 0; i + 2 * (sizeof(name) - 1) <= n; i++) {
        size_t k = 0;
        while (k < sizeof(name) - 1 && b[i + 2 * k] == (unsigned char)name[k] && b[i + 2 * k + 1] == 0) k++;
        if (k == sizeof(name) - 1) return 1;
    }
    return 0;                                    /* belongs to another title */
}

static void xb_udata_init(void)
{
    char p[160], q[160], old[48];
    unsigned long tid = (unsigned long)CURRENT_XBE_HEADER->CertificateHeader->TitleID;

    s_ud[0] = 0;
    if (!nxIsDriveMounted('E') && !nxMountDrive('E', "\\Device\\Harddisk0\\Partition1")) {
        fprintf(stderr, "UDATA: cannot mount E:, saving next to the game\n");
        return;
    }
    /* Never share a folder with another game: if the folder for our title ID is
     * already used by a different title, move on to the next free ID. */
    for (int k = 0; k < 32; k++) {
        snprintf(s_ud, sizeof(s_ud), "E:\\UDATA\\%08lX", (tid + (unsigned long)k) & 0xFFFFFFFFUL);
        if (xb_folder_is_ours(s_ud)) break;
        s_ud[0] = 0;
    }
    if (!s_ud[0]) {
        fprintf(stderr, "UDATA: no free title folder, saving next to the game\n");
        return;
    }
    xb_mkdirs(s_ud);
    if (!xb_exists(s_ud)) {
        fprintf(stderr, "UDATA: cannot create %s, saving next to the game\n", s_ud);
        s_ud[0] = 0;
        return;
    }

    snprintf(p, sizeof(p), "%s\\TitleMeta.xbx", s_ud);
    xb_write_meta(p, "TitleName=Sonic R\r\n");
    snprintf(p, sizeof(p), "%s\\TitleImage.xbx", s_ud);
    xb_write_always(p, xb_titleimage_xbx, sizeof(xb_titleimage_xbx));

    xb_make_save_folder(UD_SAVES, "Sonic R Race Saves");
    xb_make_save_folder(UD_SETTINGS, "Sonic R Settings");
    xb_make_save_folder(UD_GHOSTS, "Sonic R Ghosts");

    /* One-time import of saves made by earlier builds next to the game. */
    for (int i = 1; i <= 8; i++) {
        snprintf(p, sizeof(p), "D:\\SAVE\\R0%d.SAV", i);
        snprintf(q, sizeof(q), "%s\\%s\\SAVE\\R0%d.SAV", s_ud, UD_SAVES, i);
        if (xb_exists(p) && !xb_exists(q)) {
            snprintf(q, sizeof(q), "%s\\%s\\SAVE", s_ud, UD_SAVES);
            xb_mkdirs(q);
            snprintf(q, sizeof(q), "%s\\%s\\SAVE\\R0%d.SAV", s_ud, UD_SAVES, i);
            xb_copy_if_missing(p, q);
        }
    }
    snprintf(q, sizeof(q), "%s\\%s\\SONICR.INF", s_ud, UD_SETTINGS);
    xb_copy_if_missing("D:\\SONICR.INF", q);
    snprintf(q, sizeof(q), "%s\\%s\\JOYSTICK.INF", s_ud, UD_SETTINGS);
    xb_copy_if_missing("D:\\JOYSTICK.INF", q);
    /* Import from the 53520001 folder used by an earlier test build. */
    snprintf(old, sizeof(old), "E:\\UDATA\\53520001");
    if (xb_exists(old) && strcmp(old, s_ud)) {
        for (int i = 1; i <= 8; i++) {
            snprintf(p, sizeof(p), "%s\\%s\\SAVE\\R0%d.SAV", old, UD_SAVES, i);
            snprintf(q, sizeof(q), "%s\\%s\\SAVE", s_ud, UD_SAVES);
            xb_mkdirs(q);
            snprintf(q, sizeof(q), "%s\\%s\\SAVE\\R0%d.SAV", s_ud, UD_SAVES, i);
            xb_copy_if_missing(p, q);
        }
        snprintf(p, sizeof(p), "%s\\%s\\SONICR.INF", old, UD_SETTINGS);
        snprintf(q, sizeof(q), "%s\\%s\\SONICR.INF", s_ud, UD_SETTINGS);
        xb_copy_if_missing(p, q);
        snprintf(p, sizeof(p), "%s\\%s\\JOYSTICK.INF", old, UD_SETTINGS);
        snprintf(q, sizeof(q), "%s\\%s\\JOYSTICK.INF", s_ud, UD_SETTINGS);
        xb_copy_if_missing(p, q);
    }
    fprintf(stderr, "UDATA: saving to %s\n", s_ud);
}

/* fopen wrapper: relative paths ("BIN/OPTION/X.RAW") are taken as relative to
 * D:\, '/' becomes '\'. Save data (SAVE\, SONICR.INF, JOYSTICK.INF, GHOST\)
 * goes to E:\UDATA\<TitleID>\<folder>\ so the dashboard lists and manages it.
 * ONLINE.DAT (the online sign-in token) is kept with the settings. */
#include "xbox_net00_patch.h"

/* The network screens' key hints (F1, F2, Esc ...) are painted into
 * BIN\OPTION\NET00.RAW. Serve a copy with Xbox button names instead: read the
 * original, apply the pixel runs from tools/make_net00_patch.py, write it to
 * the title's UDATA folder and open that. The game data is left untouched. */
static FILE *xb_open_net00(const char *orig)
{
    static unsigned char buf[256 * 256 * 3];
    char out[96];
    if (!s_ud[0]) return NULL;
    FILE *f = fopen(orig, "rb");
    if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n != sizeof(buf)) return NULL;
    const unsigned char *px = xb_net00_pixels;
    for (int i = 0; i < XB_NET00_RUNS; i++) {
        size_t off = ((size_t)xb_net00_runs[i][1] * 256 + xb_net00_runs[i][0]) * 3;
        size_t len = (size_t)xb_net00_runs[i][2] * 3;
        memcpy(buf + off, px, len);
        px += len;
    }
    snprintf(out, sizeof(out), "%s\\NET00X.RAW", s_ud);
    f = fopen(out, "wb");
    if (!f) return NULL;
    fwrite(buf, 1, sizeof(buf), f);
    fclose(f);
    return fopen(out, "rb");
}

FILE *xb_fopen(const char *path, const char *mode)
{
    char rel[400], buf[560];
    size_t n = 0;
    if (!path) return NULL;
    while (path[0] == '.' && (path[1] == '/' || path[1] == '\\')) path += 2;
    if ((path[0] == 'D' || path[0] == 'd') && path[1] == ':') {
        path += 2;
        while (*path == '/' || *path == '\\') path++;
    }
    for (; *path && n < sizeof(rel) - 1; path++)
        rel[n++] = (*path == '/') ? '\\' : *path;
    rel[n] = 0;

    if (!strchr(rel, ':')) {
        const char *folder = NULL;
        if (s_ud[0]) {
            if (!_strnicmp(rel, "SAVE\\", 5))          folder = UD_SAVES;
            else if (!_strnicmp(rel, "GHOST\\", 6))    folder = UD_GHOSTS;
            else if (!_stricmp(rel, "SONICR.INF") || !_stricmp(rel, "JOYSTICK.INF") ||
                     !_stricmp(rel, "ONLINE.DAT"))   /* online sign-in token */
                                                       folder = UD_SETTINGS;
        }
        if (folder) snprintf(buf, sizeof(buf), "%s\\%s\\%s", s_ud, folder, rel);
        else        snprintf(buf, sizeof(buf), "D:\\%s", rel);
    } else {
        snprintf(buf, sizeof(buf), "%s", rel);
    }
    if (mode && mode[0] == 'r' && !_stricmp(rel, "BIN\\OPTION\\NET00.RAW")) {
        FILE *pf = xb_open_net00(buf);
        if (pf) return pf;
    }
    if (mode && (strchr(mode, 'w') || strchr(mode, 'a'))) {
        char *sl = strrchr(buf, '\\');
        if (sl) { *sl = 0; xb_mkdirs(buf); *sl = '\\'; }
    }
    return fopen(buf, mode);
}

/* Last-chance logger for CPU exceptions (divide by zero, bad pointers, ...).
 * The kernel walks the handler chain at fs:[0]; this one logs the fault and the
 * code addresses found on the stack, then lets the kernel carry on (which hangs
 * or reboots). Addresses can be matched against source/xbox/main.map. */
static EXCEPTION_REGISTRATION s_excReg;

static EXCEPTION_DISPOSITION __cdecl xbox_exc_handler(EXCEPTION_RECORD *rec, void *frame,
                                                      CONTEXT *ctx, void *dispatcher)
{
    (void)frame;
    (void)dispatcher;
    fprintf(stderr, "EXCEPTION code=0x%08lx at=%p flags=0x%lx params=%lu\n",
            (unsigned long)rec->ExceptionCode, rec->ExceptionAddress,
            (unsigned long)rec->ExceptionFlags, (unsigned long)rec->NumberParameters);
    if (rec->NumberParameters >= 2) {
        fprintf(stderr, "  info0=0x%lx info1=0x%lx (access violation: 0=read 1=write, address)\n",
                (unsigned long)rec->ExceptionInformation[0], (unsigned long)rec->ExceptionInformation[1]);
    }
    if (ctx) {
        fprintf(stderr, "  eip=%08lx esp=%08lx ebp=%08lx eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n",
                (unsigned long)ctx->Eip, (unsigned long)ctx->Esp, (unsigned long)ctx->Ebp,
                (unsigned long)ctx->Eax, (unsigned long)ctx->Ebx, (unsigned long)ctx->Ecx,
                (unsigned long)ctx->Edx, (unsigned long)ctx->Esi, (unsigned long)ctx->Edi);
        const unsigned long *sp = (const unsigned long *)ctx->Esp;
        int shown = 0;
        for (int i = 0; i < 1024 && shown < 48; i++) {
            unsigned long v = sp[i];
            if (v >= 0x11000UL && v < 0x180000UL) {   /* inside .text of the XBE */
                fprintf(stderr, "  stack+%d: %08lx\n", i * 4, v);
                shown++;
            }
        }
    }
    fflush(stderr);
    return ExceptionContinueSearch;
}

static void xbox_install_exception_handler(void)
{
    unsigned long prev;
    __asm__ __volatile__("movl %%fs:0, %0" : "=r"(prev));
    s_excReg.prev = (struct _EXCEPTION_REGISTRATION *)prev;
    s_excReg.handler = (void *)xbox_exc_handler;
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(&s_excReg));
}

/* Put the AC97 controller back to an idle state before the reboot: stop and
 * reset both bus-master channels (analog + S/PDIF), mask/clear their
 * interrupts and drop the descriptor-list pointers, so no DMA/IRQ is left
 * pointing at memory that is about to disappear. */
static void xb_ac97_shutdown(void)
{
    volatile unsigned char *pb = (volatile unsigned char *)0xfec00000;
    volatile unsigned int *pw = (volatile unsigned int *)0xfec00000;
    const int crOff[2] = { 0x11B, 0x17B };   /* control registers */
    const int srOff[2] = { 0x116, 0x176 };   /* status registers */
    const int bdOff[2] = { 0x110, 0x170 };   /* descriptor list base addresses */
    for (int i = 0; i < 2; i++) {
        volatile unsigned char *cr = pb + crOff[i];
        *cr = 0;                                  /* stop, no interrupts */
        for (int t = 0; t < 100000 && (*cr & 1); t++) { }
        *cr = 2;                                  /* reset the channel registers */
        for (int t = 0; t < 100000 && (*cr & 2); t++) { }
        *cr = 0;
        pb[srOff[i]] = 0xFF;                      /* clear pending status */
        pw[bdOff[i] >> 2] = 0;                    /* forget the descriptor list */
    }
    fprintf(stderr, "exit: audio hardware idle\n");
}

/* The game leaves through exit(0) (shutdown.c, SDL_QUIT), which never returns
 * to main(). nxdk's exit() ends in a *quick* reboot that does not reset the
 * audio hardware, leaving the console silent until a power cycle. So hook exit:
 * idle the AC97, then do a full reset (the same as "restart" from the dashboard). */
static void xb_exit_cleanup(void)
{
    fprintf(stderr, "exit: idling audio, full reboot\n");
    fflush(stderr);
    xb_ac97_shutdown();
    SDL_Delay(200);
    XReboot();   /* HalRebootRoutine: full SMC reset */
    for (;;) { }
}

int main(void)
{
    static char *argv[] = { "SONICR.XBE", 0 };
    /* The Xbox has no console: send stderr to D:\SONICR.LOG. Fully buffered:
     * every unbuffered write to the HDD stalled the game for a frame or more.
     * The buffer is flushed at natural pauses (slow/loading frames), by the
     * watchdog, the exception handler and on exit. If D: is read-only this is
     * silently skipped. */
    if (freopen("D:\\SONICR.LOG", "w", stderr)) {
        setvbuf(stderr, NULL, _IOFBF, 64 * 1024);
        fprintf(stderr, "Sonic R Xbox: started, build %s %s\n", __DATE__, __TIME__);
    }
    xb_udata_init();
    atexit(xb_exit_cleanup);
    xbox_install_exception_handler();
    xbox_log_mem("start");
    sonicr_main(1, argv);
    /* Leave cleanly: flush the log, stop the audio hardware, then hand control
     * back to the dashboard (rebooting with the AC97 still running left the
     * dashboard with crackling sound / a muted game on the next start). */
    fprintf(stderr, "exit: returning to dashboard\n");
    fflush(stderr);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    xb_ac97_shutdown();
    SDL_Delay(200);
    /* A full reboot lands on the dashboard on every setup; XLaunchXBE(NULL)
     * (quick reboot) left a black screen on this console. */
    XReboot();
    return 0;
}
