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


/* fopen wrapper: relative paths ("BIN/OPTION/X.RAW", "SAVE/R01.SAV",
 * "SONICR.INF") are taken as relative to D:\\, and '/' becomes '\\'. */
#include <string.h>
FILE *xb_fopen(const char *path, const char *mode)
{
    char buf[512];
    size_t n = 0;
    if (!path) return NULL;
    while (path[0] == '.' && (path[1] == '/' || path[1] == '\\')) path += 2;
    if (!strchr(path, ':')) {
        memcpy(buf, "D:\\", 3);
        n = 3;
    }
    for (; *path && n < sizeof(buf) - 1; path++)
        buf[n++] = (*path == '/') ? '\\' : *path;
    buf[n] = 0;
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
    xbox_install_exception_handler();
    xbox_log_mem("start");
    sonicr_main(1, argv);
    XReboot();
    return 0;
}
