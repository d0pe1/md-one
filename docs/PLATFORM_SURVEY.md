# Which firmware should we actually build on?

Survey of 2026-10-16. Dinghy was a suggestion carried into this project from
outside, so it was checked against the field rather than taken on trust.

**Conclusion: keep Dinghy as the platform, but not for the reason originally
given — and take obervave's parameter/CC architecture rather than inventing
our own.**

## There is only one FM-1 platform

Ten repositories compared: Dinghy, Felucca and eight shipped firmwares
(sloop-fm1, obervave, zp12, fm1-x0x, fm1-amb, fm1-ctl, tonitor, ETCH).

* **9 of 10 use the identical layout** — `firmware/hal/`, `firmware/src/`,
  `build.sh`. Only ETCH diverges (`hal/` at the root).
* **All 17 of Dinghy's HAL files exist in all 10 repos.** None is absent
  anywhere.
* `libc.c` is byte-identical across every firmware checked.

Lineage as declared in the emulator's catalogue — of 49 firmwares, 20 state a
base:

| Base | Firmwares built on it |
| --- | --- |
| **Felucca** | 10 |
| **SLOOP** (itself on Felucca) | 7 |
| X0X (itself on Felucca) | 2 |
| Dexed (engine, not platform) | 2 |
| **Dinghy** | **0** |

So the honest picture is: everyone is on **Felucca's platform**, reached
directly or through SLOOP. Dinghy is not a competing base — it is that same
platform with the application deleted.

## Is Dinghy's snapshot stale?

No. For each of the 17 HAL files, the most common version across the ten
repositories was compared against Dinghy's:

**Dinghy matches the majority version on all 17 files.**

Worth noting how much the HAL is patched in practice — the majority is often
thin:

| File | Repos agreeing on the most common version |
| --- | --- |
| `fm1_gpio.h`, `fm1_vec.S`, `fm1_xip.h` | 10/10 |
| `fm1_adc.h`, `fm1_audio.h`, `fm1_cc.h` | 9/10 |
| `fm1_isr.S`, `fm1_time.h`, `fm1_timer.h` | 8/10 |
| `fm1_flash.h`, `fm1_sys.h`, `fm1_uart.h`, `fm1_usb.h` | 5/10 |
| `fm1_guard.h`, `fm1_lcd_hw.h` | 4/10 |
| `fm1_input.h` | 3/10 |
| `fm1_irq.h` | **2/10** |

The HAL is a starting point everyone modifies, not a frozen library. Expect to
patch it, and expect upstream divergence.

## What Dinghy uniquely has, and what it lacks

`platform.c` is **absent from SLOOP, fm1-amb, tonitor and zp12**, and differs
in obervave. It is zvenson's own consolidation of the plumbing — the one part
of Dinghy that is genuinely new code, three weeks old, with **zero shipped
firmwares using it**.

That is the real risk, and it is mitigated: our build reproduces Dinghy 0.3
byte-for-byte and runs correctly in the emulator (`BUILD_AND_TEST.md`). If
`platform.c` ever proves wrong, obervave and zp12 — same author, same HAL,
shipped and tested on hardware — are the fallback.

**Dinghy has no MIDI CC parsing.** `platform.c` gives raw
`midi_get(&status, &d1, &d2)`, and `dinghy.c` handles only `0x90`/`0x80`.
Felucca, SLOOP, obervave, fm1-x0x, fm1-amb, fm1-ctl and ETCH all parse CC;
Dinghy, zp12 and tonitor do not. For us that is ~5 lines
(`(st & 0xF0) == 0xB0`) on top of the right parameter model — see below.

## USB audio is not universal — keep it on

Every firmware carries the UAC code, but not all enable it:

| Firmware | `FELUCCA_UAC` |
| --- | --- |
| dinghy, Felucca, sloop-fm1, obervave, fm1-x0x, fm1-amb, ETCH | **1 (on)** |
| zp12, tonitor | 0 |
| fm1-ctl | 0 — deliberate, *"no audio: no USB audio input"* |

Since our entire output path is USB audio into Ableton, this is a hard
requirement, and Dinghy already satisfies it. Note that Dinghy cannot have it
turned *off* without patching: `platform.c` calls `uac_service()` and friends
unconditionally while `usb.c` defines them only under `#if FELUCCA_UAC`, so
`FELUCCA_UAC 0` fails to link. obervave handles the switch cleanly
(`-DFELUCCA_UAC=0: MIDI only, as zp12`).

## The candidates, ranked for our use

Our needs: USB audio on, deep MIDI CC, voice-shaped (not step-sequencer
shaped), minimum application code to delete, room for ~97 KB of Braids.

| Base | C/H files | UAC | CC | Shape | Verdict |
| --- | --- | --- | --- | --- | --- |
| **dinghy** | **29** | on | no | blank slate | **platform of choice** — least to delete |
| tonitor | 41 | off | no | ambient, granular | closest aesthetic; UAC off; little to reuse |
| **obervave** | 48 | on | **yes** | 8-voice polysynth | **CC architecture of choice**; proven |
| fm1-amb | 49 | on | yes | generative ambient | good aesthetic reference |
| zp12 | 55 | off | no | sampler | not relevant |
| fm1-x0x | 91 | on | yes | groovebox | sequencer baggage |
| fm1-ctl | 141 | **off** | yes | MIDI controller | best MIDI reference, no audio |
| sloop-fm1 | 124 | on | yes | groovebox | heavy |
| Felucca | 162 | on | yes | full synth | the origin; heaviest |
| ETCH | 233 | on | yes | waveform drawing | own layout |

## Take obervave's parameter table

obervave solves exactly the problem we described as "make every parameter CC
addressable and it becomes a modulation monster". `obv_params.h`, 170 lines:

```c
typedef struct {
    const char *name;             /* 8 characters at most (the knob label) */
    uint8_t max, def, kind, cc;   /* cc: a fixed MIDI CC, 0 = taken from 20 up in table order */
} pdef_t;
static const pdef_t PDEF[NPARAM] = {
    [P_CUTOFF] = {"CUTOFF", 127, 80, K_CUT, 74},
    [P_O1_PW]  = {"PW",     127, 64, K_PCT,  0},
    ...
};
static uint8_t cc_map[128];       /* MIDI CC -> parameter + 1, 0 = none */
```

One table drives the screen label, the range, the default, the display
formatting (`kind`) **and** the MIDI CC number — auto-assigned from CC 20
upward, with standard CCs pinned where they exist (74 for cutoff). ~66
parameters, 80-byte patches, 32 programs.

This is the right model for our spec, it is by the same author as Dinghy so it
drops onto the same platform, and it is GPL-3.0 — which we are anyway.

## Verdict

* **Platform: Dinghy.** Not because the ecosystem uses it (it does not), but
  because it is the minimal cut of the platform the ecosystem *does* use, its
  HAL is the majority version on every file, and USB audio is on.
* **Parameter and CC layer: modelled on obervave's `pdef_t`.**
* **Aesthetic references: tonitor and fm1-amb**, for how an ambient instrument
  is shaped on this hardware.
* **Fallback if `platform.c` disappoints: obervave or zp12.**

The original recommendation turns out to be right, but the stated reason was
wrong. It is worth knowing the difference: we are not following the crowd onto
Dinghy, we are taking the crowd's platform without the crowd's applications.
