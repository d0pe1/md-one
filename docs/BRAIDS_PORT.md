# Porting Braids to the FM-1

Status of the vertical slice, 2026-10-16. Everything below was measured.

**Result: Braids' macro oscillator — all 48 shapes — compiles for pi32v2, fits
in ~97 KB of the ~526 KB free, needs no soft-float, and renders in tune at
44.1 kHz with no regenerated tables.**

## Why Braids and not Plaits, Rings or Clouds

Mutable's catalogue splits cleanly along one line: which MCU it was written
for. Counting `float`/`double` tokens across each module's DSP sources:

| Module | float tokens / 100 lines | Original MCU |
| --- | --- | --- |
| **braids** | **0.0** | STM32F1, 72 MHz, **no FPU** |
| **tides (v1)** | **0.0** | STM32F1 |
| **peaks** | **0.0** | STM32F1 |
| **streams**, **frames** | **0.0** | STM32F1 |
| stages | 4.0 | STM32F4 + FPU |
| tides2 | 5.8 | STM32F4 + FPU |
| clouds, marbles | 6.0 | STM32F4 + FPU |
| rings | 8.2 | STM32F4 + FPU |
| warps | 8.9 | STM32F4 + FPU |
| elements | 9.4 | STM32F4 + FPU |
| **plaits** | **10.0** | STM32F4 + FPU |

The FM-1 has no FPU and no libgcc soft-float, so a single `float` fails the
link. Braids was written under exactly that constraint and ports almost
directly. Rings, Clouds, Elements and Plaits assume hardware floating point
and would each need a full fixed-point rewrite — a different project.

This is also why hortator could ship Plaits' *drum* models: those are the
cheap, mostly-integer ones. It is not evidence the rest of Plaits fits.

### Plaits, measured — the prediction above, confirmed

The section above argued from source inspection that Plaits assumes an FPU.
On 2026-10-17 it became possible to test that claim rather than infer it:
M-VAVE's official SDK ships `libcompiler_rt.a`, which supplies the soft-float
arithmetic our toolchain lacks, so Plaits **links** for the first time
(`docs/LICENSE_REGISTER.md`).

It links. It does not run.

| Engine | cycles / 24-frame call | per 256-frame block | % budget |
| --- | --- | --- | --- |
| wavetable | 146,938 | 1,567,336 | **113%** |
| virtual_analog | 172,597 | 1,841,033 | **132%** |
| fm | 220,687 | 2,353,998 | **169%** |

That is **one voice, free-running**, against the 1,393,152 cycles available
for the entire instrument. The shipping four-voice engine — two filters, EQ,
drive, AHDSR, patchbay and mixer — runs at **20.4%**.

The gap is the whole story:

```
Braids, dearest shape  (HARMONICS)     45,139 cycles / voice / block
Plaits, cheapest engine (wavetable) 1,567,336 cycles / voice / block
                                        ~35x
```

Same algorithm class, same chip, same harness. The only difference is the
number format — and on a chip with no FPU, that difference is everything.

#### What was done to make the measurement falsifiable

* **Trigger state was handled, not ignored.** 11 of Plaits' 22 engines read
  `EngineParameters::trigger`, and nine branch on `TRIGGER_UNPATCHED`
  specifically — they behave *differently* unpatched rather than merely
  staying silent. Free-running engines were measured unpatched; triggered
  ones get a rising edge on the first block and are measured while ringing,
  which is the expensive part. Measuring an untriggered percussive engine
  would have given a flatteringly low number — the same trap Braids'
  `strike_` flag set, documented above.
* **The spread was sampled rather than assumed**: 1.5x between cheapest and
  dearest, against 3x across Braids' shapes. No spread rescues something that
  starts at 113%.
* **stmlib's portable paths** were used (`-DTEST`), not its ARM inline
  assembly (`ssat`, `usat`, `vsqrt.f32`), which this chip cannot execute.
* **The libm surface is tiny** and was not the obstacle: Plaits calls no libm
  at all — its own comments say *"to avoid the call to sinf, we use a 3rd
  order polynomial"* — and stmlib needs only `sqrtf`, `sinf`, `cosf` and
  `fabsf`, none at audio rate.

#### What this does not overturn

**Float is not forbidden by the chip, only by our link.** That correction
stands and is useful on its own: it removes the failure mode where a stray
`0.5` produces a cryptic missing-symbol error, and it allows `exp()` and
`sin()` at setup rate.

