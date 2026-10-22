# Firmware safety and recovery

## Status

**MD-One runs on hardware.** Flashed 2026-10-22 to the development unit,
identity `FM-1_971` confirmed by the installer after restart. It boots,
sounds, delivers audio to a DAW over USB, and reports 42% CPU on its own
HOME page.

The recovery loop has been walked end to end on that device — see
"Recovery, verified by us" below. A build that boots but misbehaves is
provably recoverable.

This section previously read *"No firmware was written to any device. No
device was connected."* That was true for the bootstrap and false the moment
hardware arrived. It is recorded here rather than quietly deleted because a
safety document whose opening line is stale is worse than one that admits
what changed.

## Device policy

| Unit | Role | Policy | State |
| --- | --- | --- | --- |
| **A** | development — "the beater" | the only unit that receives our firmware | **running MD-One** |
| **B** | reference | stays stock | not yet unboxed |

Unit A was running M-VAVE's `FM-1_015` before the first flash, which is the
same identity as the image preserved at
`artifacts/firmwares/mvave-stock-015.fwsc` — so the rollback target is not
merely *a* stock image but the one this device shipped with.

### The unresolved tension in "keep one boxed"

A sealed unit is a perfect *replacement* but a useless *reference*. To compare
our output against stock behaviour, or to hold a known-good stock image, unit B
must be powered on and connected at least once.

There is a real decision here, and it should be made deliberately rather than
by default:

* **Keep B sealed.** Maximum insurance: an untouched unit to fall back on. But
  you have no stock flash dump of your own, no reference recordings, and no
  second device for A/B listening.
* **Unbox B once, read-only.** Dump its stock flash, record reference audio
  from the stock engine, then shelve it. It stays stock — reading is not
  writing — and you gain the baseline that makes "does ours sound right?"
  answerable.

The second is the better engineering choice *provided* the read path is
genuinely read-only, which is exactly what has not been established yet (see
below). **Recommendation: leave B sealed until a read-only dump has been
performed successfully on unit A.** Practise on the unit you can afford to lose.

## Why this device deserves caution

| Fact | Consequence |
| --- | --- |
| **Single flash bank**, 1 MB | no second copy to boot from |
| **No recovery button, no test pads, no debug header** | no out-of-band rescue |
| Updates authenticated by **CRC-16 only**, no signature | a malformed package is not rejected for being wrong, only for being corrupt |
| Version gate lives in the **host updater**, not the device | the device will accept an older or hand-rolled package |

The absence of a signature check is what makes custom firmware possible at all,
and it is also what removes the safety net.

### The SysEx that must never be sent by hand

From `references/fm1-emu/docs/recon.md` and FM-1-RE:

```
F0 22 24 35 7D F7     <-- drops the unit into mask-ROM download mode
F0 22 24 35 7F F7     <-- the normal firmware-upgrade command
```

**One byte apart.** `7D` versus `7F`. Never type these by hand; use the
installer tooling, which sends the correct one.

### The flash head

The OTA loader can rewrite `uboot.boot` and `isd_config.ini`. The community
rule is to keep those **byte-identical to stock**. Dinghy's build already reads
them from the AC79 SDK rather than generating them, which is part of why
building on Dinghy is safer than rolling our own packager.

## Telling the modes apart over USB

Observed on a stock unit, 2026-10-20 — the first hardware this project has
seen:

| Mode | USB ID | Enumerates as |
| --- | --- | --- |
| **Normal** | `4C4A:C755` | `FM-1 Audio` + `FM-1 Midi`, class-compliant, driver `usbaudio` |
| Mask-ROM recovery | `4C4A:8057` | `WL80UBOOT` |

Same vendor — `4C4A` is `"LJ"`, JieLi — different product. Only the recovery
ID was written down before, which left no way to tell from these documents
whether an attached device was running normally or already in trouble.

A normal-mode unit presents **both** a capture endpoint and a render
endpoint. The capture one, `Microphone (FM-1 Audio)` on Windows, is the
entire delivery path for this instrument: it is how the engine's samples
reach a DAW without a D/A → A/D round trip. Seeing it confirms the
assumption the whole design rests on, on real hardware, with nothing
flashed.

