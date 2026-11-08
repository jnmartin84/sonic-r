/* sonicr_music — prepares the Sonic R soundtrack for the Xbox port.
 *
 * The PC game (1998) plays its music as CD audio tracks, which are not files.
 * The Xbox port plays MUSIC\trackN.son: headerless 44100 Hz, 16-bit, stereo,
 * little-endian PCM — the exact layout of CD audio. This tool produces those
 * files two ways:
 *
 *   1. Straight from the original Sonic R CD (Windows): every audio track is
 *      read digitally from the disc and written as MUSIC\trackN.son, where N
 *      is the CD track number (the numbers the game asks for).
 *   2. From WAV files you already ripped: any PCM or float WAV (8/16/24/32-bit,
 *      mono or stereo, any sample rate) is converted to 44100 Hz 16-bit
 *      stereo. The track number is taken from the file name ("track02.wav",
 *      "Sonic R - 05.wav", "07 Can You Feel the Sunshine.wav" ...).
 *
 * Usage:
 *   sonicr_music                 rip from the first CD drive with audio tracks
 *   sonicr_music E:              rip from drive E:
 *   sonicr_music file.wav ...    convert WAV files (or drag them onto the exe)
 *   sonicr_music folder          convert every .wav in a folder
 *   -o DIR                       output folder (default: MUSIC next to the tool
 *                                when ripping, MUSIC next to the WAVs otherwise)
 *
 * Copy the MUSIC folder next to default.xbe on the Xbox.
 *
 * Builds as a 32-bit Windows console program (works on Windows XP..11); the
 * WAV conversion part also builds on Linux/macOS with any C compiler.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define SEP '\\'
#else
#include <sys/stat.h>
#include <dirent.h>
#define MKDIR(p) mkdir(p, 0755)
#define SEP '/'
#endif

static int g_pause = 0;   /* keep the console window open when started by double click / drag */

static void finish(int code)
{
    if (g_pause) {
        printf("\nPress Enter to close.\n");
        getchar();
    }
    exit(code);
}

static void join(char *out, size_t n, const char *dir, const char *name)
{
    size_t l = strlen(dir);
    if (l && (dir[l - 1] == '\\' || dir[l - 1] == '/'))
        snprintf(out, n, "%s%s", dir, name);
    else
        snprintf(out, n, "%s%c%s", dir, SEP, name);
}

/* ------------------------------------------------------------------ */
/* WAV -> .son                                                         */
/* ------------------------------------------------------------------ */

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Track number from a file name: the first run of digits, 1..99. */
static int track_from_name(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;
    for (const char *p = base; *p; p++) {
        if (isdigit((unsigned char)*p)) {
            int n = atoi(p);
            if (n >= 1 && n <= 99) return n;
            return -1;
        }
    }
    return -1;
}

static float sample_at(const uint8_t *d, int fmt, int bits, int idx)
{
    switch (bits) {
    case 8:  return ((int)d[idx] - 128) / 128.0f;
    case 16: return (int16_t)rd16(d + idx * 2) / 32768.0f;
    case 24: {
        int32_t v = d[idx * 3] | (d[idx * 3 + 1] << 8) | (d[idx * 3 + 2] << 16);
        if (v & 0x800000) v |= ~0xFFFFFF;
        return v / 8388608.0f;
    }
    case 32:
        if (fmt == 3) { float f; memcpy(&f, d + idx * 4, 4); return f; }
        return (int32_t)rd32(d + idx * 4) / 2147483648.0f;
    }
    return 0.0f;
}

static int16_t to16(float f)
{
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    return (int16_t)(f * 32767.0f);
}