**Plaits is still interesting as algorithms, not as code.** A hand-written
fixed-point port of one or two engines is a different project with a
plausible budget — exactly how Braids got here. The two worth the effort for
a drone instrument are `modal` and `string`, the resonators.

hortator's integer Plaits *drum* ports are evidence the approach works, but
they are **GPL-3.0-only** and would live in `firmware/`, never `dsp/`. A port
written from the MIT original keeps `dsp/` reusable and is the cleaner route.

#### The experiment is not in the tree

Plaits was never vendored into `dsp/`, and the probe was never added to
`dsp/target/`, because measuring before porting was the entire point. The
table above is the only thing worth keeping.

## FM, measured — what the chip was built for

Plaits' `fm` engine costs 169% of budget for one voice. The thing it is
*imitating* costs 3%.

Measured with `dsp/target/probe_fm.cc` via `scripts/measure-fm-cycles.sh`:
`OPS` operators in a serial stack, the deepest one fed back into itself. A
stack is the worst case — every operator runs every sample and none can be
skipped.

| Configuration | cyc/block | per voice | **per operator** | % budget |
| --- | ---: | ---: | ---: | ---: |
| 2-op, no feedback | 59,412 | 14,853 | 7,427 | 4.26% |
| 2-op, feedback | 62,489 | 15,622 | 7,811 | 4.49% |
| 4-op, feedback | 123,929 | 30,982 | 7,746 | 8.90% |
| 6-op, feedback | 181,274 | 45,318 | **7,553** | 13.01% |
| 6-op, 1 voice | 45,316 | 45,316 | 7,553 | 3.25% |
| 6-op, 12 voices | 525,378 | 43,782 | 7,297 | 37.71% |

**An operator costs 7,553 cycles per voice per block, and that figure barely
moves** — 7,427 to 7,811 across every configuration, including the step from
one voice to twelve. FM scales linearly in operators and in voices, with no
shared state to contend for. Feedback costs 5%: one multiply and one `q15_t`.

The number that reframes the engine question:

```
one Braids oscillator (HARMONICS)   45,139 cycles / voice / block
a six-operator FM voice             45,318 cycles / voice / block
```

**A full DX7-class voice costs the same as one Braids shape.** And the stock
FM-1 firmware's own configuration — 12 voices of 6 operators, 72 simultaneous
operators — fits in **37.7%** of our budget, with the filters, EQ and patchbay
still affordable on top.

### Why it is so cheap, when Braids is not

Two reasons, and neither is implementation quality.

1. **A Braids "shape" is usually several oscillators.** `TRIPLE_SAW` is three
   detuned saws; `HARMONICS` is an additive bank. Priced in operators, every
   Braids shape is already a small FM patch: `FILTERED_NOISE` at 15,129 is two
   operators, `HARMONICS` at 45,139 is six.
2. **Sines do not alias.** Band-limiting is the expensive part of a saw or a
   pulse — Braids pays for wavetable lookups, interpolation, and in places a
   correction term. A sine has one partial, so an FM operator is a table read,
   a linear interpolation and an add. The modulation *is* the add.

Phase modulation is the cheapest way to get a moving spectrum out of a chip
with no floating point, which is presumably why this chip is in this box.

### The primitive already existed

`fm1_osc_sine_pm()` has been in `dsp/shared/fm1_osc.c` since bootstrap,
validated against a double-precision sine by `dsp/tests/test_osc.c`. The
header's opening comment anticipated exactly this:

> *The six-operator engine, harmonic-spectrum editor and wavetables come later
> and plug in at the same seam.*

Making it usable took one change: `extern "C"` guards. The implementation is
C, the engine and probes are C++, and nothing had ever included the header
from C++ before — so the first attempt failed to link against a function
plainly present in the object file.

### What the table deliberately excludes

**Envelopes.** A DX7 voice carries one per operator, and ours is priced at
~4,575 cycles/voice/block (`SIGNAL_PATH.md` §8). Six of those is 27,450 —
*more than half again what the six operators cost*. Leaving them out is the
point: the envelopes, not the oscillators, are where a six-operator voice
gets expensive.

**Algorithm routing.** A serial stack is one hardwired topology. The DX7's 32
algorithms need a per-operator sum of selected modulator outputs — a few adds
per operator, small but not zero, and not measured here.

This is a cost measurement, not a design. Nothing is built.

## Provenance — take it from upstream, not from VCV

AudibleInstruments is two projects in one tree, and the distinction decides
our licence:

