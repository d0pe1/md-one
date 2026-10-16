# artifacts/

Generated and downloaded files. **Everything here except this README is
gitignored**, for two different reasons.

```
artifacts/
  firmwares/   other authors' released .fwsc packages, as emulator test input
  builds/      our own firmware builds
  emu/         emulator output: WAV, screen BMP/PNG, per run
  dsp/         rendered audio from the DSP harness
```

## Why it is not committed

**`firmwares/` is other people's work.** Each `.fwsc` is an author's released
binary, downloaded from their own site by `scripts/fetch-firmware.ps1` purely
so the emulator has something real to run. They are not ours to redistribute.
Every package has a `.json` beside it recording author, licence, repo, source
URL, SHA-256 and fetch time.

**Future flash dumps are device-unique.** When hardware arrives, any dump
belongs here or outside the tree entirely — never in git, never in an issue or
a paste. It can contain chip keys, calibration data and unique identifiers.
See `docs/FIRMWARE_SAFETY.md`.

The two verification screenshots referenced by the docs were copied into
`docs/media/` precisely so the documentation survives without this directory.

## Reproducing the contents

```powershell
.\scripts\fetch-firmware.ps1 dinghy felucca x0x
.\scripts\run-emu.ps1 -Firmware dinghy -Ms 3000 -Keys "800-2200:n7" -Snap 1500
cd dsp; make demo
```

## What is here now

| Path | From |
| --- | --- |
| `firmwares/dinghy-0.3.fwsc` | thefirmwarehub.com, `4e907044…` |
| `firmwares/felucca-1.6.1.fwsc` | thefirmwarehub.com, `dff3a38f…` |
| `firmwares/x0x-1.1.0.fwsc` | thefirmwarehub.com, `a0c29deb…` |
| `builds/dinghy-local.fwsc` | our own build — **identical to the release above** |
| `emu/dinghy-0.3/` | WAV + screens, stock package |
| `emu/dinghy-local/` | WAV + screens, our build |
| `dsp/sine-220.wav`, `dsp/fm-220x110.wav` | the DSP harness |

Inspect any WAV with:

```powershell
python scripts\wavstat.py artifacts\emu\dinghy-0.3\dinghy-0.3.wav
```