static int convert_wav(const char *in, const char *outDir)
{
    int track = track_from_name(in);
    if (track < 0) {
        printf("  %s: no track number in the file name - skipped\n", in);
        return 0;
    }
    FILE *f = fopen(in, "rb");
    if (!f) { printf("  %s: cannot open\n", in); return 0; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        printf("  %s: read error\n", in); fclose(f); free(buf); return 0;
    }
    fclose(f);
    if (size < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) {
        printf("  %s: not a WAV file (convert it to WAV first) - skipped\n", in);
        free(buf); return 0;
    }
    int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0;
    const uint8_t *data = NULL; uint32_t dataLen = 0;
    for (long p = 12; p + 8 <= size; ) {
        uint32_t len = rd32(buf + p + 4);
        if (!memcmp(buf + p, "fmt ", 4) && len >= 16) {
            fmt = rd16(buf + p + 8); ch = rd16(buf + p + 10);
            rate = rd32(buf + p + 12); bits = rd16(buf + p + 22);
            if (fmt == 0xFFFE && len >= 40) fmt = rd16(buf + p + 32);   /* WAVE_FORMAT_EXTENSIBLE */
        } else if (!memcmp(buf + p, "data", 4)) {
            data = buf + p + 8;
            dataLen = len;
            if ((long)(p + 8 + dataLen) > size) dataLen = (uint32_t)(size - p - 8);
        }
        p += 8 + len + (len & 1);
    }
    if (!data || (fmt != 1 && fmt != 3) || ch < 1 || ch > 2 || rate < 8000 ||
        !(bits == 8 || bits == 16 || bits == 24 || bits == 32)) {
        printf("  %s: unsupported WAV (need PCM/float, mono/stereo) - skipped\n", in);
        free(buf); return 0;
    }
    int frameBytes = ch * bits / 8;
    long frames = (long)(dataLen / frameBytes);
    long outFrames = (long)((double)frames * 44100.0 / rate);
    char name[32], out[1024];
    snprintf(name, sizeof(name), "track%d.son", track);
    join(out, sizeof(out), outDir, name);
    FILE *o = fopen(out, "wb");
    if (!o) { printf("  cannot write %s\n", out); free(buf); return 0; }
    int16_t blk[2 * 4096];
    int n = 0;
    for (long i = 0; i < outFrames; i++) {
        double src = (double)i * rate / 44100.0;
        long s0 = (long)src;
        long s1 = s0 + 1 < frames ? s0 + 1 : s0;
        float t = (float)(src - s0);
        for (int c = 0; c < 2; c++) {
            int cc = ch == 2 ? c : 0;
            float a = sample_at(data, fmt, bits, (int)(s0 * ch + cc));
            float b = sample_at(data, fmt, bits, (int)(s1 * ch + cc));
            blk[n++] = to16(a + (b - a) * t);
        }
        if (n == (int)(sizeof(blk) / sizeof(blk[0]))) {
            fwrite(blk, 2, (size_t)n, o);
            n = 0;
        }
    }
    if (n) fwrite(blk, 2, (size_t)n, o);
    fclose(o);
    free(buf);
    printf("  %s -> %s (%ld:%02ld)\n", in, out, outFrames / 44100 / 60, (outFrames / 44100) % 60);
    return 1;
}

static int has_wav_ext(const char *s)
{
    size_t l = strlen(s);
    return l > 4 && (s[l - 4] == '.') && tolower((unsigned char)s[l - 3]) == 'w' &&
           tolower((unsigned char)s[l - 2]) == 'a' && tolower((unsigned char)s[l - 1]) == 'v';
}

static int convert_folder(const char *dir, const char *outDir)
{
    int done = 0;
#ifdef _WIN32
    char pat[1024];
    WIN32_FIND_DATAA fd;
    join(pat, sizeof(pat), dir, "*.wav");
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char p[1024];
        join(p, sizeof(p), dir, fd.cFileName);
        done += convert_wav(p, outDir);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!has_wav_ext(e->d_name)) continue;
        char p[1024];
        join(p, sizeof(p), dir, e->d_name);
        done += convert_wav(p, outDir);
    }
    closedir(d);
#endif
    return done;
}

static int is_dir(const char *p)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* ------------------------------------------------------------------ */
/* CD audio ripping (Windows)                                          */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
#define SECT_RAW 2352
#define READ_SECTS 20

#pragma pack(push, 1)
typedef struct { UCHAR Reserved, Control_Adr, TrackNumber, Reserved1; UCHAR Address[4]; } TOC_TRACK;
typedef struct { UCHAR Length[2]; UCHAR FirstTrack, LastTrack; TOC_TRACK Tracks[100]; } TOC_DATA;
typedef struct { LARGE_INTEGER DiskOffset; ULONG SectorCount; ULONG TrackMode; } RAW_INFO;
#pragma pack(pop)
#define IOCTL_READ_TOC 0x00024000      /* IOCTL_CDROM_READ_TOC */
#define IOCTL_RAW_READ 0x0002403E      /* IOCTL_CDROM_RAW_READ */
#define TRACKMODE_CDDA 2

