/**
 * adp.c — see adp.h. Clean AICA Yamaha-ADPCM decoder for headerless interleaved
 * stereo (wav2adpcm -n -i -t).
 */
#include "adp.h"
#include "path_ci.h"

#include <string.h>

static const int ADP_DIFF[16] = {
     1,  3,  5,  7,  9,  11,  13,  15,
    -1, -3, -5, -7, -9, -11, -13, -15
};
static const int ADP_SCALE[8] = {
    0xE6, 0xE6, 0xE6, 0xE6, 0x133, 0x199, 0x200, 0x266
};

/* One nibble -> one s16 sample, advancing this channel's predictor state. */
static int adp_step(int code, int *cur, int *quant)
{
    int delta = (*quant * ADP_DIFF[code]) / 8;   /* C '/' truncates toward zero */
    int c = *cur + delta;
    if (c < -32768) {
        c = -32768;
    }
    else if (c > 32767) {
        c = 32767;
    }
    *cur = c;

    int nq = (*quant * ADP_SCALE[code & 7]) >> 8;
    if (nq < 127) {
        nq = 127;
    }
    else if (nq > 24576) {
        nq = 24576;
    }
    *quant = nq;

    return c;
}

int Adp_Open(AdpDecoder *d, const char *path, int sampleRate, int channels)
{
    memset(d, 0, sizeof(*d));
    if (channels != 1 && channels != 2) {
        return -2;
    }

    d->fp = sr_fOpenCI(path, "rb");
    if (!d->fp) {
        return -1;
    }

    if (fseek(d->fp, 0, SEEK_END) != 0) {
        Adp_Close(d);
        return -1;
    }
    long sz = ftell(d->fp);
    if (sz < 0) {
        Adp_Close(d);
        return -1;
    }
    fseek(d->fp, 0, SEEK_SET);

    d->channels = channels;
    d->sampleRate = sampleRate;
    d->fileBytes = sz;

    /* Mono: 1 ADP byte = 2 samples = 4 PCM bytes. Stereo (-i): 1 ADP byte
     * carries one L nibble (high) + one R nibble (low) = one stereo frame =
     * 4 PCM bytes. Either way the payload expands x4. */
    d->pcmBytes = d->fileBytes * 4;

    Adp_Rewind(d);
    return 0;
}

int64_t Adp_PcmBytes(const AdpDecoder *d)
{
    return d->pcmBytes;
}

void Adp_Rewind(AdpDecoder *d)
{
    if (d->fp) {
        fseek(d->fp, 0, SEEK_SET);
    }
    d->cur[0] = d->cur[1] = 0;
    d->quant[0] = d->quant[1] = 127;
    d->obytePos = d->obyteCount = 0;
}

/* Decode one frame into obuf. Mono: 1 byte -> 2 samples (4 bytes). Stereo: 2
 * bytes -> a planar byte per channel -> 2 stereo frames (8 bytes). */
static int adp_decode_frame(AdpDecoder *d)
{
    int16_t *o = (int16_t *)d->obuf;

    if (d->channels == 1) {
        uint8_t b;
        if (fread(&b, 1, 1, d->fp) != 1) {
            return 0;
        }
        o[0] = (int16_t)adp_step(b & 0x0F, &d->cur[0], &d->quant[0]);        /* even = low  */
        o[1] = (int16_t)adp_step((b >> 4) & 0x0F, &d->cur[0], &d->quant[0]); /* odd  = high */
        d->obytePos = 0;
        d->obyteCount = 4;
        return 4;
    }

    /* Inverse of wav2adpcm's -i interleave collapses to: each byte carries one
     * left nibble (high) + one right nibble (low), decoded sequentially. */
    uint8_t b;
    if (fread(&b, 1, 1, d->fp) != 1) {
        return 0;
    }

    int L = adp_step((b >> 4) & 0x0F, &d->cur[0], &d->quant[0]); /* high = left  */
    int R = adp_step(b & 0x0F, &d->cur[1], &d->quant[1]);        /* low  = right */

    o[0] = (int16_t)L;
    o[1] = (int16_t)R;
    d->obytePos = 0;
    d->obyteCount = 4;
    return 4;
}

size_t Adp_Read(AdpDecoder *d, uint8_t *out, size_t nbytes)
{
    size_t done = 0;
    while (done < nbytes) {
        if (d->obytePos >= d->obyteCount) {
            if (adp_decode_frame(d) == 0) {
                break;   /* EOF */
            }
        }
        size_t avail = (size_t)(d->obyteCount - d->obytePos);
        size_t want = nbytes - done;
        size_t copy = (want < avail) ? want : avail;
        memcpy(out + done, d->obuf + d->obytePos, copy);
        done += copy;
        d->obytePos += (int)copy;
    }
    return done;
}

void Adp_Close(AdpDecoder *d)
{
    if (d->fp) {
        fclose(d->fp);
        d->fp = NULL;
    }
}
