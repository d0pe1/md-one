# dsp/

Synthesis mathematics, developed and tested without a synthesizer.

```
shared/   integer DSP -- compiles for BOTH the host and the pi32v2 target
host/     host-only: WAV writer, float reference, renderer, benchmark
target/   compile shared/ for pi32v2 and enforce the chip's rules
tests/    deterministic tests
```

## The split, and why it matters

`shared/` is the code that ships on the device. The chip has **no FPU and no
libgcc soft-float**, so a single `float` fails the *link*, not the compile —
an error that surfaces late and points at a missing libgcc symbol rather than
at the line that caused it.

So `shared/` is integer-only and `host/` holds everything floating-point: the
reference oscillator we measure against, and the file and timing plumbing.
`target/check.sh` compiles `shared/` with JieLi's clang and fails immediately,
naming the file, if a float or a 64-bit division creeps in.

## Use it

```sh
wsl -d archlinux
cd /mnt/c/temp/GitHub/fm1-laboratory/dsp

make            # build everything
make test       # deterministic tests
make bench      # cost against the 5.8 ms audio budget
make target     # pi32v2 compile + rule check
make demo       # render WAVs into build/
```

Inspect what comes out:

```powershell
python ..\scripts\wavstat.py build\fm-220x110.wav
```

## What the tests pin down

Not coverage — the five properties the synthesis engine will rest on:

| Test | Result |
| --- | --- |
| tuning vs the ideal ratio | within **0.000865 cents** |
| Q15 table + interpolation vs a `double` sine | **84.5 dB** SNR |
| determinism across 16 seeded frequencies | byte-identical |
| block path vs sample-at-a-time, ragged blocks | identical |
| 32-bit phase wrap | no discontinuity |

The tuning one is the interesting result. The target bans 64-bit division, so
the phase increment is computed as a 64-bit **multiply** by a per-sample-rate
constant (`fm1_phase_inc` in `shared/fm1_fixed.h`). The cost of avoiding the
division is about a thousandth of a cent.

## Conventions

| Format | Used for | Scale |
| --- | --- | --- |
| Q15 | samples, levels | 1.0 = 32767 |
| Q12 | gains | 1.0 = 4096 |
| Q16 | frequency in Hz | 440 Hz = 440 << 16 |
| `uint32_t` | oscillator phase | full range = one cycle, wraps for free |

## Generated tables

The target cannot compute a sine table at startup, so
`scripts/gen_sine_table.py` emits `shared/fm1_sine_table.h` as plain integers.
`--check` verifies it is current.

This is the seed of the procedural-wavetable workflow the project is aimed at:
the same generator will later emit harmonic-spectrum tables.

## Licence boundary — keep it

`dsp/` is **ours**, written from scratch, `SPDX-License-Identifier: MIT`. It
contains no Felucca, Dinghy, SLOOP or Mutable code.

The firmware will be GPL-3.0-only (see `docs/LICENSE_REGISTER.md`). MIT links
into GPL-3.0 fine, so this arrangement lets the engine be reused elsewhere —
a desktop plugin, another chip — without the GPL following it.

> **Rule:** nothing copied or adapted from a GPL source enters `dsp/`. Ported
> algorithms go in `firmware/`.

## Scope today

One oscillator: a Q15 table sine with linear interpolation, a block path, and a
phase-modulation primitive. Deliberately small — it exists to prove the harness
end to end before any real synthesis starts.

The six-operator engine, harmonic-spectrum editor and wavetables plug in at the
same seam. See the recommended first milestone in the root `README.md`.