## The stock image is in hand

M-VAVE ship their own firmware inside their open SDK
(<https://github.com/m-vave/fm1_open>), at
`x/sdk/firmware/FM-1_015.fwsc` — 699,956 bytes, V15.

It is copied to `artifacts/firmwares/mvave-stock-015.fwsc`, verified
identical. That directory is gitignored: the image is M-VAVE's and is not
ours to redistribute. The copy exists because `references/` is *reproduced*
by `scripts/clone-references.ps1` rather than preserved — a re-clone fetches
it again from GitHub, which is exactly the dependency a recovery image must
not have.

**This materially changes the risk.** Every recovery path below is
documented-but-unproven, and upstream's own note is that restoring a whole
image "remains untested". A vendor-signed stock package plus M-VAVE's own
M-UPGRADE updater is a route the manufacturer supports and tests, which is
almost certainly stronger than our dump-and-restore. It does not make
flashing safe; it makes the most likely failure recoverable by the route
most likely to work.

What it does **not** cover: damage to the bootloader itself, which no
application-level update can repair. `firmware/tools/build.py`'s brick
checks exist for exactly that, and they stay.

The package identities differ and should: ours is `FM-1_971`, M-VAVE's is
`FM-1_015`. Two firmwares sharing an identity look like one firmware to the
updater and to the Firmware Hub.

## The "verified backup" precondition cannot be met, and that is settled

This document has required a verified stock backup before any write. That
requirement is **unsatisfiable**, and it took reading
[aroum/fm1-custom-fw](https://github.com/aroum/fm1-custom-fw)'s
`REVERSE_ENGINEERING.md` to establish why:

> No documented method exists to dump firmware *from* a connected FM-1 unit.

The flash is a Puya P25Q80H die **co-packaged inside the AC7911B SoC**, so
there is no external chip to clip onto either. What that repository calls
firmware extraction carves the image out of M-UPGRADE's own binary — it
reads the updater, not the device.

So the gate as written could never be passed, and a gate that cannot be
passed is not caution, it is a way of never deciding. Replaced with what is
actually achievable:

| Check | Status |
| --- | --- |
| A stock image exists locally to roll back TO | ✅ `artifacts/firmwares/mvave-stock-015.fwsc` |
| The firmware retains a bootloader/OTA entry | ✅ `platform.c:339` — OCT− at power-on enters `recovery_main()` |
| A crashing build falls back on its own | ✅ boot guard: crash → USB rescue → ROM (`platform.c:5`) |
| The bootloader is not rebuilt by us | ✅ `uboot.boot` pinned by SHA-256 to the tested SDK (`build.py`) |
| Restore protocol implemented and understood | ✅ device-pull OTA; `fm1_ota.py` in `references/FM-1-RE` |
| Mask-ROM recovery exists if all that fails | ⚠️ verified by others on hardware, not by us; needs an RP2040 dongle |

**The requirement that actually matters** is the one that repository states
plainly, and it is about our own build rather than about backups:

> Any custom firmware MUST retain or implement a mechanism to enter the
> bootloader/OTA mode. If custom firmware is flashed without a bootloader
> entry handler, the device will be **permanently soft-locked** against
> future MIDI updates or rollback.

Ours does, inherited from Dinghy and checked above. That is the single
condition separating "recoverable" from "lost", and it is worth more than
any backup — because a device that can still take an update can always be
rolled back, and one that cannot is gone whatever you saved.

Still true, and unchanged: **never overwrite the 16 KB region holding the
partition header and boot vector.** We do not build it; we ship the SDK's,
hash-checked.

## Recovery, verified by us on hardware — 2026-10-22

The whole loop was walked on a real device, each step observed:

| Step | Observed |
| --- | --- |
| Enter rescue from a **running** firmware | OCT− + OCT+ held 5 s; red countdown from 2 s; release cancels |
| The rescue is reachable over USB | enumerates, answers the installer, reports identity **`FM-1_000`** |
| Install a firmware **from** rescue | `fm1_install.py` wrote a full package and the device restarted |
| Return to normal | reports `FM-1_971`, audio running |

This is the first recovery claim in this document that is ours rather than
inherited. Everything below it is still someone else's testing.

**What is NOT verified, and the distinction matters:**

* **The boot-time OCT− trigger does not work on this hardware.** Three cold
  boots, button held before power, nothing else pressed — straight to the
  home screen every time. It is kept (hardened) as a second route, but it
  is not the one to rely on.
* **Entry from a firmware that will not boot** — the automatic path, two
  crashed boots setting `BOOT_RECOVERY`. Untested. It is also the path that
  matters most for a bad flash, and it is the one we cannot rehearse
  without deliberately building something broken.
* Mask-ROM USB, and anything needing the RP2040 dongle.

So the honest position: a build that **boots but misbehaves** is now
provably recoverable. A build that **does not boot at all** rests on a path
nobody here has exercised.

## Recovery paths — documented, unproven

Three layers are described upstream. Their validation status differs, and none
of it has been confirmed by us.

| Layer | Trigger | Status |
| --- | --- | --- |
| 1. USB rescue | **OCT− + OCT+ held 5 s while running** (Felucca's trigger, added by us); or automatic after two boot crashes | the held-while-running path is **verified by us**, see above; the boot-time OCT− variant **does not fire on this hardware** |
| 2. Mask-ROM USB | device enumerates as `WL80UBOOT`, USB `4C4A:8057` | needs an RP2040 "USB_KEY" dongle clocking D+ shortly after power-up |
| 3. Full flash R/W | FM-1-Transporter reads 1 MiB in ~3 s | community-reported |

`jl-uboot-tool` lists WL82/AC791N support as **"unknown"**, though a user
reportedly restored a unit with it. lunar-modulator's own notes are the most
candid summary available:

> The owner's FM-1 has matching full backups and passed a small unused-sector
> write and restore… **Restoring a whole image or recovering an application
> that will not start remains untested.**

**Read that as: layer 1 is reasonably trustworthy; layers 2 and 3 are a plan,
not a guarantee.** Do not install anything whose failure you could only
survive via layer 2.

## Before any physical write — the gate

Every item must be satisfied, in order, and recorded here with dates:

- [ ] **Device identified.** `references/lunar-modulator/tools/fm1_identify.py`
      (verified on hardware upstream). Record model, stock version, flash ID.
- [ ] **Full stock flash backup** of unit A, with SHA-256 recorded, stored in
      two places. Verify the dump is 1 MiB and not all `0xFF`.
- [ ] **Backup verified restorable** — ideally by restoring it once. An
      unverified backup is a belief, not a backup.
- [ ] **Recovery hardware in hand** before the first write: the RP2040 USB_KEY
      dongle built and tested, not ordered.
- [ ] **Stock `FM-1.fwsc` from M-VAVE** downloaded and hashed, so the official
      updater is a known-working escape route.
- [ ] **Image layout understood** — the difference between the application
      payload, the `.fwsc` update package and a full flash image, and which of
      them the installer writes.
- [ ] **The exact build chosen**, built reproducibly, run in the emulator
      first, with its SHA-256 recorded.
- [ ] **Explicit authorisation** from Nico for that specific build on that
      specific unit.

Until all of these are ticked, the emulator is the target.

### A note on what the emulator does not prove

Our Dinghy run produced 14 957 accesses to two MMIO addresses the emulator does
not model (`0x00013e08`, `0x00013e0c`). The firmware sounded correct anyway, so
they are probably harmless — but the emulator cannot tell us what those
registers do on silicon.

**Browser or emulator success is not evidence that firmware is safe to flash.**
It is evidence that the logic is right, which is a different claim.

## Data hygiene

Device-specific material — chip keys, unique identifiers, calibration data, the
contents of flash dumps — must not be committed, published or pasted into logs
or issues. `artifacts/` is gitignored for this reason; keep dumps there or
outside the tree entirely.
