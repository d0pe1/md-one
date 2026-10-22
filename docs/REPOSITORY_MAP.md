# Repository map

Seven upstream projects, cloned 2026-10-16 into `references/`. Every one is at
the SHA below; `scripts/clone-references.ps1` reproduces the set.

All of these are **read-only references**. Nothing in `references/` is edited.
Our own work goes in `dsp/`, `firmware/`, `scripts/` and `docs/`.

| Repo | SHA | Branch | Licence | Role |
| --- | --- | --- | --- | --- |
| [joakimeriksson/fm1-emu](https://github.com/joakimeriksson/fm1-emu) | `7196a9f` | master | MIT | **Execution target.** CPU emulator + batch runner. |
| [zvenson/dinghy](https://github.com/zvenson/dinghy) | `d44df7d` | main | GPL-3.0-only | **Firmware base.** Minimal platform + host mode. |
| [hugelton/Felucca](https://github.com/hugelton/Felucca) | `a53ecf4` | main | GPL-3.0 | Platform origin; `hal/` is the peripheral documentation. |
| [AL-255/FM-1-RE](https://github.com/AL-255/FM-1-RE) | `ec832f2` | main | WTFPL | Stock-firmware RE: boot chain, memory map, OTA protocol. |
| [ip2k/lunar-modulator](https://github.com/ip2k/lunar-modulator) | `bfa0424` | main | MIT | Mutable-Instruments prior art; hardware and recovery notes. |
| [DeadActive/hortator](https://github.com/DeadActive/hortator) | `7e34159` | main | GPL-3.0-only | Integer Plaits drum ports — proof the algorithms fit. |
| [cicloid/awesome-fm-1](https://github.com/cicloid/awesome-fm-1) | `b310034` | main | CC0-1.0 | Index of the wider ecosystem. |

Commit dates run from 2026-09-08 to 2026-10-15: this ecosystem is a few weeks
old and moving fast. Re-pin deliberately rather than drifting.

---

## A. fm1-emu — our execution target

Emulates the JieLi AC791N: both pi32v2 cores, timers, interrupts, flash, the
240×240 screen, key matrix, encoders, knobs, audio and the MIDI UART. Runs 47
community firmwares unmodified.

Crates that matter to us:

| Crate | What |
| --- | --- |
| `fm1-isa` | Instruction decode tables, generated from `isa/fm1.yaml` |
| `fm1-core` | CPU and the AArch64 / WASM JITs |
| `fm1-soc` | Chip and board: peripherals, LCD, audio, keys |
| **`fm1-emu`** | **Batch runner — the one we use.** No external deps. |
| `fm1-live` | Native windowed synth with real audio out |
| `fm1-web` | The wasm build behind the GitHub Pages player |
| `fm1-plugin` | AU/VST3/CLAP. Separate workspace, macOS-only, **out of scope** |

`fm1-emu` is the automation hook: it loads a `.fwsc`, runs a scripted panel
performance and writes a WAV and screen snapshots, with no GUI. That is what
makes a headless edit→build→listen loop possible. See `BUILD_AND_TEST.md`.

Docs worth reading: `docs/recon.md` (hardware, condensed and sourced),
`docs/isa-notes.md` (26 KB of instruction-set reconstruction),
`docs/stock-firmware.md`, and `PLAN.md` (the work log, including what is *not*
done).

### Known limits — do not treat the emulator as the device

**Measured against hardware, 2026-10-22.** The emulator earned a great deal
of trust this day and lost it on exactly two axes. Both are worth knowing
before relying on a green run.

| | Verdict |
| --- | --- |
| Logic, drawing, audio generation, navigation | **trustworthy.** Nine menus, a modulation matrix, an EQ curve and a two-slot voice chain all worked first time on silicon |
| Timing | **useless.** It counts instructions and calls them cycles. It reported 21.8% where the device reports **42%** — 1.92x out, because it models no XIP flash stall at all |
| Anything it idealises about hardware state | **actively misleading.** See below |

The second failure is the one that matters, because it was silent and it hit
the safety path. The OCT− rescue trigger inherited from Dinghy reaches the
rescue screen in the emulator **every time**, and does not fire on hardware
**at all** — three cold boots, button held before power, straight to the home
screen. The cause is that `recovery_key()` hand-polls the key matrix
milliseconds after power-on, when the shift register driving it still holds
whatever it powered up with; the emulator returns clean settled state on the
first frame and so can never reproduce it.

The rule that follows: **a green emulator run proves the logic, never the
timing, and never anything that depends on hardware settling.** Those need
the device, and this project now has one.

The author is explicit that the ISA was recovered from firmware images, not
from a vendor manual. Our own run surfaced one concrete gap:

```
14957 unknown MMIO accesses at 4 addresses (bring-up list, first 24):
  read  @ 0x00013e08   write @ 0x00013e08
  read  @ 0x00013e0c   write @ 0x00013e0c
```

Dinghy touches two unmodelled registers ~15 000 times in 3 seconds and still
sounds correct, so they are probably benign for this firmware — but *probably*
is the operative word. See `OPEN_QUESTIONS.md` (Q3).

Two Windows-specific issues we hit, neither requiring an upstream change:

* `tools/fetch_firmwares.mjs` derives its root from
  `new URL("..", import.meta.url).pathname`, which is `/C:/...` on Windows and
  resolves to `C:\C:\...`. Replaced by `scripts/fetch-firmware.ps1`.
* `--snap` takes snapshot **times in ms**, not a filename, and writes to a
  hardcoded relative `work/` directory with the error discarded — so a missing
  directory silently yields no screens. Handled by `scripts/run-emu.ps1`.

---

## B. dinghy — our firmware base

**Recommended, but see `PLATFORM_SURVEY.md` for why.** Dinghy was suggested
from outside this project, so it was checked against nine other firmwares.
Summary: **no shipped firmware uses Dinghy as a base** — the ecosystem is on
Felucca's platform, reached directly or via SLOOP. Dinghy is not a competing
base; it is that same platform with the application removed, and its HAL
matches the ecosystem majority on all 17 files. That is why it stands, not
popularity.

```
firmware/src/dinghy.c     ~200 lines: a 4-voice sine. The part we replace.
firmware/src/platform.c   boot, USB, updates, audio ISR, settings. Done.
firmware/src/*.c, hal/    Felucca's platform as SLOOP carries it, unchanged
```

What it already solves, so we never have to:

* USB firmware update, compatible with the FM-1 Firmware Hub and the web
  installers — including the path *back* to stock firmware;
* USB rescue (hold **OCT−** at power-on; automatic after two boot crashes);
* class-compliant USB-MIDI in/out, and USB audio as a 16-bit stereo input;
* the panel by name — 27 keys, 14 buttons, 7 encoders, MASTER, LEDs;
* 240×240 screen primitives; settings in flash with a CRC; a crash screen
  showing the faulting PC.

Two features matter more than the feature list:

**Host mode.** `tests/run_host.sh` compiles the same `dinghy.c` with the host
compiler and renders `build/host/dinghy.wav` plus screen PNGs, with a MIDI
trace. Verified working. This is an independent third test layer, faster than
the emulator and far faster than hardware.

**Build-time rule checks.** `./build.sh` fails on exactly the things that brick
or break an FM-1 firmware: 64-bit division, a call into chip ROM, too much RAM,
a hardware register touched outside `hal/`. Our build showed all four passing:

```
ok  .ram_text: 901 insns, no calls
ok  image 55076 B of 581564; RAM .data+.bss 12368 B of 98304; pool 30720 B of 344064
ok  register access: hal/ only (src/, loader/ clean)
```

Headroom for our engine: **526 KB of flash** and **~390 KiB of RAM** — 84 KiB
of `.data/.bss` plus the 306 KiB left in the `.pool` region. The device has
512 KiB of RAM in total; the "98304" above is only the `.bss` region, not the
whole budget (see `HARDWARE_ARCHITECTURE.md`).

Licence consequence: Dinghy is **GPL-3.0-only**. Building on it makes our
firmware GPL-3.0-only too. See `LICENSE_REGISTER.md` — this is the single most
consequential decision in the bootstrap.

---

## C. Felucca — platform origin

The original open FM-1 firmware; a four-operator FM synth with sequencer,
arpeggiator and effects, 257 files. Dinghy's platform *is* Felucca's, by way of
SLOOP, so the lineage is Felucca → SLOOP → Dinghy.

Read it for `hal/` — the de-facto peripheral documentation, since no vendor
datasheet is public — and for a worked example of a complete synth on this
hardware. Do not re-implement drivers that already exist there.

---

## D. FM-1-RE — stock firmware reverse engineering

395 files: architecture overview, boot chain, memory layout, the OTA protocol,
a V13 disassembly-and-reassembly pipeline, function databases, Ghidra scripts
for pi32v2, and preserved V13/V14 firmware images.

This is an **analysis** repository, not a flashable firmware project. Its value
to us is the boot/flash/update ground truth behind `FIRMWARE_SAFETY.md`,
including the SysEx command that must never be sent by hand.

Licence is WTFPL — effectively unrestricted, but the *firmware images* in it
are M-VAVE's work, not the repo author's to relicense.

---

## E. Synthesis prior art — treat as unverified

**lunar-modulator** (MIT). Mutable Instruments engines (Plaits, Braids, Rings)
for the FM-1, with good hardware/recovery documentation and a verified
`tools/fm1_identify.py`. Its own README is candid:

> **Preview: not yet installable on an FM-1.** Lunar's application still needs
> to be linked and tested on the chip.

So: valuable as architecture and as MIT-licensed reference, but **no part of
its synthesis has run on real hardware.** Do not treat a Lunar engine as proven
to fit.

**hortator** (GPL-3.0-only). A drum firmware on Felucca with **integer ports of
Plaits' drum algorithms** that do run on the device — though the README says
"Beta, untested on most hardware". This is the better evidence that Mutable
algorithms can be made to fit, and the better reference for *how* to convert
them to fixed point. It is not evidence that every Plaits oscillator will fit:
the drum models are the cheap ones.

---

## F. awesome-fm-1 — the index

CC0, three files, a curated list of firmwares, editors, flashing utilities, CPU
research and guides. Use it to find things; it contributed no code here.

---

## Recommended starting point

**Dinghy + fm1-emu + `dsp/`**, which is what this bootstrap validated:

1. **`dsp/`** — synthesis mathematics, no hardware, tests in milliseconds.
2. **Dinghy host mode** — the same C as the firmware, on the host, as WAV.
3. **fm1-emu** — the full device, headless, scripted, with audio and screens.

Each layer is independently testable and each catches a different class of bug.
Hardware, when it arrives, becomes a fourth layer that confirms rather than
discovers.
