# FM-1 hardware architecture

Compiled from `references/fm1-emu/docs/recon.md`, `references/FM-1-RE`,
`references/lunar-modulator/docs/`, `references/dinghy/docs/`, and what our own
toolchain run confirmed directly.

**We own no FM-1 yet.** Everything below is from community reverse engineering
or from our own emulator and compiler observations. Nothing here has been
checked against a device by us. Confidence is marked per claim.

| Mark | Meaning |
| --- | --- |
| **[verified here]** | We observed it ourselves during this bootstrap |
| **[community]** | Consistently documented by two or more upstream projects |
| **[single source]** | One project asserts it; not cross-checked |

## The chip

| | | |
| --- | --- | --- |
| SoC | JieLi (珠海杰理) **AC791N / WL82** | [community] |
| CPU | **pi32v2**, JieLi's proprietary 32-bit ISA, **dual core** | [community] |
| Clock | 240 MHz | [community] |
| Flash | **1 MB, single bank**, XIP mapped at `0x02000000` | [community] |
| RAM | **512 KiB** at `0x01C00000` — see the map below | **[verified here]** |
| FPU | **none** | **[verified here]** |
| Debug | **no recovery button, no test pads, no debug header** | [community] |

The embedded string `JL-BR22` is inherited library naming, **not** the SoC ID.

### The RAM map

Data RAM spans `0x01C00000`–`0x01C80000` = **512 KiB**, corroborated by two
independent sources: `references/fm1-emu/crates/fm1-soc/src/lib.rs`
("data RAM (≥512 KiB)") and the regions in
`references/dinghy/firmware/app.ld`.

| Region | Base | Size | Purpose |
| --- | --- | --- | --- |
| RAMTEXT | `0x01C00000` | 24 KiB | code that must run while flash is busy |
| *(debug gap)* | `0x01C06000` | 8 KiB | reserved for RAM-run debug |
| **RAM** `.data/.bss` | `0x01C08000` | 96 KiB | ordinary application state |
| **POOL** `.pool` | `0x01C20000` | 336 KiB | big zeroed buffers: FX lines, canvases |
| user stack | `0x01C74100` | ~24 KiB | 256-byte guard bands either side |
| supervisor stack | `0x01C7A100` | ~8 KiB | |
| NOINIT | `0x01C7C000` | 15 KiB | survives resets and UBOOT entry |
| reserved | `0x01C7FD50` | 688 B | boot_info, boot-state mailbox, vectors |

Only the two bold regions are ours. **Do not mistake the 96 KiB `.data/.bss`
figure for the whole budget** — the pool is nearly four times larger and is
declared for exactly the big buffers a synth wants:

```c
static int16_t delay[65536] __attribute__((section(".pool")));
```

### Budget, measured

From our own Dinghy build:

```
image 55076 B of 581564            (9.5% of the app flash region)
RAM .data+.bss 12368 B of 98304    (12.6%)
pool 30720 B of 344064             (8.9%)
```

A minimal firmware therefore occupies ~42 KiB of the 512 KiB of RAM, leaving
roughly **526 KB of flash and ~390 KiB of RAM** (84 KiB `.bss` + 306 KiB pool)
for a synthesis engine.

## The pi32v2 instruction set

**[verified here]** — we compiled C and disassembled it with the vendor tools.

* ELF32, little-endian, `e_machine = 0xf1` (241). Standard `binutils` does not
  know it: `file` reports *unknown arch 0xf1*.
* Variable-length encodings, 2 and 4 bytes in our sample.
* Blackfin-style **algebraic assembly**, not mnemonic. Real output from
  `scripts/verify-toolchain.sh`:

```
fill:
   6:  75 04          [--sp] = {rets, r5, r4}
   8:  13 16          r3 = r1
   c:  03 49          if (r3 == 0) goto 18
  14:  71 95          call -22 <scale>
  16:  d8 ec 4b 05    [r4+r5<<2] = r0
  1c:  85 e8 f8 31    if (r3 != r5) goto -16
  20:  55 04          {pc, r5, r4} = [sp++]
```

Note `[r4+r5<<2] = r0` — scaled-index addressing in one instruction, which is
good news for table lookups, and multi-register push/pop in one slot.

Registers seen: `r0`–`r5` (argument/scratch), `sp`, `rets`, `pc`. Return is
`rts`, or popping `pc` off the stack.

