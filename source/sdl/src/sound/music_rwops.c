/**
 * music_rwops.c — see music_rwops.h.
 *
 * Virtual file layout presented to SDL_mixer:
 *   [0, 44)            synthesized canonical PCM WAV header
 *   [44, 44+dataSize)  the audio body (raw for .SON, decoded for .ADX)
 *
 * SDL_mixer reads/seeks this like any WAV. The only non-header seek it issues in
 * practice is loop-to-start (back to byte 44); the .ADX path handles arbitrary
 * seeks by rewinding + re-decoding, so correctness never depends on that.
 */
#include "music_rwops.h"
#include "adx.h"
#include "adp.h"
#include "path_ci.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t  header[44];
    int64_t  pos;            /* virtual read cursor */
    int64_t  dataSize;       /* audio body byte count */
    int64_t  total;          /* 44 + dataSize */

    int      mode;           /* 0 = .SON passthrough, 1 = .ADX, 2 = .ADP */

    /* .SON */
    FILE    *fp;
    long     sonDataStart;   /* file offset of first PCM byte (0) */

    /* .ADX / .ADP — decode-on-demand; decodedPos tracks the decoder's cursor */
    AdxDecoder adx;
    AdpDecoder adp;
    int64_t    decodedPos;   /* PCM byte offset the active decoder is at */
} MusicSrc;

static void wr16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void build_wav_header(uint8_t *h, uint32_t rate, uint16_t channels,
                             uint16_t bits, uint32_t dataSize)
{
    uint32_t byteRate = rate * channels * (bits / 8);
    uint16_t blockAlign = (uint16_t)(channels * (bits / 8));
    memcpy(h + 0, "RIFF", 4);
    wr32le(h + 4, 36 + dataSize);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    wr32le(h + 16, 16);            /* fmt chunk size */
    wr16le(h + 20, 1);            /* PCM */
    wr16le(h + 22, channels);
    wr32le(h + 24, rate);
    wr32le(h + 28, byteRate);
    wr16le(h + 32, blockAlign);
    wr16le(h + 34, bits);
    memcpy(h + 36, "data", 4);
    wr32le(h + 40, dataSize);
}

/* Dispatch decode/rewind to the active decoder (mode 1 = ADX, 2 = ADP). */
static size_t decode_read(MusicSrc *m, uint8_t *out, size_t n)
{
    return (m->mode == 2) ? Adp_Read(&m->adp, out, n) : Adx_Read(&m->adx, out, n);
}

static void decode_rewind(MusicSrc *m)
{
    if (m->mode == 2) {
        Adp_Rewind(&m->adp);
    }
    else {
        Adx_Rewind(&m->adx);
    }
    m->decodedPos = 0;
}

/* Advance/rewind the active decoder so its output cursor sits at `target`. */
static void decode_seek_pcm(MusicSrc *m, int64_t target)
{
    if (target < m->decodedPos) {
        decode_rewind(m);
    }
    uint8_t tmp[4096];
    while (m->decodedPos < target) {
        int64_t need = target - m->decodedPos;
        size_t chunk = (need > (int64_t)sizeof(tmp)) ? sizeof(tmp) : (size_t)need;
        size_t got = decode_read(m, tmp, chunk);
        if (got == 0) {
            break;
        }
        m->decodedPos += (int64_t)got;
    }
}

static Sint64 src_size(SDL_RWops *ctx)
{
    MusicSrc *m = (MusicSrc *)ctx->hidden.unknown.data1;
    return m->total;
}

static Sint64 src_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    MusicSrc *m = (MusicSrc *)ctx->hidden.unknown.data1;
    int64_t np;
    switch (whence) {
        case RW_SEEK_SET: np = offset;            break;
        case RW_SEEK_CUR: np = m->pos + offset;   break;
        case RW_SEEK_END: np = m->total + offset; break;
        default: return -1;
    }
    if (np < 0) {
        np = 0;
    }
    if (np > m->total) {
        np = m->total;
    }
    m->pos = np;
    return np;
}

