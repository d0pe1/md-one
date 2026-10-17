#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Assert that a raw audio capture is a working instrument, not a noise.

Reads headerless interleaved int16 little-endian -- what the emulator writes
when FM1_BENCH_WAV is set. That variable's name is misleading: there is no WAV
header (fm1-live/src/main.rs writes `s.to_le_bytes()` straight out), so
scripts/wavstat.py cannot read these files and this is not a duplicate of it.

Split out of check-all.sh so the assertions can be tested directly:

    scripts/check-audio.py --selftest

A check that has never failed has not been shown to work.

Usage: check-audio.py <raw.pcm> [--min-frames N]
"""
import math
import os
import struct
import sys
import tempfile

MIN_SAMPLES = 44100     # a second of mono-equivalent; less means it died early
MIN_RMS     = 200       # below this it is silence with a dropout, not a drone
PINNED_MAX  = 0.01      # fraction of samples allowed at full scale


def analyse(path, min_samples=MIN_SAMPLES):
    raw = open(path, 'rb').read()
    n = len(raw) // 2
    if n == 0:
        return ["empty capture -- the emulator wrote nothing"], "0 samples"

    s = struct.unpack('<%dh' % n, raw[:n * 2])
    peak   = max(abs(v) for v in s)
    rms    = int(math.sqrt(sum(float(v) * v for v in s) / n))
    pinned = sum(1 for v in s if abs(v) >= 32767)
    nz     = sum(1 for v in s if v)

    summary = (f"{n} samples, peak {peak}, rms {rms}, "
               f"{100.0 * nz / n:.1f}% non-zero, {pinned} pinned")

    fails = []
    if n < min_samples:
        fails.append(f"only {n} samples -- the run stopped early")
    if peak == 0:
        fails.append("silence -- it booted but never made a sound")
    elif rms < MIN_RMS:
        fails.append(f"rms {rms} is near-silence")
    if pinned > n * PINNED_MAX:
        fails.append(f"{pinned} samples pinned at full scale -- "
                     f"clipping, not limiting")
    return fails, summary


def _write(data):
    fd, path = tempfile.mkstemp(suffix='.pcm')
    os.write(fd, struct.pack('<%dh' % len(data), *data))
    os.close(fd)
    return path


def selftest():
    """Every assertion must fire on input designed to trip it."""
    import random
    random.seed(7)
    n = 50000
    cases = [
        ("healthy drone",  [int(8000 * math.sin(i * 0.05)) for i in range(n)], False),
        ("dead silence",   [0] * n,                                            True),
        ("near-silence",   [random.choice((-1, 0, 1)) for _ in range(n)],      True),
        ("hard clipped",   [32767 if i % 2 else -32767 for i in range(n)],     True),
        ("stopped early",  [int(8000 * math.sin(i * 0.05)) for i in range(900)], True),
    ]
    bad = 0
    for name, data, should_fail in cases:
        path = _write(data)
        try:
            fails, summary = analyse(path)
        finally:
            os.unlink(path)
        got = bool(fails)
        mark = "ok  " if got == should_fail else "FAIL"
        if got != should_fail:
            bad += 1
        want = "rejected" if should_fail else "accepted"
        print(f"  {mark}  {name:<16} expected {want:<8} -> "
              f"{'rejected' if got else 'accepted'}")
        if fails:
            for f in fails:
                print(f"            {f}")
    print()
    print("selftest passed" if not bad else f"selftest FAILED ({bad})")
    return 1 if bad else 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if len(argv) < 2:
        print(__doc__.strip())
        return 2

    min_samples = MIN_SAMPLES
    if "--min-frames" in argv:
        min_samples = int(argv[argv.index("--min-frames") + 1])

    fails, summary = analyse(argv[1], min_samples)
    print(f"        {summary}")
    for f in fails:
        print(f"        {f}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
