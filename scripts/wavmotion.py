#!/usr/bin/env python3
"""Is a stereo WAV actually moving, and is it actually stereo?

For drone work the two failure modes that matter are a signal that is
technically correct but perceptually dead (no evolution) and one that is
nominally stereo but identical in both channels. Neither shows up in a peak or
RMS figure, so check them directly.

  python scripts/wavmotion.py artifacts/dsp/drone/drone-drift.wav
"""
import math
import sys
import wave


def read(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit(f"{path}: expected 16-bit PCM")
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    s = [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]
    if ch == 2:
        return s[0::2], s[1::2], rate
    return s, list(s), rate


def rms(xs):
    return math.sqrt(sum(x * x for x in xs) / len(xs)) if xs else 0.0


def main():
    for path in sys.argv[1:]:
        L, R, rate = read(path)
        n = min(len(L), len(R))
        print(path)
        print(f"  {n} frames @ {rate} Hz = {n / rate:.1f} s")

        # --- stereo -----------------------------------------------------
        diff = [l - r for l, r in zip(L, R)]
        mid = [(l + r) / 2 for l, r in zip(L, R)]
        dr, mr = rms(diff), rms(mid)
        if dr == 0:
            print("  stereo:  MONO -- L and R are bit-identical")
        else:
            print(f"  stereo:  side/mid = {dr / mr:.3f}"
                  f" ({20 * math.log10(dr / mr):.1f} dB)  L rms {rms(L):.0f}  R rms {rms(R):.0f}")

        # --- movement ---------------------------------------------------
        # Two kinds, and they are independent: a drone can hold a steady level
        # while its spectrum evolves, which is exactly what a timbre LFO does.
        # Measuring only amplitude would call that "static".
        #
        #   level      per-second RMS
        #   brightness per-second zero-crossing rate, a cheap stand-in for
        #              spectral centroid -- no DFT, and good enough to tell
        #              moving from frozen.
        win = rate
        starts = range(0, n - win + 1, win)
        levels = [rms(mid[i:i + win]) for i in starts]

        def zcr(xs):
            c = sum(1 for a, b in zip(xs, xs[1:]) if (a >= 0) != (b >= 0))
            return c / len(xs) if xs else 0.0

        brights = [zcr(mid[i:i + win]) for i in starts]

        def spread(xs):
            m = sum(xs) / len(xs)
            if not m:
                return 0.0
            sd = math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))
            return sd / m

        def verdict(cv):
            return "static" if cv < 0.02 else "subtle" if cv < 0.08 else "clearly moving"

        if len(levels) >= 3:
            cvl, cvb = spread(levels), spread(brights)
            print(f"  level:   {min(levels):.0f}..{max(levels):.0f} rms, "
                  f"spread {cvl * 100:.1f}%  -- {verdict(cvl)}")
            print(f"  bright:  {min(brights) * rate / 2:.0f}..{max(brights) * rate / 2:.0f} Hz, "
                  f"spread {cvb * 100:.1f}%  -- {verdict(cvb)}")
            overall = max(cvl, cvb)
            print(f"  overall: {verdict(overall)}")

        if len(levels) >= 8:
            peak = max(levels) or 1
            print("  level:   " + "".join(" ▁▂▃▄▅▆▇█"[min(8, int(8 * s / peak))] for s in levels))
            pb = max(brights) or 1
            print("  bright:  " + "".join(" ▁▂▃▄▅▆▇█"[min(8, int(8 * s / pb))] for s in brights))
        print()


if __name__ == "__main__":
    main()