```
AudibleInstruments/
  src/        GPL-3.0-or-later  (c) Andrew Belt   <- Rack wrappers. NO DSP.
  res/        (c) Emilie Gillet, by permission    <- panel art. NOT open.
  eurorack/   MIT (c) Emilie Gillet               <- the actual algorithms.
```

`src/Braids.cpp` is parameter plumbing; every sample comes from
`eurorack/braids/`. We vendor from **`pichenettes/eurorack`** upstream
(`f4cfa76`, stmlib `e3bd7c9`), MIT, headers intact — so the port can live in
`dsp/` and stay reusable. Taking the same algorithm from VCV's `src/` or from
hortator would have made it GPL and confined it to `firmware/`.

Caveat inside `eurorack/` itself, per its README: **STM32F projects are MIT,
AVR projects are GPL-3.0.** Braids is STM32F. Check before porting anything
else.

`scripts/vendor-mi.sh` copies the minimal set and writes `dsp/mi/PROVENANCE.md`
with a SHA-256 per file.

## What it took to compile

The toolchain does support C++ (`clang++` is a symlink to `clang`,
`-target pi32v2`), but it is strictly freestanding — no libc headers, no
libstdc++. MI includes `<inttypes.h>`, `<cstring>`, `<algorithm>`, `<cstdlib>`
but uses almost nothing from them, so `dsp/mi/shim/` provides a few dozen
lines: the `stdint` typedefs, three `mem*` declarations (linked from Dinghy's
`libc.c`), and `min`/`max`/`swap`/`fill`/`fill_n`/`upper_bound`.

`<cmath>` and `<cstdio>` are deliberately **empty**, so any real use of libm
fails loudly at compile time instead of silently pulling in soft-float.

Compiled for pi32v2, `-O2 -ffreestanding -nostdlib -fno-exceptions -fno-rtti`:

| Object | Size |
| --- | --- |
| `fm1_braids.cc` (our adapter) | 1,032 B |
| `analog_oscillator.cc` | 4,864 B |
| `digital_oscillator.cc` | 28,056 B |
| `macro_oscillator.cc` | 4,860 B |
| `resources.cc` (all LUTs) | 60,172 B |
| `random.cc` | 392 B |
| **total** | **99,376 B (~97 KB)** |

`dsp/target/check.sh` confirms **no soft-float helpers and no 64-bit division**
in any of it. `resources.cc` is 400 KB of *source* but only 60 KB of data.

## The sample-rate problem — solved at source

**Resolved 2026-10-16: the tables are now regenerated at 44.1 kHz and
`kPitchCorrection` is 0.** `scripts/gen-braids-resources.sh` re-runs MI's own
generators with `sample_rate = 44100`, so every rate-dependent table is right:
pitch, SVF cutoffs, resonator tuning, bowing/blowing envelopes, granular rates
and oscillator delays. The interim pitch-offset hack is described below for the
record, because the reasoning still explains *why* the tables matter.

### Running MI's python2 generators

The generators are Python 2 and predate modern numpy, so the script copies them
to a temp directory and applies the minimum mechanical fixes, leaving
`references/eurorack` pristine:

| Fix | Why |
| --- | --- |
| `print x` → `print(x)` | py2 print statement |
| `builtins.xrange/unicode/long` | py2 builtins the compiler uses |
| `string.uppercase` → `ascii_uppercase` | renamed in py3 |
| `str.translate(table, delete)` → 3-arg `str.maketrans` | py2's 2-arg translate has no py3 equivalent |
| `file(…, 'wb')` → `open(…, 'w')` | `file` is gone; it writes text |
| `map(ord, …read())` → `list(…read())` | py3 binary reads already yield ints |
| `n = size / ratio` → `//` | int division used as a slice index |
| `.astype(int)` on fmod results | numpy ≥ 1.12 rejects float indices |
| `sys.path` insert | py2 implicit relative imports |

### Validating the pipeline

Correctness was established by **regenerating at 96000 and diffing against
upstream's committed `resources.cc`**:

```
upstream tables: 45   ours: 45
ALL DATA TABLES IDENTICAL -- the diff was declarations only
```

The only textual differences are `const X*` vs `const X* const` in the index
arrays, because the committed file predates a const-correctness change in
stmlib's compiler. Every one of the 45 data tables matches byte for byte.

That check immediately caught a real bug. `lookup_tables.py` **reassigns
`sample_rate = 48000` partway through**, in the envelope-increments section,
deliberately pinning envelope rates to 48 kHz independently of the audio rate.
A naive `s/^sample_rate = .*/` replaced both and silently corrupted
`lut_env_portamento_increments` — 127 of 128 entries wrong. The generator now
substitutes `count=1` and says so:

