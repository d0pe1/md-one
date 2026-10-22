# Licence register

Assessed 2026-10-16, before any third-party code was integrated. Licences were
read from each clone's own `LICENSE` file at the pinned SHA.

**This is an engineering assessment, not legal advice.**

## The register

| Project | SHA | Declared licence | What we might reuse | Obligation if we do |
| --- | --- | --- | --- | --- |
| [dinghy](https://github.com/zvenson/dinghy) | `d44df7d` | **GPL-3.0-only** | the whole platform; our firmware starts as a fork | **Our firmware becomes GPL-3.0-only. Complete source to every recipient.** |
| [Felucca](https://github.com/hugelton/Felucca) | `a53ecf4` | **GPL-3.0** | `hal/` drivers, reached via Dinghy | same; already inherited through Dinghy |
| SLOOP (isod89) | — | GPL-3.0 | platform changes, reached via Dinghy | same; **not cloned** — only present inside Dinghy |
| [fm1-emu](https://github.com/joakimeriksson/fm1-emu) | `7196a9f` | **MIT** | a build/test tool; we ship none of it | attribution if redistributed |
| [FM-1-RE](https://github.com/AL-255/FM-1-RE) | `ec832f2` | **WTFPL** | documentation and facts only | none practically |
| [lunar-modulator](https://github.com/ip2k/lunar-modulator) | `bfa0424` | **MIT** | DSP engines, `fm1_identify.py` | retain copyright + MIT text |
| [hortator](https://github.com/DeadActive/hortator) | `7e34159` | **GPL-3.0-only** | integer Plaits drum ports | GPL-3.0-only (we are there anyway) |
| [awesome-fm-1](https://github.com/cicloid/awesome-fm-1) | `b310034` | **CC0-1.0** | an index | none |
| [JIANT FM](https://github.com/juanjiant-bit/JIANT) | `dc8d025` | **GPL-3.0-only** | **ideas only** — cloned to read its control surface | GPL-3.0-only if code is ever taken. **Nothing from it may reach `dsp/`.** |
| [AudibleInstruments](https://github.com/VCVRack/AudibleInstruments) `src/` | `a1cd335` | **GPL-3.0-or-later** (Andrew Belt) | **nothing** — VCV Rack wrappers only, no DSP | avoid; see the trap below |
| AudibleInstruments `eurorack/` submodule | `9739c02` | **MIT** (Emilie Gillet) | Braids DSP — this is where the algorithms live | retain the per-file MIT header verbatim |
| AudibleInstruments `res/` | `a1cd335` | **© Emilie Gillet, "used with permission"** | **nothing** | **not an open licence — do not reuse the panel graphics** |
| JieLi AC79 AIoT SDK | `d179b44` | **Apache-2.0** | `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` | notice + attribution; patent grant |
| JieLi pi32v2 toolchain | `20260730.1` | **proprietary, no licence file** | compiler only — output, not input | **do not redistribute the toolchain** |
| Terminus font (in Dinghy) | — | **SIL OFL 1.1** | the 8×16 bitmap font | OFL notice; reserved-name rule |
| designburg signet (Dinghy logo) | — | **all rights reserved** | nothing | **must be replaced in any fork** |

## The decision that follows

**Building on Dinghy makes our firmware GPL-3.0-only.** Not "should be" —
GPL-3.0-only is the licence of the platform we would be deriving from, and it
is `-only`, so we cannot even move to GPL-4 later.

Practically this means:

* anyone we give a built `.fwsc` to is entitled to its **complete
  corresponding source**, under GPL-3.0-only;
* that includes our synthesis engine, parameter system and UI, because they
  link against Dinghy's platform in one binary;
* we cannot later close the engine, dual-licence it, or ship a proprietary
  build, without a clean-room rewrite of everything derived from Felucca.

If the project is open source, this costs nothing and puts us in the same
position as every other FM-1 firmware. **If there is any intent to keep the
synthesis engine proprietary, that decision must be made now**, because the
alternative is writing the platform from scratch — the USB stack, the update
path, the rescue mode, the HAL — which is most of the work Dinghy exists to
save.

Assumed, as nothing suggests otherwise: **this is an open-source project and
GPL-3.0-only is acceptable.** Recorded here so it is a choice, not a default.

### Keeping our own DSP reusable

`dsp/shared/` and `dsp/host/` are **written from scratch by us** and carry
`SPDX-License-Identifier: MIT`. They include no Felucca, Dinghy or Mutable
code, and `dsp/` builds and tests entirely independently of any firmware.

MIT is GPL-3.0 compatible, so this code can be linked into our GPL-3.0-only
firmware *and* reused elsewhere — a desktop plugin, a different chip — without
the GPL following it. Keeping that boundary clean is worth some discipline:

> **Rule:** nothing copied or adapted from a GPL source may enter `dsp/`. If we
> port a Plaits algorithm from hortator, it goes in `firmware/`, not `dsp/`.

## Provenance discipline

Three distinct things, to be labelled distinctly in commit messages:

| Kind | Meaning | What to record |
| --- | --- | --- |
| **Inspiration** | we read it and wrote our own | no obligation; cite anyway |
| **Adapted** | our code, their structure or constants | licence applies; note the source file and SHA |
| **Copied** | their code, possibly reformatted | licence applies in full; keep their header verbatim |

Take source from the **original upstream** at a recorded SHA, not from a fork
that may have stripped notices. `references/README.md` holds the manifest.

## The AudibleInstruments trap — read before porting

AudibleInstruments looks like one project under one licence. It is two, and
taking code from the wrong half would relicense our engine.

```
AudibleInstruments/
  src/        GPL-3.0-or-later  (c) Andrew Belt   <- VCV Rack wrappers. NO DSP.
  res/        (c) Emilie Gillet, by permission    <- panel art. NOT open.
  eurorack/   MIT (c) Emilie Gillet               <- the actual algorithms.
```

`src/Braids.cpp` is 11 920 bytes of VCV parameter plumbing. Every sample of
audio comes from `eurorack/braids/`, which carries a per-file MIT header
verbatim in each source file:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software… — Copyright 2012 Emilie Gillet

The `eurorack/` submodule points at `VCVRack/pichenettes-eurorack` (a mirror).
**Take source from `pichenettes/eurorack` upstream**, record the SHA, and keep
Emilie Gillet's header intact.

One caveat inside `eurorack/` itself, from its own README:

> Code (AVR projects): GPL3.0. **Code (STM32F projects): MIT license.**

Braids, Plaits, Rings, Elements, Clouds, Tides, Warps, Marbles, Stages,
Streams, Frames and Peaks are STM32F projects and therefore MIT. The older AVR
modules are GPL-3.0. Check which family a module belongs to before porting it.

**Consequence for us:** Braids' DSP is MIT, so a fixed-point port of it may
live in `dsp/` and keep the engine reusable. Had we taken it from VCV's `src/`
or from hortator, it would have been GPL-3.0 and confined to `firmware/`.

## Unresolved questions

1. **Mutable Instruments provenance.** MI code is MIT under Emilie Gillet's
   copyright, but it can also be reached *via* lunar-modulator (MIT), hortator
   (GPL-3.0-only) or VCV's AudibleInstruments (GPL-3.0-or-later). Those are
   different obligations for the same algorithm. Always go to
   `pichenettes/eurorack` directly, keep the original header, and record which
   upstream file it came from.
2. **Hortator's integer ports are GPL-3.0-only**, while MI's originals are MIT.
   The fixed-point conversion is the valuable part and it is DeadActive's work,
   so taking it means GPL. Fine for `firmware/`, forbidden in `dsp/`.
3. **The JieLi toolchain has no licence file.** We treat it as
   use-but-never-redistribute. Its *output* is ours. This is the common
   community assumption and has not been verified against any JieLi terms.
4. **Firmware images in FM-1-RE** are M-VAVE's work regardless of the repo's
   WTFPL. Do not redistribute stock firmware images or a dump of our own
   device's flash.
5. **`.fwsc` packages in `artifacts/firmwares/`** are other authors' released
   binaries, fetched from their own sites and kept only as emulator test input.
   They are gitignored and must not be republished. Each has a `.json` beside
   it recording author, licence, source URL and SHA-256.

## The official M-VAVE SDK — assessed 2026-10-17

M-VAVE published an **FM1 Open** firmware track and a development kit:
[open.m-vave.com/fm1-workbench](https://open.m-vave.com/fm1-workbench) →
`FM1-Open-V13-Development-Kit.zip`
(sha256 `2a3b2ede…f68f57`, 1,682,912 B, matching the hash in its own URL).

A manufacturer openly supporting custom firmware on its own hardware is not
the usual outcome, and it changes several assumptions recorded above.

### What is in it

| Part | Licence | Relevance |
| --- | --- | --- |
| `app/`, `web/` application source | **GPL-3.0-only** + SDK linking exception | the same licence our fork already carries |
| `sdk/lib/libfm1_engine.a` | **proprietary**, FM1-devices-only grant | their FM engine. We do not want it |
| `sdk/lib/libcompiler_rt.a` | **compiler-rt** (Apache-2.0 + LLVM exception) | **soft-float and 64-bit division** |
| `sdk/lib/libc.a` | **newlib** (BSD-family) | a real libc for the target |
| `sdk/tools/pack/*`, `sdk/firmware/FM-1_015.fwsc` | **Apache-2.0** | `uboot.boot`, `ota.bin`, `cfg_tool.bin`, recovery image |
| `app/board/` | GPL-3.0-only | real HAL source: audio, panel, LCD, input |

Derived from the JieLi AC79 AIoT SDK at commit `5fa84a9b…`, Apache-2.0 — the
same upstream we already use.

### The finding that matters

**`libcompiler_rt.a` exports exactly the symbols `dsp/target/check.sh` bans:**

```
__adddf3 __subdf3 __muldf3 __divdf3      double
__addsf3 __subsf3 __mulsf3 __divsf3      float
__floatsidf __floatsisf __fixdfsi __fixsfsi __extendsfdf2 __truncdfsf2
__udivdi3 __divdi3 __umoddi3 __moddi3 __muldi3
```

So **"no float, no 64-bit division" is a property of our build, not of the
chip.** The AC791N still has no FPU, and a float still fails to *link* in our
tree today — that part was always true. But the runtime library that fixes it
exists, is permissively licensed, and is redistributable.

This is not an argument for rewriting `dsp/` in float. Soft-float costs tens
of cycles per operation against one or two for integer arithmetic, and the
budget is 1,360 cycles per sample across all four voices. The fixed-point
work stands. What the library actually buys:

* it removes a class of build fragility — a stray `0.5` currently fails the
  link with an unhelpful message about a missing symbol;
* it allows `exp()` and `sin()` at **setup** rate, which is exactly what
  `gen_svf_table.py` and `gen_env_table.py` exist to work around (generating
  tables offline remains better practice, but it stops being forced);
* it makes porting float DSP **possible to attempt**. See below.

### Macro Oscillator 2 (Plaits)

This is the interesting consequence. Braids ported because Braids is already
fixed point — it targets an STM32F1 with no FPU. **Plaits is float
throughout**, because it targets an STM32F4 that has one. That is why it was
previously not a candidate: it would not have linked at all.

With compiler-rt it links. Whether it *runs* is a separate question, and an
empirical one:

* budget is **1,360 cycles per sample** across four voices, or about 5,440
  for one voice alone;
* a soft-float multiply or add is roughly 30–100 cycles;
* so the ceiling is very roughly **50–150 float operations per sample** for a
  single voice, and a quarter of that at four.

Plaits' engines are not uniformly expensive, exactly as Braids' shapes were
not — our own per-shape table ranged 15,129 to 45,139 cycles. The right move
is the one that worked before: **measure first**, with the existing probe
harness, one engine at a time, before porting anything.

Licensing is clean: Plaits is an STM32F project, so MIT under the
`pichenettes/eurorack` rule already recorded above, and a port may live in
`dsp/`. Note that hortator's integer Plaits drum ports are **GPL-3.0-only**
and would be confined to `firmware/` — if a float port measures acceptably,
it is also the licensing-cleaner route.

### The other usable piece

The boot and packaging resources are **Apache-2.0**: `uboot.boot`,
`cfg_tool.bin`, `ota.bin`, `boot-config.bin`. `firmware/tools/build.py`
currently refuses to run without an external `AC79_SDK` checkout for exactly
these files. Taking them from here makes the build self-contained, and
Apache-2.0 is GPL-3.0 compatible.

### What this does NOT change

* **The brick risk.** Their recovery restores a device *stuck at the upgrade
  screen*, at the same firmware version. It is not a brick-fix, and the
  warning is explicit: *force flashing may erase data, cause boot failure,
  and require dedicated tools to recover*. `docs/FIRMWARE_SAFETY.md` stands,
  and the boxed second unit is still the right call.
* **Our licence.** GPL-3.0-only either way. `dsp/` stays MIT because it
  depends on none of this.
* **`libfm1_engine.a` stays out.** It is their FM engine, MD-One has its own,
  and its grant is restricted to FM1 devices — a restriction worth not
  propagating into something meant to be reusable.

### Should `firmware/` move from Dinghy to the official APP?

Not on this evidence. The arguments for are real — official, maintained,
ships its own boot resources, hash-verified. Against:

* the build is **Windows x64 + PowerShell** (`build.bat`, `Build.ps1`,
  `Package.cs`); our entire toolchain is WSL and bash;
* the APP is structured around `fm1_engine_render()`, the proprietary blob we
  do not want;
* our Dinghy fork builds, boots and passes the brick checks today.

The valuable parts — compiler-rt, the boot resources, the HAL as a reference
— are permissively licensed and can be taken **without** migrating the
platform. Borrow the libraries, keep the build.


## If we release

- [ ] `LICENSE` with the full GPL-3.0 text
- [ ] per-file `SPDX-License-Identifier` headers
- [ ] a `LICENSING.md` crediting Kuroshita/Hügelton (Felucca), isod89 (SLOOP)
      and zvenson (Dinghy), following Dinghy's own format
- [ ] the designburg logo **removed** and replaced with our own
- [ ] Terminus OFL notice retained
- [ ] our own package identity, so installers tell our firmware from Dinghy
- [ ] complete corresponding source published alongside every released `.fwsc`