static size_t src_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    MusicSrc *m = (MusicSrc *)ctx->hidden.unknown.data1;
    size_t want = size * maxnum;
    if (want == 0) {
        return 0;
    }

    uint8_t *out = (uint8_t *)ptr;
    size_t done = 0;

    /* header region */
    while (done < want && m->pos < 44) {
        out[done++] = m->header[m->pos++];
    }

    /* audio body */
    while (done < want && m->pos < m->total) {
        int64_t pcmOff = m->pos - 44;
        int64_t remain = m->dataSize - pcmOff;
        size_t  chunk  = want - done;
        if ((int64_t)chunk > remain) {
            chunk = (size_t)remain;
        }

        size_t got;
        if (m->mode == 0) {
            if (fseek(m->fp, m->sonDataStart + pcmOff, SEEK_SET) != 0) {
                break;
            }
            got = fread(out + done, 1, chunk, m->fp);
        } else {
            if (pcmOff != m->decodedPos) {
                decode_seek_pcm(m, pcmOff);
            }
            got = decode_read(m, out + done, chunk);
            m->decodedPos += (int64_t)got;
        }
        if (got == 0) {
            break;
        }
        done += got;
        m->pos += (int64_t)got;
    }

    return (size == 0) ? 0 : (done / size);
}

static size_t src_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    (void)ctx; (void)ptr; (void)size; (void)num;
    return 0;   /* read-only */
}

static int src_close(SDL_RWops *ctx)
{
    if (ctx) {
        MusicSrc *m = (MusicSrc *)ctx->hidden.unknown.data1;
        if (m) {
            if (m->mode == 0 && m->fp) {
                fclose(m->fp);
            }
            if (m->mode == 1) {
                Adx_Close(&m->adx);
            }
            if (m->mode == 2) {
                Adp_Close(&m->adp);
            }
            free(m);
        }
        SDL_FreeRW(ctx);
    }
    return 0;
}

static SDL_RWops *make_rw(MusicSrc *m)
{
    SDL_RWops *rw = SDL_AllocRW();
    if (!rw) {
        if (m->mode == 0 && m->fp) {
            fclose(m->fp);
        }
        if (m->mode == 1) {
            Adx_Close(&m->adx);
        }
        if (m->mode == 2) {
            Adp_Close(&m->adp);
        }
        free(m);
        return NULL;
    }
    rw->size  = src_size;
    rw->seek  = src_seek;
    rw->read  = src_read;
    rw->write = src_write;
    rw->close = src_close;
    rw->type  = SDL_RWOPS_UNKNOWN;
    rw->hidden.unknown.data1 = m;
    return rw;
}

SDL_RWops *MusicRW_OpenSon(const char *path, uint32_t rate,
                           uint16_t channels, uint16_t bits)
{
    FILE *fp = sr_fOpenCI(path, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    fseek(fp, 0, SEEK_SET);

    MusicSrc *m = (MusicSrc *)calloc(1, sizeof(*m));
    if (!m) {
        fclose(fp);
        return NULL;
    }

    m->mode = 0;
    m->fp = fp;
    m->sonDataStart = 0;
    m->dataSize = sz;
    m->total = 44 + (int64_t)sz;
    m->pos = 0;
    build_wav_header(m->header, rate, channels, bits, (uint32_t)sz);

    return make_rw(m);
}

SDL_RWops *MusicRW_OpenAdx(const char *path)
{
    MusicSrc *m = (MusicSrc *)calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }

    if (Adx_Open(&m->adx, path) != 0) {
        free(m);
        return NULL;
    }

    m->mode = 1;
    m->dataSize = Adx_PcmBytes(&m->adx);
    m->total = 44 + m->dataSize;
    m->pos = 0;
    m->decodedPos = 0;
    build_wav_header(m->header, (uint32_t)m->adx.sampleRate,
                     (uint16_t)m->adx.channels, 16, (uint32_t)m->dataSize);

    return make_rw(m);
}

SDL_RWops *MusicRW_OpenAdp(const char *path, uint32_t rate, uint16_t channels)
{
    MusicSrc *m = (MusicSrc *)calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }

    if (Adp_Open(&m->adp, path, (int)rate, (int)channels) != 0) {
        free(m);
        return NULL;
    }

    m->mode = 2;
    m->dataSize = Adp_PcmBytes(&m->adp);
    m->total = 44 + m->dataSize;
    m->pos = 0;
    m->decodedPos = 0;
    build_wav_header(m->header, rate, channels, 16, (uint32_t)m->dataSize);

    return make_rw(m);
}
