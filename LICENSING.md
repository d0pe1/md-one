# Licensing

MD-1 is **GPL-3.0-only**, because it builds on Dinghy, which is GPL-3.0-only.
That is not a preference — it is the licence of the platform this firmware
derives from, and it is `-only`, so this project cannot move to a later GPL.

Anyone given a built `.fwsc` is entitled to its complete corresponding source
under GPL-3.0-only. That includes the synthesis engine, the parameter system
and the UI, because they link against Dinghy's platform in one binary.

`docs/LICENSE_REGISTER.md` holds the full assessment, per project, with the SHA
each licence was read at. **It is an engineering assessment, not legal advice.**

## The one exception: `dsp/`

`dsp/shared/`, `dsp/host/` and `dsp/engine/` are written from scratch and carry
`SPDX-License-Identifier: MIT`. They contain no Dinghy, Felucca or SLOOP code,
and `dsp/` builds and tests with no firmware present at all.

MIT is GPL-3.0 compatible, so this code links into the GPL-3.0-only firmware
*and* stays reusable elsewhere — a desktop plugin, a different chip — without
the GPL following it.

> **Rule:** nothing copied or adapted from a GPL source may enter `dsp/`.
> A Plaits algorithm ported from hortator (GPL-3.0-only) goes in `firmware/`,
> never in `dsp/`.

`dsp/mi/` is vendored Mutable Instruments source and is **not** ours — see
below.

## Credit where this comes from

| Project | Author | Licence | What MD-1 uses |
| --- | --- | --- | --- |
| [Dinghy](https://github.com/zvenson/dinghy) | zvenson | GPL-3.0-only | the platform; `firmware/` began as a fork at `d44df7d` |
| [Felucca](https://github.com/hugelton/Felucca) | Kuroshita / Hügelton | GPL-3.0 | the HAL and drivers, reached through Dinghy |
| SLOOP | isod89 | GPL-3.0 | platform work, reached through Dinghy |
| [eurorack](https://github.com/pichenettes/eurorack) | Emilie Gillet | MIT | Braids DSP — every sample of oscillator audio |
| [fm1-emu](https://github.com/joakimeriksson/fm1-emu) | Joakim Eriksson | MIT | the emulator every number in `docs/` was measured on |
| Terminus font | Dimitar Zhekov | SIL OFL 1.1 | the 8×16 bitmap font, via Dinghy |

Mutable Instruments source keeps Emilie Gillet's MIT header verbatim in every
file. Braids is an STM32F project and therefore MIT; the older AVR modules in
the same repository are GPL-3.0. Check which family a module belongs to before
porting it.

## Deliberately not here

- **Dinghy's logo** (`assets/logo.svg`, the designburg signet) is *all rights
  reserved*, not GPL. It has been removed from this fork, as Dinghy's own
  `MAKE-IT-YOURS.md` asks. MD-1 needs its own.
- **AudibleInstruments panel graphics** are © Emilie Gillet, "used with
  permission" — not an open licence. Not reused.
- **The JieLi pi32v2 toolchain** ships with no licence file. Used, never
  redistributed. Its output is ours.
- **Stock M-VAVE firmware images**, and any dump of a real device's flash, are
  M-VAVE's work and may carry device-unique material. Never committed, never
  published. See `docs/FIRMWARE_SAFETY.md`.
- **`references/`** is 851 MB of other people's repositories, reproduced by
  `scripts/clone-references.ps1` at the SHAs in `docs/REPOSITORY_MAP.md`. Never
  vendored here.

## Provenance discipline

Three distinct things, labelled distinctly in commit messages:

| Kind | Meaning | What to record |
| --- | --- | --- |
| **Inspiration** | read it, wrote our own | no obligation; cite anyway |
| **Adapted** | our code, their structure or constants | licence applies; note source file and SHA |
| **Copied** | their code, possibly reformatted | licence applies in full; keep their header verbatim |

Take source from the **original upstream** at a recorded SHA, never from a fork
that may have stripped notices.
