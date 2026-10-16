#!/usr/bin/env python3
"""Compare two renders of the same sound spectrally.

Used to check our 44.1 kHz Braids port against the SAME MI source running on
upstream's 96 kHz tables -- the reference. Comparing partials rather than
samples means no resampling is needed, so no resampler artifacts pollute the
result. Pitch and harmonic structure must match; the absolute sample rate need
not.

  python scripts/compare_partials.py ref.wav ours.wav
"""
import cmath
import math
import sys
import wave


def read_mono(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit(f"{path}: expected 16-bit PCM")
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    s = [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]
    if ch > 1:
        s = [sum(s[i:i + ch]) / ch for i in range(0, len(s) - ch + 1, ch)]
    return s, rate


def partials(sig, rate, want=12, fmin=20.0, fmax=12000.0, bins=900):
    """Goertzel over a log grid, then local maxima. Returns [(hz, dbfs)]."""
    n = min(len(sig), rate)
    if n == 0:
        return []
    seg = sig[len(sig) // 3: len(sig) // 3 + n]
    win = [0.5 - 0.5 * math.cos(2 * math.pi * i / n) for i in range(n)]
    seg = [s * w for s, w in zip(seg, win)]

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
    top = peaks[:want]
    if not top:
        return []
    ref = top[0][0]
    return sorted([(f, 20 * math.log10(m / ref) if m else -120.0) for m, f in top])


def cents(a, b):
    return 1200.0 * math.log2(b / a) if a > 0 and b > 0 else float("inf")


def envelope(sig, rate, step_ms=50):
    """RMS per step_ms, in dB relative to the loudest step.

    For the impulse-driven and noise shapes, matching partials is the wrong
    test: their PRNG advances per SAMPLE, so at two different sample rates the
    noise realisation differs and no spectrum can match, bug or no bug. What
    must match is the shape in TIME -- attack and decay are calibrated in
    seconds by the tables, and getting those right at 44.1 kHz is the whole
    point of regenerating them."""
    w = max(1, rate * step_ms // 1000)
    out = []
    for i in range(0, len(sig) - w + 1, w):
        seg = sig[i:i + w]
        out.append(math.sqrt(sum(x * x for x in seg) / w))
    if not out:
        return []
    peak = max(out) or 1.0
    return [20 * math.log10(v / peak) if v > 0 else -120.0 for v in out]


def compare_envelopes(ref, our, step_ms=50):
    """Mean and worst absolute difference in dB over the common span."""
    n = min(len(ref), len(our))
    if n < 4:
        return None
    diffs = [abs(a - b) for a, b in zip(ref[:n], our[:n])
             if a > -60.0 or b > -60.0]          # ignore the silent tail
    if not diffs:
        return None
    return sum(diffs) / len(diffs), max(diffs), n


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    ref_path, our_path = sys.argv[1], sys.argv[2]

    rs, rr = read_mono(ref_path)
    os_, orr = read_mono(our_path)
    rp = partials(rs, rr)
    op = partials(os_, orr)

    print(f"  reference {ref_path}  ({rr} Hz, {len(rp)} partials)")
    print(f"  ours      {our_path}  ({orr} Hz, {len(op)} partials)")
    print()
    print(f"  {'REF Hz':>10} {'OURS Hz':>10} {'CENTS':>8} {'REF dB':>8} {'OURS dB':>8} {'d dB':>7}")
    print(f"  {'-'*10} {'-'*10} {'-'*8} {'-'*8} {'-'*8} {'-'*7}")

    used = set()
    worst_cents = 0.0
    worst_db = 0.0
    matched = 0
    for f, db in rp:
        # nearest unused partial in ours, within a semitone
        best, bestd = None, 1e9
        for j, (g, gdb) in enumerate(op):
            if j in used:
                continue
            d = abs(cents(f, g))
            if d < bestd:
                best, bestd = j, d
        if best is not None and bestd < 100.0:
            used.add(best)
            g, gdb = op[best]
            matched += 1
            worst_cents = max(worst_cents, bestd)
            worst_db = max(worst_db, abs(gdb - db))
            print(f"  {f:10.1f} {g:10.1f} {cents(f, g):8.1f} {db:8.1f} {gdb:8.1f} {gdb - db:7.1f}")
        else:
            print(f"  {f:10.1f} {'--':>10} {'--':>8} {db:8.1f} {'--':>8} {'--':>7}")

    for j, (g, gdb) in enumerate(op):
        if j not in used:
            print(f"  {'--':>10} {g:10.1f} {'--':>8} {'--':>8} {gdb:8.1f} {'--':>7}")

    # --- envelope, which is the meaningful test for the time-varying shapes
    re_, oe = envelope(rs, rr), envelope(os_, orr)
    env = compare_envelopes(re_, oe)
    print()
    if env:
        mean_d, worst_d, n = env
        print(f"  envelope over {n * 50 / 1000.0:.1f}s: mean |delta| {mean_d:.1f} dB, "
              f"worst {worst_d:.1f} dB")
        bars = lambda e: "".join(" ▁▂▃▄▅▆▇█"[max(0, min(8, int(8 * (v + 60) / 60)))] for v in e[:40])
        print(f"    ref  {bars(re_)}")
        print(f"    ours {bars(oe)}")
        print()

    extra = len(op) - matched
    print(f"  matched {matched}/{len(rp)} reference partials"
          + (f", {extra} unmatched in ours" if extra else ""))
    if not matched:
        print("  verdict: NO MATCH")
        return 1

    print(f"  worst pitch error  {worst_cents:.1f} cents")
    print(f"  worst level error  {worst_db:.1f} dB")

    # A missing or extra partial is a real spectral difference, not noise, so
    # near-total coverage is required. An earlier threshold of two thirds
    # called an 8-of-12 match a MATCH -- a third of the spectrum gone, which
    # is exactly the case this is meant to catch. One miss is tolerated for
    # analyser edge effects.
    reasons = []
    if matched < len(rp) - 1:
        reasons.append(f"only {matched} of {len(rp)} reference partials found")
    if extra > 1:
        reasons.append(f"{extra} partials present in ours but not the reference")
    if worst_cents >= 15.0:
        reasons.append(f"pitch off by {worst_cents:.1f} cents")
    if worst_db >= 6.0:
        reasons.append(f"level off by {worst_db:.1f} dB")

    if reasons:
        print("  verdict: DIFFERS -- " + "; ".join(reasons))
        return 1
    print("  verdict: MATCH")
    return 0


if __name__ == "__main__":
    sys.exit(main())
