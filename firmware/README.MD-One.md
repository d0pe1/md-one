# MD-One — the firmware

Our fork of Dinghy, carrying the MD-One application. Before a line was changed,
the unmodified build was reproduced byte for byte:

```
build/dinghy-0.3.fwsc  sha256 4e907044…  == the published release
```

That reproduction is what makes every later difference attributable to us
rather than to the toolchain.

```
firmware/
  src/drone.c          <- MD-One: the part we write
  src/platform.c       <- Dinghy's plumbing: read it, don't fight it
  hal/                 <- Felucca's HAL: do not duplicate
  tools/build.py       <- our identity lives here (PRODUCT, APP_NAME)
```

**Package identity `FM-1_971`, app name `MD-One`.** Not Dinghy's — two
firmwares sharing an identity look like one firmware to the installers and the
hub. `docs/MAKE-IT-YOURS.md` covers the rest of the rename and releases.

## Before writing any of it, two things are already decided

**1. The licence.** Dinghy is **GPL-3.0-only**. Forking it makes our firmware
GPL-3.0-only, and everyone we hand a `.fwsc` to is entitled to the complete
corresponding source. `docs/LICENSE_REGISTER.md` explains why this is the one
decision that cannot be deferred.

**2. Where the DSP goes.** `dsp/shared/` is ours and MIT, with no GPL code in
it. The firmware links it; the boundary stays clean:

> Anything ported or adapted from a GPL source (Felucca, hortator, SLOOP)
> belongs in `firmware/`, never in `dsp/`.

That is what keeps the synthesis engine reusable outside this device.

## The constraints that will shape the code

From `references/dinghy/docs/RULES.md`, summarised in
`docs/HARDWARE_ARCHITECTURE.md`:

* no `float` or `double` — the **link** fails, there is no FPU and no libgcc;
* no 64-bit division or modulo; 64-bit multiplication is fine;
* no constant in `0xFFC00000`–`0xFFCFFFFF` (chip ROM), even accidentally;
* hardware registers only inside `hal/`;
* `app_audio()` gets 256 frames and 5.8 ms, interrupted by USB and the key
  scan — aim under half.

`./build.sh` enforces the first four and fails with the reason. `dsp/target/check.sh`
enforces the first two against our DSP earlier, where the error names the file.

## Current budget

Measured from the MD-One build, not from Dinghy's:

| | Used | Available | Still free |
| --- | --- | --- | --- |
| Flash image | 138 296 B | 581 564 B | **~433 KB** |
| RAM `.data+.bss` | 13 264 B | 98 304 B | **~83 KiB** |
| RAM `.pool` | 118 572 B | 344 064 B | **~220 KiB** |

The pool is where the four `BraidsVoice` objects live, at 17 152 B each, plus
a 4 KiB comb delay line per voice.

**CPU is the budget that actually binds, not memory.** Four voices cost 225 083
cycles per 256-frame block — 16.2% of the 1 393 152 available. `docs/SIGNAL_PATH.md`
§8 has the per-block measurements.

The device has **512 KiB** of data RAM; those two regions are the parts an
application may use. `.pool` is the one for big buffers — delay lines,
wavetables, canvases — and it is nearly four times the size of `.bss`, so do
not treat the 98 304 figure as the memory budget. Full map in
`docs/HARDWARE_ARCHITECTURE.md`.

## Testing it without hardware

Three layers, all verified and documented in `docs/BUILD_AND_TEST.md`:

1. `cd dsp && make test` — the mathematics, in under a second
2. `tests/run_host.sh` — the same app C compiled natively, rendered to WAV
3. `scripts/run-emu.ps1` — the whole device, headless, with audio and screens

We own no FM-1 yet, and **no firmware may be flashed** until the gate in
`docs/FIRMWARE_SAFETY.md` is satisfied.