static long msf_to_lba(const UCHAR *a) { return ((long)a[1] * 60 + a[2]) * 75 + a[3] - 150; }

static HANDLE open_drive(char letter)
{
    char path[16];
    snprintf(path, sizeof(path), "\\\\.\\%c:", letter);
    return CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, 0, NULL);
}

static int read_toc(HANDLE h, TOC_DATA *toc)
{
    DWORD got = 0;
    memset(toc, 0, sizeof(*toc));
    return DeviceIoControl(h, IOCTL_READ_TOC, NULL, 0, toc, sizeof(*toc), &got, NULL) != 0;
}

static int count_audio(const TOC_DATA *t)
{
    int n = 0;
    for (int i = 0; i <= t->LastTrack - t->FirstTrack; i++)
        if (!(t->Tracks[i].Control_Adr & 0x04)) n++;   /* Control (low nibble) bit 2 = data track */
    return n;
}

static int rip_cd(char letter, const char *outDir)
{
    HANDLE h = open_drive(letter);
    if (h == INVALID_HANDLE_VALUE) { printf("Cannot open drive %c:\n", letter); return 0; }
    TOC_DATA toc;
    if (!read_toc(h, &toc)) { printf("Drive %c: has no readable disc.\n", letter); CloseHandle(h); return 0; }
    int tracks = toc.LastTrack - toc.FirstTrack + 1;
    printf("Drive %c: %d tracks, %d audio.\n", letter, tracks, count_audio(&toc));
    static BYTE buf[SECT_RAW * READ_SECTS];
    int done = 0;
    for (int i = 0; i < tracks; i++) {
        TOC_TRACK *t = &toc.Tracks[i];
        if (t->Control_Adr & 0x04) continue;            /* data track */
        long start = msf_to_lba(t->Address);
        long end = msf_to_lba(toc.Tracks[i + 1].Address);   /* next track or lead-out */
        if (end <= start) continue;
        char name[32], out[1024];
        snprintf(name, sizeof(name), "track%d.son", t->TrackNumber);
        join(out, sizeof(out), outDir, name);
        FILE *o = fopen(out, "wb");
        if (!o) { printf("Cannot write %s\n", out); continue; }
        long total = end - start, lastPct = -1, errors = 0;
        for (long s = start; s < end; ) {
            ULONG n = (ULONG)(end - s < READ_SECTS ? end - s : READ_SECTS);
            RAW_INFO ri;
            ri.DiskOffset.QuadPart = (LONGLONG)s * 2048;
            ri.SectorCount = n;
            ri.TrackMode = TRACKMODE_CDDA;
            DWORD got = 0;
            int ok = 0;
            for (int tr = 0; tr < 4 && !ok; tr++)
                ok = DeviceIoControl(h, IOCTL_RAW_READ, &ri, sizeof(ri), buf, SECT_RAW * n, &got, NULL) &&
                     got == SECT_RAW * n;
            if (!ok) {   /* unreadable spot: write silence and carry on */
                memset(buf, 0, SECT_RAW * n);
                errors++;
            }
            fwrite(buf, 1, SECT_RAW * n, o);
            s += n;
            long pct = (s - start) * 100 / total;
            if (pct != lastPct) {
                printf("\r  track %2d: %3ld%%", t->TrackNumber, pct);
                fflush(stdout);
                lastPct = pct;
            }
        }
        fclose(o);
        printf("\r  track %2d -> %s (%ld:%02ld)%s\n", t->TrackNumber, out,
               total / 75 / 60, (total / 75) % 60, errors ? "  [some read errors, filled with silence]" : "");
        done++;
    }
    CloseHandle(h);
    return done;
}

