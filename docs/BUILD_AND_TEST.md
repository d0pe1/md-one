# Build and test

Every command here was run on 2026-10-16 and its real output is shown. Where
something failed, the failure is recorded rather than tidied away.

Three independent layers, fastest first:

| Layer | What it proves | Round trip |
| --- | --- | --- |
| 1. `dsp/` harness | the mathematics | < 1 s |
| 2. Dinghy host mode | the firmware's C, natively | ~2 s |
| 3. `fm1-emu` | the whole device, headless | ~4 s |

---

## Layer 1 — the DSP harness

Integer DSP in `dsp/shared/` compiles for both host and target. Host-only
floating-point reference code lives in `dsp/host/`.

## The gate: `scripts/check-all.sh`

One command builds everything and then **runs** it. Use it before any commit;
the `make` targets below are for working inside `dsp/`.

```sh
wsl -d archlinux -- bash scripts/check-all.sh
```

```
== dsp tests        make test (oscillator, comb, mix bus)
== target rules     no float, no 64-bit divide, pi32v2 clean
== host tools       render_drone, render_braids
== target probes    probe_drone, probe_fx, probe_braids
== firmware         compile, link, brick checks, package
== emulator         boots, runs 3 s
                    250368 samples, peak 10557, rms 3228,
                    98.6% non-zero, 0 pinned

10 passed, nothing failed
```

**Why it exists.** `make test` covers `dsp/` only. Three consumers of the
parameter table — `dsp/target/probe_drone.cc`, `dsp/host/render_drone.cc` and
`firmware/src/drone.c` — sat broken for days after `P_DRIFT_DEPTH` was split
into `P_FM`/`P_MOD`, because nothing built them. Two build scripts were
separately compiling the engine as C++98 while the firmware shipped it as
C++11.

**The last stage is the one that cannot be faked.** Everything above it proves
the code compiles; only the emulator proves the device boots and makes a
sound. The audio assertions live in `scripts/check-audio.py`, which carries
its own negative controls:

```sh
python3 scripts/check-audio.py --selftest
```

```
  ok    healthy drone    expected accepted -> accepted
  ok    dead silence     expected rejected -> rejected
  ok    near-silence     expected rejected -> rejected
  ok    hard clipped     expected rejected -> rejected
  ok    stopped early    expected rejected -> rejected
```

A check that has never failed has not been shown to work.

> **`scripts/run-emu.ps1` is stale.** It passes `--fwsc/--ms/--wav/--keys/--snap`
> to `fm1-emu`, which now accepts only `--bin/--flash/--entry/--steps`. The
> device runner is `fm1-live`, and it writes audio through the `FM1_BENCH_WAV`
> *environment variable* — raw interleaved int16, no WAV header — not a flag.
> Crossing from WSL to the Windows build additionally needs
> `WSLENV=FM1_BENCH_WAV`, or the emulator runs, renders, and writes nothing,
> which looks exactly like a silent firmware.

## Working inside `dsp/`

```sh
wsl -d archlinux
cd /mnt/c/temp/GitHub/fm1-laboratory/dsp
make            # render, bench, tests
make test
make bench
make target     # compile shared/ for pi32v2 and enforce the chip's rules
make demo       # render WAVs
```

### `make test` — passed

```
tuning (integer phase increment vs the ideal ratio)
  [ok] phase increment is in tune -- worst -0.000865 cents at 20 Hz
accuracy (Q15 table + interpolation vs a double sine)
  [ok] fixed point tracks the float reference -- 84.5 dB (floor 70 dB)
determinism (same parameters -> identical samples)
  [ok] 16 seeded frequencies reproduce exactly -- seed 20261016
block path (the ISR uses blocks; it must equal the sample path)
  [ok] ragged blocks match sample-at-a-time -- 1024 samples @ 997 Hz
phase wrap (32-bit accumulator rollover)
  [ok] no discontinuity across the wrap -- largest step 2053

all tests passed (0 failures)
```

The tuning result is the one worth keeping: computing phase increments with a
64-bit **multiply** instead of a banned 64-bit **divide** costs under
**0.001 cent**, about a thousandth of what anyone can hear.

### `make bench` — ran

```
block 256 frames @ 44100 Hz -> 5.80 ms of audio per block
host is not the target: these are relative figures only

  voices   us/block   % of budget   blocks/s
       1       0.22         0.00    4577568
      16       3.40         0.06     294511

static footprint: osc 8 B/voice, block buffer 512 B
```

