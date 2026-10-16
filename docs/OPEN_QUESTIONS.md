# Open questions

Things we do not know, ordered by how much they could cost us. Each says what
would settle it.

---

## Q1 — The pi32v2 toolchain download is not pinned

`https://pkgman.jieliapp.com/s/linux-toolchain` serves whatever is current. We
received `jieli-linux-toolchains-20260730.1`; fm1-emu's `docs/recon.md`
documents `20250324.1`. There is no documented way to request a version.

**Why it matters.** It is the one build input we cannot reproduce on demand.
A different compiler could change code generation, timing, or image size.

**Mitigating evidence.** Our Dinghy 0.3 build matched the published release
byte for byte with `20260730.1`, so it is compatible with what the author used.

**Settle it by:** archiving `~/.jieli/jieli-linux-toolchains-20260730.1`
(136 MB) somewhere durable and recording its SHA-256. Do this before depending
on a build. The setup script already refuses to re-download over an existing
install.

---

## Q2 — Cycle cost on the target — ANSWERED (emulator), pending hardware

`scripts/measure-braids-cycles.sh` runs a flat pi32v2 probe in fm1-emu and
subtracts a zero-block build from a 64-block build. One voice per 256-frame
block costs **15,129 cycles (FILTERED_NOISE) to 45,139 (HARMONICS)** against a
1,393,152-cycle budget — **1.1% to 3.2%**. Four voices of the worst shape is
12.9%. Full table in `docs/BRAIDS_PORT.md`.

**CPU is not a constraint at four voices.** That resolves the practical
question.

**What is still open** is the fidelity of the number. The emulator models one
instruction per cycle, which ignores XIP flash stalls — and every Braids
lookup table is in flash — plus RAM contention with core 1 and interrupt
overhead. The true figure could plausibly be 2–3× worse, which still leaves
four voices inside the budget.

**Settle it by:** `fm1_ticks()` around `app_audio()` on hardware, compared
against the emulator for the same workload. That also answers Q6.

---

## Q3 — Two unmodelled MMIO registers

Dinghy hits `0x00013e08` and `0x00013e0c` about 14 957 times per 3 s and the
emulator does not model them:

```
read  @ 0x00013e08 x7479     write @ 0x00013e08 x3739
read  @ 0x00013e0c x1870     write @ 0x00013e0c x1869 (first value 0x20)
```

Audio and display were correct regardless, so they are likely benign for this
firmware — perhaps a timer or a USB status register.

**Why it matters.** It is a concrete, measurable boundary of emulator fidelity,
and if our firmware ever depends on these, the emulator will quietly lie.

**Settle it by:** searching Felucca's `hal/` and the AC79 SDK headers for the
address; both are on disk. Worth half an hour before the first real engine.

---

## Q4 — The reference device policy

Two units are on order, one intended to stay boxed. A sealed unit cannot
provide a stock flash dump or reference recordings. See `FIRMWARE_SAFETY.md`.

**Decision needed from Nico**, but not yet: it only becomes live when the units
arrive. Current recommendation is to keep B sealed until a read-only dump has
succeeded on A.

---

## Q5 — Recovery beyond the USB rescue is unproven

Dinghy's OCT− rescue is verified by its author on hardware. The mask-ROM path
needs an RP2040 USB_KEY dongle; `jl-uboot-tool` lists WL82/AC791N support as
"unknown"; lunar-modulator states that restoring a whole image "remains
untested".

**Settle it by:** building the USB_KEY dongle and proving a read-only flash
dump *before* the first write, not after a brick.

---

## Q6 — Does the emulator's timing model match the device?

Even if the instruction semantics are right, the interrupt cadence, SPI cost
and flash stalls may not be. Code tuned against the emulator's budget could
still miss the real deadline.

**Settle it by:** comparing `fm1_ticks()` on hardware against the emulator for
the same workload, once a device exists. Until then, leave generous headroom —
RULES.md's "under half the budget" is the right instinct.

---

## Q7 — Six-operator FM headroom is assumed, not measured

The stock firmware runs a 6-op DX7 core with up to 12 voices across both cores,
which is strong evidence the hardware can do it. But the stock engine is
hand-tuned assembly-adjacent C by people with the datasheet, and it uses
**both** cores.

Our Dinghy build uses one core. Whether our target architecture (6 operators,
arbitrary harmonic spectra per operator, wavetables) fits in one core at useful
polyphony is unknown.

**Settle it by:** building up from one operator and measuring, rather than
designing for six and discovering the cost late. This is why the recommended
first milestone is a single oscillator.

---

## Q8 — Dual-core use is entirely unexplored

The AC791N has two pi32v2 cores. The stock firmware and FM-106 use both;
Dinghy uses one. We have read nothing about how Dinghy's platform would
schedule the second core, or whether it is even initialised.

**Settle it by:** reading Felucca's and SLOOP's core-1 handling. Defer — single
core first, and only reach for the second when measurement says we need it.

---

## Q9 — Wavetable storage strategy

~526 KB of flash is free, but flash is XIP at `0x02000000` and a flash erase
stalls everything including audio for tens of milliseconds. Reading wavetables
from XIP flash inside the audio ISR may or may not be fast enough.

**Why it matters.** It decides whether procedural wavetables are generated into
RAM at patch-change time (needs RAM, costs a glitch) or read from flash
continuously (needs predictable XIP read latency).

**Settle it by:** a targeted experiment once the oscillator exists — render the
same table from RAM and from flash and compare block times.

---

## Q10 — Our tuning verification is circular at the edges

`make test` compares the integer oscillator against a `double` sine computed
from the same phase-increment constant. That validates the table and the
interpolation; it does **not** independently validate the constant itself.

The tuning test does check the increment against an ideal ratio computed
separately, which breaks most of the circularity. But a test that rendered a
known-pitch tone and measured it with an independent analyser would be
stronger.

**Settle it by:** extending `wavstat.py` into a proper high-resolution pitch
estimator and asserting on its output in CI. Low priority; the current error
bound (0.001 cent) is far below audibility.

---

## Resolved during bootstrap

| Question | Answer |
| --- | --- |
| Does a usable pi32v2 compiler exist and work here? | Yes — clang 4.0.1, verified by compiling and disassembling |
| Can we build an existing firmware unmodified? | Yes — Dinghy 0.3, **bit-identical to its release** |
| Can the emulator run locally built firmware? | Yes — verified with our own `.fwsc` |
| Can we hear our own DSP without hardware? | Yes — three independent ways |
| Is Dinghy the right base? | Yes — and it is GPL-3.0-only, which decides our licence |
| Windows or WSL? | Both: emulator native on Windows, toolchain in WSL, measured |
| Is `/mnt/c` too slow? | For builds no (~1 s). For git yes (~160×). Documented. |
