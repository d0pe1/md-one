# MD-One — concept and roadmap

## The instrument

A four-voice drone machine for the M-VAVE FM-1, built on Mutable Instruments'
Braids, delivered into a DAW over USB audio.

**What it is for.** Making evolving textures and drones that get recorded. It
is a studio instrument, not a performance box and not a standalone toy — the
built-in speaker is irrelevant to it. One USB-C cable carries MIDI in and a
16-bit stereo audio input, so the exact samples the engine writes arrive in
Ableton, with no D/A → A/D round trip.

**What it is not.** Not a groovebox: no sequencer, no pattern memory, no song
mode. Not a reverb box: the DAW has better reverb for free, so the device
spends its memory on tuned resonance instead. Not a polysynth: four voices
that stay on, not twelve that come and go.

**The shape of it.** Four always-on voices, each a Braids macro oscillator
through a tuned comb, with slow drift and four shared LFOs. Voices are
latched, not held. Notes are decisions, not gestures.

---

## Where we are

Feasibility is settled. Everything below was measured, not assumed.

| | Status | Evidence |
| --- | --- | --- |
| pi32v2 toolchain | **proven** | reproduces Dinghy 0.3 byte-for-byte |
| C++ on the target | **proven** | Braids compiles, 99 KB of 526 KB |
| Braids port correctness | **proven** | matches a 96 kHz reference, 0.0 cents / 0.0 dB |
| Tables at 44.1 kHz | **proven** | regenerated; reproduces upstream at 96 kHz, all 45 tables identical |
| Engine on the chip | **proven** | 4 voices = 20.6% of the audio budget |
| Signal path complete | **built** | two filters, EQ, drive, AHDSR, patchbay, mixer — `SIGNAL_PATH.md` §8 |
| Firmware boots and sounds | **proven** | runs in `fm1-live`, all brick checks pass |
| FM feasibility | **measured** | 7,553 cycles/operator; 12 voices × 6 ops = 37.7% — `BRAIDS_PORT.md` |
| Playable UX | **built** | seven menus, every parameter on a page, slewed so CC does not zipper |
| Hardware | **none** | two units ordered, not arrived |

The 20.6% is the engine as it stands: four voices, two filters, metering, an
AHDSR and parameter slewing. A loaded patch — three EQ bands and drive on
every channel — is **32%**.

The headroom is real but optimistic: the emulator models 1 instruction per
cycle and every Braids table lives in XIP flash. **At a pessimistic 3× for
stalls, 20.6% becomes 62% and the 32% patch becomes 96%.** That no longer
clears the "stay under half" rule, and it is the first number in this project
that hardware could plausibly refute. It does not change what to build next —
the UX costs nothing at audio rate — but it does mean the block list is now
bounded by measurement rather than by taste, and the 3× figure itself is a
guess that only a real device can settle.

---

## Phases

### Phase 0 — Feasibility ✅ complete

Toolchain, Braids port, 44.1 kHz tables, drone engine, firmware that boots and
drones. Verified against a reference implementation, not by ear.

### Phase 1 — Signal path, then the control surface

**The signal path comes first.** `docs/SIGNAL_PATH.md` defines the blocks,
their order, gain staging and modulation attachment points. The menus are
derived from it — specifying the control surface first was backwards, and
produced a UX spec for blocks that do not exist.

**The signal path is now built.** OPEN-1, OPEN-2 and OPEN-3 are closed
(`SIGNAL_PATH.md` §7) — the first two dissolved rather than being decided, once
two multimode filters per voice made the ordering a per-patch choice. OPEN-4
and OPEN-5 remain, and both are block-list questions answerable by measurement,
not architecture questions.

Built and measured, in order along the chain:

1. ✅ Two **multimode filters** per voice — LP/BP/HP/COMB/RESO, with the comb
   as a *mode* rather than a separate block. The comb turned out cheaper than
   the SVF, so folding it in cost nothing.
2. ✅ **VCA** driven by a full **AHDSR**, per sample rather than per block —
   block-rate gain is a 172 Hz staircase.
3. ✅ **Three-band multimode EQ and drive** on every mixer channel.
4. ✅ **int32 mix bus with a single saturation point**, replacing the per-add
   saturation that distorted the mix rather than limiting it.
5. ✅ **FX patchbay** — four channels, one cable out of each, any routing
   including feedback. Modular, not a reorderable rack.
6. ✅ **Mixer** — four strips, levels, pan, nine-segment meters, on one page.

**The control surface is now built too**, from `docs/UX_DESIGN.md`:

7. ✅ **Page framework** — seven menus, fifteen pages, each exposing exactly
   four parameters. The map is `dsp/engine/fm1_pages.h` (MIT, host-tested);
   only the drawing is in the firmware.