**Do not read these as device numbers.** A desktop x86 core is far faster than
a 240 MHz pi32v2. The value is the trend as the engine grows, and 8 bytes per
voice is a real figure.

### `make target` — passed

```
pi32v2: clang version 4.0.1 () ()
fm1_osc.c                ok  2740 B
target check: dsp/shared/ is clean for pi32v2
```

This catches a `float` or a 64-bit divide the moment it is written, naming the
file — instead of at firmware link time as an undefined libgcc symbol.

### `make demo` — spectrally verified

```
build/sine-220.wav       88200 frames @ 44100 Hz, carrier 220 Hz
build/fm-220x110.wav     88200 frames @ 44100 Hz, carrier 220 Hz (phase-modulated)
```

Analysed with `scripts/wavstat.py`:

| File | Strongest partials | Reading |
| --- | --- | --- |
| `sine-220.wav` | 220.6 Hz at −14 dBFS, next 61 dB down | clean sine |
| `fm-220x110.wav` | 439.9, 329.1, 109.5, 770.2, 548.1 Hz | textbook FM |

Every FM partial is carrier ± k·110 Hz — 110, 330, 440, 550, 770. The
phase-modulation primitive is correct.

> `wavstat.py` uses a 600-bin log-spaced Goertzel grid, so its frequency
> readings are approximate (220.6 rather than 220.0). It is a smoke test. The
> precise tuning claim comes from `make test`, not from here.

---

## Layer 2 — Dinghy host mode

The same `dinghy.c` the firmware runs, compiled with gcc:

```sh
wsl -d archlinux
export PATH=$HOME/.jieli/venv/bin:$PATH     # run_host.sh calls python3 and needs Pillow
cd ~/fm1lab/dinghy
tests/run_host.sh
```

Real output:

```
   200 ms  MIDI out 90 60 100
  1000 ms  screen -> build/host/dinghy-1000.ppm
  4501 ms  settings saved (1 B)
sound  -> build/host/dinghy.wav (286464 frames)
screen -> build/host/dinghy-1000.png
```

The MIDI trace, the settings write and the screen renders all work. This is the
fastest way to try a synthesis idea in the firmware's own code.

> `run_host.sh` invokes `python3` directly and needs Pillow. Arch's system
> python3 has none, hence the `PATH` line putting `~/.jieli/venv` first.

---

## Layer 3 — the emulator

### Build (Windows, native)

```powershell
cd references\fm1-emu
cargo build --release -p fm1-emu
```

`Finished release profile in 14.35s`, 3 warnings, binary 2.33 MB. The
`fm1-emu` crate depends only on the other workspace crates, so no C toolchain
or system libraries are involved.

### Get firmware to run

```powershell
.\scripts\fetch-firmware.ps1 -List
.\scripts\fetch-firmware.ps1 dinghy felucca x0x
```

Downloads each author's released `.fwsc` and writes a `.json` beside it with
the SHA-256, size and source URL.

### Run it

```powershell
.\scripts\run-emu.ps1 -Firmware dinghy -Ms 3000 `
                      -Keys "800-2200:n7,1200-2400:n11" -Snap 300,1500
```

Panel script forms: `<ms>:PLAY`, `<from>-<to>:n<note>`, `<ms>:enc0:+3`.

Result — two note keys four semitones apart:

```
  124928 frames @ 44100 Hz = 2.83 s
  peak 6937 (21.2% FS)   rms 2364.8 (-22.8 dBFS)
  partials:
       261.5 Hz    -31.7 dBFS
       329.1 Hz    -33.0 dBFS