```
sample_rate: 2 assignment(s) present, first set to 44100
```

Without the 96 kHz round-trip that corruption would have shipped unnoticed.

### Result

| | Before (96 kHz tables) | After (44.1 kHz tables) |
| --- | --- | --- |
| `kPitchCorrection` | 1724 (13.47 semitones) | **0** |
| Pitch | correct, via the offset | correct natively |
| Envelopes, filters, grains | **2.18× fast** | correct |
| `resources.cc` object | 60,172 B | 58,964 B |

Measured after regeneration, TRIPLE_SAW at timbre 0, with no correction:

| MIDI | Expected | Measured |
| --- | --- | --- |
| 36 | 65.41 Hz | 65.1 Hz |
| 48 | 130.81 Hz | 131.1 Hz |
| 60 | 261.63 Hz | 261.5 Hz |

The physical models now behave too — `plucked` at MIDI 40 shows the harmonic
series of 82.4 Hz, `bowed` at 414.3 Hz (5× 82.9), `struck_bell` inharmonic as
it should be.

`dsp/mi/braids/resources.cc` and `.h` are therefore **generated, not copied**;
`vendor-mi.sh` no longer copies them and `dsp/mi/braids/RESOURCES_GENERATED.md`
records it.

## Appendix: the interim pitch-offset approach

Braids runs at 96 kHz (`braids.cc: sys.Init(F_CPU / 96000 - 1, true)`); the
FM-1 runs at 44.1 kHz. Its pitch table is generated by
`braids/resources/lookup_tables.py` as

```python
sample_rate = 96000
increments = excursion / sample_rate * pitches     # excursion = 2**32
```

Replay those increments at 44.1 kHz and

```
f_out = increment * 44100 / 2^32 = f(pitch) * 44100 / 96000
```

— **flat** by `12 * log2(96000/44100) = 13.4676` semitones. Asking the table
for a note that much *higher* returns exactly the increment 44.1 kHz needs, so
a constant pitch offset of `13.4676 * 128 = 1724` units cancels it, with a
0.12-cent residual. No table is regenerated.

Measured, TRIPLE_SAW at timbre 0:

| MIDI | Expected | Measured |
| --- | --- | --- |
| 36 | 65.41 Hz | 65.1 Hz |
| 48 | 130.81 Hz | 131.1 Hz |
| 60 | 261.63 Hz | 261.5 Hz |

with clean saw harmonic series above each. The residual is `wavstat.py`'s
~17-cent log-bin resolution, not pitch error.

> I first applied this correction with the wrong sign, and the render came out
> 26.94 semitones flat — exactly twice 13.47. The octave ratios were still
> perfect, which is what identified it as a constant offset rather than a table
> problem. Worth keeping as a reminder that "it sounds wrong" is much less
> useful than "it is wrong by exactly 2x the correction".

### What the offset does and does not fix

**Pitch is correct for every shape.** Both `AnalogOscillator` and
`DigitalOscillator` read `lut_oscillator_increments`, so one correction covers
all of them — verified: `saw_swarm` and `harmonics`, both digital shapes, come
out at the right fundamental.

**Character is still wrong for shapes calibrated in seconds or absolute Hz.**
The SVF and resonator coefficients, bowing/blowing envelopes, granular rates
and oscillator delays are still 96 kHz tables and run 2.18× fast. Affected
shapes are listed in `shape_needs_rate_retune()` — the physical models,
filtered, vowel and granular ones. They are *in tune but wrong in timbre*.

Fixing them is a contained job: `lookup_tables.py` is Python-3 clean and
derives every one of those tables from the single `sample_rate` variable.
Re-running it at 44100 is the next step for those shapes. (`waveforms.py` has
a Python-2 `print`, but waveform tables are rate-independent.)

## Cost on the real CPU, measured in the emulator

`scripts/measure-braids-cycles.sh` builds `dsp/target/probe_braids.cc` as a
flat pi32v2 image, runs it in fm1-emu, and subtracts a zero-block build from a
64-block build so setup, `Init()`, the `.data` copy and the harness all cancel.

The emulator clocks the AC791N at 240 MHz, one instruction per cycle, so
retired instructions are the cycle estimate. **One block is 256 frames at
44.1 kHz = 5.805 ms = 1,393,152 cycles.**

> **This is optimistic.** One instruction per cycle ignores XIP flash stalls —
> and every Braids lookup table lives in flash — plus RAM contention with core
> 1 and interrupt overhead. Treat it as a lower bound on time and therefore an
> upper bound on voice count. Halve it again for RULES.md's "stay under half
> the budget" advice.

