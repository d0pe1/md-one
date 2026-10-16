# Firmware safety and recovery

## Status for this bootstrap

**No firmware was written to any device. No device was connected.**

Concretely, during this bootstrap we did **not**:

* connect an FM-1 (none exists here — a USB scan found only a Focusrite
  interface, an AKAI MIDI Mix and a virtual MIDI driver);
* run `tools/fm1_install.py` or any other installer;
* send any SysEx to any device;
* erase, write or read device flash;
* touch a bootloader region;
* attempt any recovery procedure.

Everything was built and tested on the host and in the emulator. The only
network writes were `git clone` and HTTPS downloads of published artifacts.

## Device policy

Two FM-1 units are on order.

| Unit | Role | Policy |
| --- | --- | --- |
| **A** | development | the only unit that ever receives our firmware |
| **B** | reference | stays stock |

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

## Recovery paths — documented, unproven

Three layers are described upstream. Their validation status differs, and none
of it has been confirmed by us.

| Layer | Trigger | Status |
| --- | --- | --- |
| 1. USB rescue | hold **OCT−** at power-on; automatic after two boot crashes | implemented in Dinghy; **verified by its author on hardware**, not by us |
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
