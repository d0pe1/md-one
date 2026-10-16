#!/usr/bin/env python3
"""Summarise a 16-bit PCM WAV: level, and the strongest partials via a plain DFT.

Deliberately stdlib-only (wave + cmath) so it runs on any Python here, on the
Windows host or in WSL, with nothing installed. It is a smoke test for "did the
emulator actually make sound, and at roughly the right pitch", not an analyser.

  python scripts/wavstat.py artifacts/emu/dinghy.wav
"""
import cmath
import math
import sys
import wave


def read_mono(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit(f"{path}: expected 16-bit PCM, got {w.getsampwidth() * 8}-bit")
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    s = [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]
    if ch > 1:                                  # average the channels
        s = [sum(s[i:i + ch]) / ch for i in range(0, len(s) - ch + 1, ch)]
    return s, rate


def peak_bins(sig, rate, want=5, fmin=20.0, fmax=8000.0):
    """Goertzel over a log-spaced grid, then keep local maxima. O(bins*N), fine here."""
    n = min(len(sig), rate)                      # at most 1 s -> ~1 Hz resolution
    if n == 0:
        return []
    seg = sig[len(sig) // 3:len(sig) // 3 + n]   # a window from the sustained part
    win = [0.5 - 0.5 * math.cos(2 * math.pi * i / n) for i in range(n)]
    seg = [s * w for s, w in zip(seg, win)]

    bins = 600
    freqs = [fmin * (fmax / fmin) ** (i / (bins - 1)) for i in range(bins)]
    mags = []
    for f in freqs:
        w = cmath.exp(-2j * math.pi * f / rate)
        acc, p = 0j, 1 + 0j
        for s in seg:
            acc += s * p
            p *= w
        mags.append(abs(acc) / n)

    peaks = [(mags[i], freqs[i]) for i in range(1, bins - 1)
             if mags[i] > mags[i - 1] and mags[i] >= mags[i + 1]]
    peaks.sort(reverse=True)
    return peaks[:want]


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for path in sys.argv[1:]:
        sig, rate = read_mono(path)
        if not sig:
            print(f"{path}: empty")
            continue
        pk = max(abs(s) for s in sig)
        rms = math.sqrt(sum(s * s for s in sig) / len(sig))
        dbfs = 20 * math.log10(rms / 32768) if rms else float("-inf")
        print(f"{path}")
        print(f"  {len(sig)} frames @ {rate} Hz = {len(sig) / rate:.2f} s")
        print(f"  peak {pk} ({pk / 32768 * 100:.1f}% FS)   rms {rms:.1f} ({dbfs:.1f} dBFS)")
        if pk == 0:
            print("  SILENT")
            continue
        print("  partials:")
        for m, f in peak_bins(sig, rate):
            print(f"    {f:8.1f} Hz   {20 * math.log10(m / 32768):6.1f} dBFS")


if __name__ == "__main__":
    main()
