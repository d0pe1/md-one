/* SPDX-License-Identifier: MIT */
#include "wav.h"
#include <stdio.h>
#include <string.h>

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v);         p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);   p[3] = (unsigned char)(v >> 24);
}

static void put16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v);         p[1] = (unsigned char)(v >> 8);
}

int wav_write(const char *path, const int16_t *samples,
              uint32_t frames, uint32_t samplerate, uint16_t channels)
{
    uint32_t data_bytes = frames * channels * 2u;
    unsigned char h[44];

    memcpy(h + 0, "RIFF", 4);  put32(h + 4, 36u + data_bytes);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4); put32(h + 16, 16);
    put16(h + 20, 1);                                  /* PCM */
    put16(h + 22, channels);
    put32(h + 24, samplerate);
    put32(h + 28, samplerate * channels * 2u);         /* byte rate */
    put16(h + 32, (uint16_t)(channels * 2));           /* block align */
    put16(h + 34, 16);                                 /* bits */
    memcpy(h + 36, "data", 4); put32(h + 40, data_bytes);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    int ok = fwrite(h, 1, sizeof h, f) == sizeof h;
    /* The samples are little-endian int16 in memory on every host we target;
     * write them directly rather than byte-swapping one at a time. */
    if (ok && data_bytes)
        ok = fwrite(samples, 1, data_bytes, f) == data_bytes;

    fclose(f);
    return ok ? 0 : -1;
}