8. ✅ **Every parameter is reachable.** All 42 per-voice and 25 global
   parameters sit on a page, and `dsp/tests/test_pages.c` fails if one stops
   being reachable — the phase's own done-condition, as a test.
9. ✅ **Parameter smoothing** — a one-pole slew at block rate, 0.16% of
   budget (`SIGNAL_PATH.md` §8).

10. ✅ **The envelope curve**, drawn live with the current segment marked.
    The one parameter group where the picture *is* the parameter: four
    numbers do not tell you that a long attack running straight into a decay
    never arrives anywhere, but the shape does.

*Still open in Phase 1:* the EQ curve drawing, which is decoration over
controls that already work.

The point was to make it operable by someone who did not build it. The
remaining gaps are Phase 2's: LFO and MOD have buttons but no menus, because
their parameters do not exist yet.

Build the Menu/Page framework from `docs/UX_DESIGN.md` before adding features,
so every later feature has an obvious home.

1. Page framework: menu state, page scrolling on OCT±, SHIFT on SEL.
2. Screen layout: the fixed zones, four labels over four values.
3. Parameter table drives label, range, default, CC and patch byte — one
   declaration each.
4. Port the existing parameters onto pages: EDIT/BRAIDS, EDIT/PITCH, FX/COMB.
5. HOME overview page.
6. **Parameter smoothing** — a one-pole slew at block rate. 7-bit CC is 128
   steps and will zipper on a sustained drone.

*Done when:* every existing parameter is reachable through a page, nothing is
reachable any other way, and the whole surface can be explained in a paragraph.

### Phase 2 — Modulation

1. Multimode LFO: triangle, saw, ramp, square, S&H, smooth random.
2. Four global LFOs; LFO menu, one per page (RATE, AMOUNT, PHASE, MODE).
3. Eight modulation destinations per voice, four per ENV page; knob = depth,
   SHIFT + knob = source.
4. Drift stays as the per-voice free-running source, independent of the LFOs.

*Done when:* a slow LFO into comb tune audibly evolves a drone over a minute
without touching anything.

### Phase 3 — Mixer and global

Levels, pan, stereo width, master. Binaural detune and Haas widening, both
checked for mono compatibility before they ship.

### Phase 4 — MIDI and patches

1. Full CC map per `docs/MIDI_SPEC.md`, generated from the parameter table.
2. Patch save/load. One patch is ~149 bytes and the settings sector is 256,
   so multiple slots need a second sector or a tighter format — decide here.
3. Note-on → `Strike()` per voice on channels 1–4.

*Done when:* every parameter can be automated from an Ableton track, and a
patch survives a power cycle.

### Phase 5 — Hardware bring-up

Gated on `docs/FIRMWARE_SAFETY.md`. Nothing is flashed until the checklist is
satisfied, including a verified stock backup and a recovery path proven on
the development unit.

1. Identify the device, back up stock flash, verify the backup restores.
2. `fm1_ticks()` around `app_audio()` → the real cycle cost, finally.
3. Confirm USB audio into Ableton on Windows; resolve the ASIO question.
4. Listen on real hardware and revise the presets.

### Phase 6 — Release

GPL-3.0 source, our own package identity, our own logo (Dinghy's is not
GPL), LICENSING.md crediting Felucca/SLOOP/Dinghy and Emilie Gillet, and a
`.fwsc` on the Firmware Hub.

---

## Principles

Things learned the hard way during Phase 0, worth keeping.

**Measure, do not listen-and-guess.** The bell decaying 2.18× too slowly was
inaudible as a *cause* — it just sounded "a bit off". Comparing against a
reference turned it into a line number. Where a reference exists, use it.

**Build the test that can fail.** The first spectral comparator called an
8-of-12 partial match a MATCH. A test only earns trust once it has been shown
to reject something it should reject.

**The platform's rules are not negotiable.** No float, no 64-bit division, no
register access outside `hal/`, and the audio ISR's deadline is real. The
build enforces the first three; only measurement enforces the fourth.

**Four knobs, four parameters, every page.** The moment a page needs five, the
page is wrong.

---

## Deliberately not doing

| | Why |
| --- | --- |
| Reverb or delay | The DAW does it better and for free. The memory buys tuned combs instead. |
| Sequencer, arpeggiator | Not a groovebox. Ableton sequences it. |
| More than four voices | Four is already twelve oscillators; the earlier "one-dimensional" complaint was about timbre, not count. |
| Polyphonic note handling | Voices are latched and addressed, not allocated. |
| Supporting the internal speaker | Mono, and irrelevant to the use case. |
| Porting Plaits, Rings or Elements | They assume an FPU. **Measured 2026-10-17**: Plaits links now that the SDK supplies soft-float, but its cheapest engine costs 113% of the whole audio budget for ONE voice -- about 35x the dearest Braids shape. Each would be a full fixed-point rewrite. See `docs/BRAIDS_PORT.md`. |
