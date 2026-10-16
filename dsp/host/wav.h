/* SPDX-License-Identifier: MIT
 * Minimal 16-bit PCM WAV writer. Host only. */
#ifndef FM1_HOST_WAV_H
#define FM1_HOST_WAV_H

#include <stdint.h>

/* Write n frames of interleaved 16-bit PCM. Returns 0 on success. */
int wav_write(const char *path, const int16_t *samples,
              uint32_t frames, uint32_t samplerate, uint16_t channels);

#endif
