# The drone engine

Four always-on Braids voices, each through a tuned comb, with slow independent
drift, mixed to stereo. Built 2026-10-16. Runs on the host today; compiles for
pi32v2 and measured in the emulator.

```
dsp/engine/fm1_params.h   the parameter table: label, range, default, MIDI CC
dsp/engine/fm1_comb.h     tuned feedback comb -- the resonator
dsp/engine/fm1_drift.h    slow random walk, block rate
dsp/engine/fm1_drone.h/cc the engine: voices, mix, control
dsp/host/render_drone.cc  host renderer with four presets
dsp/target/probe_drone.cc the same engine, measured on the emulated CPU
```

All MIT and written here. It links the MIT Braids port in `dsp/mi/` and
contains no GPL-derived code, so the engine stays reusable outside this device
(`docs/LICENSE_REGISTER.md`).

## Hear it

```sh
cd dsp
make drone                       # one preset
make drone-all                   # all four, with restrikes
./build/render_drone --list
./build/render_drone --preset bell --sec 60 --restrike 6000 --out bell.wav
```

| Preset | What it is |
| --- | --- |
| `drift` | four detuned saw stacks, slow independent wander |
| `bell` | struck bells over a bowed bed, long comb tails |
| `breath` | flute and vowel formants, noisy and alive |
| `glass` | harmonics and wavetables, slow spectral morph |

Check them with `python scripts/wavmotion.py <wav>`, which reports stereo width
and whether the sound is actually evolving.

## Design decisions, and why

**Four voices, full `MacroOscillator` each.** Measurement said neither CPU nor
memory constrains this (below), so every voice keeps all 48 shapes rather than
being specialised to the cheap ones. Four is already twelve oscillators, since
the `TRIPLE_*` shapes are three detuned oscillators apiece.

**A tuned comb instead of reverb.** The output goes straight into a DAW over
USB audio, where reverb is better and free. Reverb also smears everything into
an average, which is the opposite of what a drone wants; a tuned comb imposes
pitch and structure. It costs ~4 KB a voice against ~306 KB of pool.

**The comb does not follow the drift.** It tracks the base pitch only. A fixed
resonator against a drifting source is what produces the slow beating — if
both drifted together the beats would vanish. It also keeps two divisions out
of the per-block path.

**Drift runs at block rate, not per sample.** A block is 5.8 ms, far finer
than anything audible as movement when full-scale travel takes seconds to
minutes. Per-sample updating would cost 256× for no perceptible gain.

**Drift moves pitch and timbre together.** Pitch is the one that matters:
against the fixed comb and the other voices it beats. ±25 cents at full depth
— enough to beat, not enough to sound out of tune.

**No 64-bit division anywhere.** The comb needs `delay = samplerate/frequency`,
and the oscillator already carries `inc = 2^32·f/fs`, so `delay = 2^32/inc`.
`2^32` does not fit in 32 bits, but `0xFFFFFFFF/inc` differs by less than one
part in four billion, and a second 32-bit division of the remainder gives the
fractional part to 1/256 of a sample.

## Cost on the target

Measured with `scripts/measure-drone-cycles.sh` — the whole engine, not a bare
oscillator, so it includes the combs, drift, per-sample panning and the
saturating stereo mix. Shape TRIPLE_SAW, comb mix 50%, feedback 0.7:

| Voices | cycles/block | µs | % of budget | marginal |
| --- | --- | --- | --- | --- |
| 1 | 55,651 | 231.9 | 4.0% | — |
| 2 | 104,524 | 435.5 | 7.5% | 48,873 |
| 3 | 153,397 | 639.2 | 11.0% | 48,873 |
| 4 | **202,277** | **842.8** | **14.5%** | 48,880 |

The marginal cost is flat to within 7 cycles across the sweep, so the engine
scales linearly and there is no hidden per-voice overhead.

A bare TRIPLE_SAW oscillator is 35,341 cycles, so the comb, drift, panning and
mix add about 13,500 per voice — a 38% overhead for all of the instrument's
character, which is a good trade.

**Four voices is 14.5% of the audio budget.** Even assuming the emulator's
1 instruction/cycle model is 3× optimistic — plausible, since Braids' tables
all live in XIP flash — that is ~44%, still inside RULES.md's advice to stay
under half.

Flash: the whole engine plus Braids is **101,864 B (~99 KB)** of the ~526 KB
free, of which our engine is 3,700 B.

## Verification

`scripts/wavmotion.py` checks the two things that matter for a drone and that
neither peak nor RMS reveals: whether it is genuinely stereo, and whether it is
actually evolving. It measures *level* (per-second RMS) and *brightness*
(per-second zero-crossing rate, a cheap stand-in for spectral centroid)
separately, because a drone can hold a steady level while its spectrum moves —
measuring only amplitude would call that static.

That distinction caught a real bug. The first version of `Render()` applied
drift to timbre but not pitch, despite a comment claiming both; `glass` read as
"static" at 0.8% level spread. With pitch drift added:

```
drone-drift.wav   level 7.6% subtle   bright 9.8% clearly moving
drone-glass.wav   level 4.1% subtle   bright 3.7% subtle
```

Note `drift` reports side/mid = +2.1 dB — the voices are hard-panned and
largely uncorrelated, so the stereo image is very wide. That is a preset
choice, not a fault, but anything summed to mono will lose a few dB.

## Not done yet

* **MIDI is not wired.** The parameter table carries the CC assignments
  (`docs/MIDI_SPEC.md`) and `fm1_ccmap_build()` builds the lookup, but nothing
  parses MIDI yet — that belongs in the firmware, where `midi_get()` lives.
* **No parameter smoothing.** 7-bit CC is 128 steps, which will zipper on a
  slowly automated cutoff over a sustained drone. A one-pole slew at block rate
  is the fix; it is not written.
* **No unison, binaural or Haas.** The globals exist in the table; only
  `SPREAD` and `MASTER` are implemented.
* **No glide.** `P_GLIDE` is in the table and ignored.
* **Not on hardware.** Nothing has been flashed; no device exists yet.