### Getting the probe to run at all

Three things the upstream probe harness does not cover, all worth knowing
before writing another probe:

1. **`main` is mangled.** This clang emits `_Z4mainv` in C++ mode while
   `crt0.S` calls `main`. Declare it `extern "C"`.
2. **`.data` is never copied.** Upstream's probes are plain C with no
   initialised globals. Braids' three `fn_table_` dispatch arrays *are*
   initialised data, so without a copy they read as zeros and `Render()` jumps
   to `0x00000000`. `dsp/target/probe.ld` adds `_data_start/_end/_load` and the
   probe copies them. (A real firmware's crt0 does this.)
3. **The image needs `.data` too.** `objcopy -O binary -j .text` drops it, and
   this LLVM objcopy accepts exactly one `-j`, so the two sections are
   extracted separately and concatenated — `.data`'s load address follows
   `.text` directly.

Also: `--break-pc` *reports* without halting, and crt0 ends in `goto .`, so the
breakpoint re-fires forever. The first break line carries the count; a second
bounded run with `--steps n+8 --dump` reads the results block back to confirm
the probe actually completed.

### Results

Measured on the emulated pi32v2, one voice, per 256-frame block:

| Shape | cycles/block | µs/block | % of budget | voices at 100% |
| --- | --- | --- | --- | --- |
| FILTERED_NOISE | 15,129 | 63.0 | 1.1% | 92 |
| BOWED | 19,190 | 80.0 | 1.4% | 72 |
| SAW_COMB | 19,788 | 82.4 | 1.4% | 70 |
| WAVETABLES | 20,348 | 84.8 | 1.5% | 68 |
| TRIPLE_SINE | 23,553 | 98.1 | 1.7% | 59 |
| PLUCKED | 25,211 | 105.0 | 1.8% | 55 |
| SAW_SWARM | 29,313 | 122.1 | 2.1% | 47 |
| FLUTED | 29,702 | 123.8 | 2.1% | 46 |
| GRANULAR_CLOUD | 29,927 | 124.7 | 2.1% | 46 |
| VOWEL_FOF | 30,777 | 128.2 | 2.2% | 45 |
| TRIPLE_SAW | 35,341 | 147.3 | 2.5% | 39 |
| HARMONICS | 45,139 | 188.1 | 3.2% | 30 |

**Four voices of the most expensive shape is 12.9% of the budget.** Even
assuming the 1 insn/cycle model is 3× optimistic, four HARMONICS voices land
near 39% — still inside RULES.md's "stay under half" advice.

The spread is only 3× across the whole catalogue, so shape choice is a sound
decision, not a CPU one. The physical models are *cheaper* than the analog
triples, which is counter-intuitive until you notice `TRIPLE_*` runs three
oscillators per voice while BOWED runs one.

**Conclusion: CPU is not a constraint at four voices.** Neither is memory. The
instrument is bounded by design and taste.

### Memory, for completeness

| Object | Size |
| --- | --- |
| `braids::AnalogOscillator` | **36 B** |
| `braids::DigitalOscillator` | **16,856 B** |
| `braids::MacroOscillator` | 17,064 B — 99% of it is DigitalOscillator |
| `fm1::BraidsVoice` | 17,152 B |

### The device has 512 KiB of RAM, not 96

It is easy to read the build summary — `RAM .data+.bss 12368 B of 98304` — as
the whole budget. It is not. The FM-1 has **512 KiB** of data RAM at
`0x01C00000` (`fm1-soc/src/lib.rs`), carved up by
`references/dinghy/firmware/app.ld`:

| Region | Base | Size | Dinghy uses | Free |
| --- | --- | --- | --- | --- |
| RAMTEXT | `0x01C00000` | 24 KiB | flash driver | — |
| *(debug gap)* | `0x01C06000` | 8 KiB | — | — |
| **RAM** `.data/.bss` | `0x01C08000` | 96 KiB | 12,368 B | **~84 KiB** |
| **POOL** `.pool` | `0x01C20000` | 336 KiB | 30,720 B (canvas) | **~306 KiB** |
| user stack | `0x01C74100` | ~24 KiB | — | — |
| supervisor stack | `0x01C7A100` | ~8 KiB | — | — |
| NOINIT | `0x01C7C000` | 15 KiB | survives reset | — |
| reserved | `0x01C7FD50` | 688 B | boot_info, vectors | — |
| | | **512 KiB** | **~42 KiB** | **~390 KiB** |

Dinghy occupies about **42 KiB of 512**, leaving roughly **390 KiB** for us
across the two regions we can use.

| Voices | Full MacroOscillator | in `.bss` | in `.pool` |
| --- | --- | --- | --- |
| 4 | 68,608 B | tight | ok |
| 6 | 102,912 B | over | ok |
| 12 | 205,824 B | over | ok |
| 16 | 274,432 B | over | tight |

So one `MacroOscillator` per voice caps at five voices *only if the array is
left in `.bss`*. Put it in `.pool` —

```c
static fm1::BraidsVoice voices[N] __attribute__((section(".pool")));
```

— and sixteen fit. The pool is zero-initialised at boot and `Init()` runs
anyway, so nothing is lost by doing so.

### Why that cap does not matter

It is tempting to read "five voices" as a limit to engineer around, by building
voices on `AnalogOscillator` (36 B) instead and getting hundreds. That would be
optimising the wrong thing.

Four oscillators is already a lot for a drone instrument. A tanpura has four
strings; a shruti box a handful of reeds; a Subharmonicon makes six tones
total. Drone richness comes from beating between a few detuned sources, slow
spectral movement and resonance — not from voice count.

And the count is already higher than it looks: the `TRIPLE_*` shapes are
**three detuned oscillators each**, so four voices is twelve oscillators, and
`SAW_SWARM` is a supersaw on top of that.

Trading `MacroOscillator` for `AnalogOscillator` would buy voices nobody needs
at the cost of every interesting drone timbre — BOWED, FLUTED, VOWEL_FOF,
GRANULAR_CLOUD all live in `DigitalOscillator`, which is the 16.8 KB.

**Design consequence: keep the full `MacroOscillator` per voice, and use four.**
Every voice can then be any of the 48 shapes.

### Budgeting voices against effects

The pool is declared for exactly this kind of buffer:

```c
static int16_t delay[65536] __attribute__((section(".pool")));
```

A drone box wants a long reverb, and that is the real competitor for pool
space. At 44.1 kHz, 16-bit mono, 306 KiB is about **3.5 seconds** of delay
line — generous. A reasonable split for four voices:

| | |
| --- | --- |
| 4 voices in `.bss` | 68.6 KiB of 84 KiB — leaves ~15 KiB for UI and modulation |
| reverb + delay in `.pool` | up to ~306 KiB |

or, if the modulation and UI state grows, move the voices into `.pool` too and
split it ~69 KiB voices / ~237 KiB effects, freeing the whole 84 KiB `.bss`.

Either way the binding constraint is **CPU and taste, not bytes**.

## Try it

```sh
wsl -d archlinux
cd /mnt/c/temp/GitHub/fm1-laboratory/dsp

make braids             # one drone note -> build/braids-triple_saw-36.wav
make braids-all         # every candidate drone shape
make braids-bench       # cost per voice
make braids-ram         # footprint and polyphony limits
make target             # prove it all still builds for pi32v2
./build/render_braids --list
./build/render_braids --shape saw_swarm --note 29 --timbre 24000 --sec 8 --out drone.wav
```

18 auditioned shapes are in `artifacts/dsp/braids/`.

## Triggering

14 of `DigitalOscillator`'s 34 render paths consume a `strike_` flag and are
silent or static without one:

`Plucked` · `StruckBell` · `StruckDrum` · `Kick` · `Snare` · `Bowed` ·
`Blown` · `Fluted` · `Vowel` · `SawSwarm` · `ClockedNoise` ·
`DigitalModulation` · `WaveParaphonic` · `QuestionMark`

`Strike()` just sets the flag; the next `Render` consumes it. `BraidsVoice`
exposes it, so MIDI note-on maps straight onto it. The host renderer has
`--strike MS` to re-trigger on a block boundary, which is how the firmware will
do it (note-on handled in the main loop, flag seen by the ISR on its next
block).

```sh
./build/render_braids --shape plucked --note 40 --sec 4 --strike 700
```

## Next

1. Get a cycle count from `fm1-emu` (`--trace`, `--break-pc`) for a real
   per-voice budget instead of host-relative numbers.
2. Design the MIDI CC map, modelled on obervave's `pdef_t` table
   (`docs/PLATFORM_SURVEY.md`) — voice-per-channel, note-on → `Strike()`.
3. Build the drone voice: four `MacroOscillator`s with slow detune, drift and
   spectral evolution, plus the tuned comb/resonator section.