static char find_audio_cd(void)
{
    DWORD drives = GetLogicalDrives();
    for (char c = 'A'; c <= 'Z'; c++) {
        if (!(drives & (1u << (c - 'A')))) continue;
        char root[4] = { c, ':', '\\', 0 };
        if (GetDriveTypeA(root) != DRIVE_CDROM) continue;
        HANDLE h = open_drive(c);
        if (h == INVALID_HANDLE_VALUE) continue;
        TOC_DATA toc;
        int ok = read_toc(h, &toc) && count_audio(&toc) > 0;
        CloseHandle(h);
        if (ok) return c;
    }
    return 0;
}
#endif

/* ------------------------------------------------------------------ */

#ifdef _WIN32
static void exe_dir(char *out, size_t n, const char *argv0)
{
#ifdef _WIN32
    GetModuleFileNameA(NULL, out, (DWORD)n);
#else
    snprintf(out, n, "%s", argv0);
#endif
    char *s = strrchr(out, SEP);
    if (s) *s = 0; else snprintf(out, n, ".");
}

#endif

static void parent_dir(char *out, size_t n, const char *path)
{
    snprintf(out, n, "%s", path);
    char *a = strrchr(out, '\\'), *b = strrchr(out, '/');
    char *s = a > b ? a : b;
    if (s) *s = 0; else snprintf(out, n, ".");
}

int main(int argc, char **argv)
{
    char outDir[1024] = "";
    const char *inputs[256];
    int nIn = 0;

    printf("Sonic R music converter for the Xbox port\n"
           "Output: MUSIC\\trackN.son (44100 Hz, 16-bit stereo PCM)\n\n");

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) snprintf(outDir, sizeof(outDir), "%s", argv[++i]);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("sonicr_music              rip the original CD (first drive with audio)\n"
                   "sonicr_music E:           rip from drive E:\n"
                   "sonicr_music a.wav b.wav  convert WAV files (track number from the name)\n"
                   "sonicr_music folder       convert every .wav in the folder\n"
                   "  -o DIR                  output folder (default: MUSIC)\n");
            return 0;
        }
        else if (nIn < 256) inputs[nIn++] = argv[i];
    }
#ifdef _WIN32
    /* Started from Explorer (double click or drag and drop): keep the window. */
    {
        DWORD pids[2];
        g_pause = GetConsoleProcessList(pids, 2) <= 1;
    }
#endif

    int done = 0;
    int isCd = (nIn == 0) || (nIn == 1 && strlen(inputs[0]) <= 3 && inputs[0][1] == ':' &&
                              (inputs[0][2] == 0 || inputs[0][2] == '\\'));
    if (isCd) {
#ifdef _WIN32
        char letter = nIn ? (char)toupper((unsigned char)inputs[0][0]) : find_audio_cd();
        if (!letter) {
            printf("No CD with audio tracks found. Put the Sonic R disc in the drive,\n"
                   "or drag your ripped WAV files onto this program.\n");
            finish(1);
        }
        if (!outDir[0]) {
            char d[1024];
            exe_dir(d, sizeof(d), argv[0]);
            join(outDir, sizeof(outDir), d, "MUSIC");
        }
        MKDIR(outDir);
        printf("Ripping the CD in drive %c: to %s\n", letter, outDir);
        done = rip_cd(letter, outDir);
#else
        printf("Reading CDs is only supported on Windows here. Rip the CD to WAV\n"
               "files with any ripper and pass them (or their folder) to this tool.\n");
        return 1;
#endif
    } else {
        if (!outDir[0]) {
            char d[1024];
            if (is_dir(inputs[0])) snprintf(d, sizeof(d), "%s", inputs[0]);
            else parent_dir(d, sizeof(d), inputs[0]);
            join(outDir, sizeof(outDir), d, "MUSIC");
        }
        MKDIR(outDir);
        printf("Converting to %s\n", outDir);
        for (int i = 0; i < nIn; i++) {
            if (is_dir(inputs[i])) done += convert_folder(inputs[i], outDir);
            else if (has_wav_ext(inputs[i])) done += convert_wav(inputs[i], outDir);
            else printf("  %s: not a WAV file - skipped (convert it to WAV first)\n", inputs[i]);
        }
    }

    printf("\n%d track(s) written. Copy the MUSIC folder next to default.xbe on the Xbox.\n", done);
    finish(done > 0 ? 0 : 1);
    return 0;
}