`fm1-emu`'s `docs/isa-notes.md` additionally documents hardware loops
(`rep N rN { ... }`), fused decrement-and-branch, `smin` as a single
instruction, and signed-conditional `ifs (...)` forms. **[single source]**

## Programming constraints that shape every design decision

From `references/dinghy/docs/RULES.md`, and the reason `dsp/shared/` looks the
way it does:

1. **No `float`, no `double`.** No FPU *and* no libgcc soft-float: a single
   float operation fails the **link**, not the compile. Fixed point is not an
   optimisation here, it is the only option.
2. **No 64-bit division or modulo.** `__udivdi3` does not exist. 64-bit
   *multiplication* is fine — `dsp/shared/fm1_fixed.h` relies on that to
   compute phase increments without dividing.
3. **Division by zero does not trap**, it returns a wrong value, because the
   compiler may hoist a division above the test guarding it. Check divisors.
4. **No constant may fall in `0xFFC00000`–`0xFFCFFFFF`** (chip ROM). The build
   rejects it as a possible ROM call even when it is arithmetic coincidence:
   `-32767 << 7` is `0xFFC00080`. Clamp before shifting, not after.
5. **Hardware registers only inside `hal/`.** Enforced by the build.

`dsp/target/check.sh` enforces 1 and 2 against our own DSP code *before* it
reaches a firmware build, so the failure names the file instead of surfacing as
a missing libgcc symbol at link time.

## Audio

| | | |
| --- | --- | --- |
| Block | **256 frames** per `app_audio()` call | [community] |
| Rate | 44.1 kHz (USB audio also does 48 kHz) | **[verified here]** |
| Budget | **5.8 ms** per block; aim under 2.9 ms | [community] |
| Format | 16-bit stereo, Q15 internally | [community] |
| Out | built-in speaker + headphone jack (jack mutes speaker) | [community] |

The ISR is interrupted by USB and the key scan, so the real budget is less than
5.8 ms. Per-frame work should be only what must be per frame; envelopes, filter
coefficients and LFOs belong on a 16- or 32-frame subdivision.

Our emulator run produced **488 audio halves in 3 s** — consistent with 256-frame
double-buffered blocks at 44.1 kHz. **[verified here]**

## Panel

| | |
| --- | --- |
| Display | 240×240 colour TFT over SPI; a full redraw is ~115 KB |
| Keys | 27 silicone keys |
| Buttons | 14, with LEDs |
| Encoders | 7 rotary, plus a MASTER volume pot |
| MIDI | TRS MIDI **in**; MIDI out probably not wired |
| USB | USB-C: MIDI, audio, and the update path |
| Wireless | BLE, MIDI-capable, unused by most firmwares |

Screen output confirmed **[verified here]** — the emulator rendered Dinghy's
240×240 panel with the correct note name and highlighted keys.

## Stock firmware

* A port of Google's **msfa** (the Dexed core): 6 operators, 32 algorithms,
  128 presets, up to 12 voices. [community]
* Speaks Yamaha DX7 SysEx, so DX7 banks load directly. [community]
* Current version V15 (2026-07-30); V13/V14 images preserved in FM-1-RE.

That the stock engine is a 6-operator DX7 core running on both cores is the
most useful single data point for our own ambitions: **the hardware is already
known to sustain 6-op FM polyphony.**

## Boot and update

* Firmware updates arrive over **USB-MIDI SysEx**, protected by **CRC-16 only —
  no cryptographic signature**. The version gate lives in the host updater, not
  the device. [community]
* Package format is the `.fwsc` container.
* The OTA loader can rewrite the flash head (`uboot.boot`, `isd_config.ini`).
  **Keep those byte-identical to stock.** [community]
* Mask-ROM USB recovery enumerates as **WL80UBOOT**, USB ID `4C4A:8057`.

Safety consequences, including the one SysEx message that must never be sent by
hand, are in **`FIRMWARE_SAFETY.md`**. Read that before connecting a device.

## What we have not verified

* Any claim about **physical** behaviour — we have no device.
* Whether the two unmodelled MMIO registers at `0x00013e08` / `0x00013e0c` that
  Dinghy hits ~15 000 times per 3 s matter on real silicon.
* Actual cycle costs. Our benchmark is host-relative only; a 240 MHz pi32v2
  figure needs `fm1_ticks()` on hardware or a cycle count from the emulator.
* Whether the emulator's timing model is accurate enough that code tuned to its
  budget will meet the real ISR deadline.

See `OPEN_QUESTIONS.md`.