```

261.5 Hz and 329.1 Hz are C4 and E4 — a major third, exactly the 4-semitone key
spacing requested. The screen snapshot independently corroborates it:

![Dinghy on the emulated panel](media/emu-dinghy-screen.png)

The panel shows `DINGHY / a sine to build on v0.3`, the note name `E4`, two
highlighted keys and `OCTAVE +0` — matching the audio exactly.

That is the full path — package → pi32v2 CPU emulation → audio ISR → PCM, plus
the SPI display — confirmed by two independent signals.

---

## The firmware build

```sh
wsl -d archlinux
export JIELI_TOOLCHAIN=$HOME/.jieli/toolchain
export AC79_SDK=$HOME/fw-AC79_AIoT_SDK
export PYTHON=$HOME/.jieli/venv/bin/python
cd ~/fm1lab/dinghy
./build.sh --release 0.3
```

Output:

```
ok    .ram_text: 901 insns, no calls
ok    image 55076 B of 581564; RAM .data+.bss 12368 B of 98304; pool 30720 B of 344064
ok    register access: hal/ only (src/, loader/ clean)
app      build/dinghy.bin  55076 B
loader   build/loader/ota.bin  6954 B
package  build/dinghy-0.3.fwsc  610110 B, identity FM-1_993
```

### The reproducibility result

Our build of Dinghy 0.3 is **byte-for-byte identical to the author's published
release**:

```
4e907044a7bda340317fc910d33a0e006eb4d587e08d8980de74d7edcca3ca8e  build/dinghy-0.3.fwsc
4e907044a7bda340317fc910d33a0e006eb4d587e08d8980de74d7edcca3ca8e  artifacts/firmwares/dinghy-0.3.fwsc
```

This is the strongest validation available without hardware. It means our
compiler, SDK revision, build flags and packaging all match the author's
exactly — and it independently confirms the toolchain even though the download
URL is unpinned (`DEVELOPMENT_ENVIRONMENT.md`).

Two notes:

* The version matters. Without `--release 0.3` the build stamps `dev`, giving
  package identity `FM-1_990` and a different hash. Both are internally
  deterministic; only the versioned one matches the release.
* Builds on `/mnt/c` and on the WSL filesystem produced identical hashes, so
  the filesystem choice is a performance question only.

### Running our own build in the emulator

```powershell
copy \\wsl$\archlinux\root\fm1lab\dinghy\build\dinghy-0.3.fwsc artifacts\builds\dinghy-local.fwsc
.\scripts\run-emu.ps1 -Firmware artifacts\builds\dinghy-local.fwsc -Name dinghy-local `
                      -Ms 3000 -Keys "800-2200:n0,1200-2400:n7"
```

```
  partials:
       261.5 Hz    -31.7 dBFS
       175.3 Hz    -33.9 dBFS
```

175.3 Hz and 261.5 Hz — F3 and C4, a perfect fifth, matching the 7-semitone
spacing. **The emulator loads locally built firmware.** The loop is closed.

---

## Failures encountered, and what fixed them

Recorded because they will recur on a fresh machine.

| Failure | Cause | Fix |
| --- | --- | --- |
| `./build.sh: /bin/sh^M: bad interpreter` | Git for Windows `core.autocrlf=true` (system-level) put CRLF in shell scripts | `scripts/fix-line-endings.ps1 -Force` |
| `fetch_firmwares.mjs` → `C:\C:\...` | POSIX `URL.pathname` assumption | `scripts/fetch-firmware.ps1` |
| `--snap out.ppm` → usage error, exit 2 | `--snap` takes times in ms, not a filename | `scripts/run-emu.ps1` |
| Snapshots silently missing | written to a hardcoded relative `work/`, error discarded | runner creates it and collects |
| `M_PI undeclared` | `-std=c99` hides it | local `FM1_PI` constant |
| `clock_gettime` undeclared | POSIX, hidden under `-std=c99` | `#define _POSIX_C_SOURCE 199309L` |
| First toolchain check passed wrongly | grepped `clang --version` for `pi32v2`, which matched the **InstalledDir path** — a stock clang would pass too | check `objdump` reports `ELF32-pi32v2` instead |
| `--quiet` produced **no audio**, exit 0 | the WAV write is nested inside the end-of-run report block that `--quiet` skips | `-Quiet` removed from `run-emu.ps1`; stale output deleted and the WAV's existence asserted after each run |

### Two that reported success while being wrong

Both are worth remembering because neither announced itself.

**The toolchain check.** The first `verify-toolchain.sh` grepped
`clang --version` for the string `pi32v2`. It passed — but `--version` prints
`InstalledDir: /root/.jieli/toolchain/pi32v2/bin`, so the test matched a path
and would have passed on a stock clang too. It now compiles a program and asks
the vendor `objdump` what architecture came out.

**The emulator `--quiet` flag.** A re-run with `--quiet` printed nothing,
exited 0, and `run-emu.ps1` reported `audio -> …` because the file existed —
from the *previous* run. The analysis then described 2.83 s of two-note audio
for what was supposed to be a 1.5 s single-note run. Upstream nests the WAV
write inside the report block `--quiet` skips:

```rust
if !args.quiet {
    ...
    if let Some(w) = &args.wav { match write_wav(w, &soc.periph.audio) { ... } }
}
```

The runner now deletes stale output first and fails loudly if no WAV appears.

That last one is worth dwelling on: the first version of
`scripts/verify-toolchain.sh` reported success for the wrong reason. It now
tests functionally — compile something, ask the vendor `objdump` what came out,
and cross-check `e_machine = 0xf1` via `file(1)`.
