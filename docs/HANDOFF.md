# MD-One — session handoff

Paste this into a fresh session to continue. It is written to be read once,
top to bottom, before touching anything.

---

## What this is

**MD-One**: a four-voice drone synthesiser firmware for the **M-VAVE FM-1**
(JieLi AC791N, `pi32v2` ISA, **no FPU**, no 64-bit divide), built on Mutable
Instruments Braids oscillators and delivering audio into a DAW over USB.

- Repo: `C:\temp\GitHub\fm1-laboratory` — public at `github.com/d0pe1/md-one`
- WSL distro `archlinux` does all building. Windows side runs git and the flasher.

## Hard constraints — do not violate

1. **Never expose device identifiers, chip keys, or calibration data** in logs or
   commits. `artifacts/` is gitignored for this reason.
2. **Do not redistribute** the JieLi `pi32v2` toolchain or stock M-VAVE firmware.
3. **`dsp/` is MIT and must stay GPL-free.** `references/` contains GPL-3.0
   firmwares (JIANT, hortator, Felucca, lunar-modulator) — **read them for ideas,
   never copy code into `dsp/`.** `firmware/` is GPL-3.0 and may borrow there, but
   hasn't needed to.
4. **Never claim a command succeeded unless it was run and its output seen.**
5. Integer only in `dsp/`: no float, no 64-bit *division* (64-bit multiply is
   fine). `scripts/check-all.sh` enforces this by banning `__udivdi3` et al.

Flashing is routine and authorised.

## The loop

```bash
wsl -d archlinux -- bash scripts/check-all.sh          # 12 checks; must be green
python references\dinghy\tools\fm1_install.py firmware\build\dinghy.fwsc --yes
```

**The flasher needs exclusive access to the MIDI port.** If it reports
`disconnected after 0 requests; nothing was written`, Ableton (or anything
holding `Felucca`) is the cause — nothing was written, the device is safe, close
the DAW and retry. This cost several cycles; check it first.

Useful measurement targets:

| command | what it answers |
|---|---|
| `make -C dsp gain` | where output level goes, per gain stage |
| `make -C dsp fresp` | filter: real −3 dB corner vs what the knob claims |
| `make -C dsp envshape` | envelope curves, time at each tenth |
| ASan build of `test_mix` | catches uninitialised/out-of-bounds (found two real bugs) |

Cycle measurement on real `pi32v2` goes through the emulator harness —
`scripts/measure-drone-cycles.sh`, and ad-hoc ladder scripts in the session
scratchpad. **The emulator is ~1.92× optimistic**; a device/emulator ratio of
**1.96×** was confirmed independently twice.

## Current state

Everything below is on the device and green (`12 passed, nothing failed`).
HEAD is `f11b74b`.

**Signal path**

```
voice 1..4 ─┐                           ┌─ CH1..4 ─┐
            ├─ 4 cables each, DEST+GAIN ┤          ├─ SUM ─ master ─ OUT
FX 1..4   ──┘                           └──────────┘
```

- Every node has **four output cables**, each with a destination and a gain.
  `DEST = ---` unplugs one. Mixer channels have no DEST — they always sum to SUM,
  which is the last stage.
- FX rack runs **block-at-a-time** unless a cable points backward (proven
  bit-identical to the per-sample path via `dsp/host/fx_equiv.cc`).

**Filters** — all trapezoidal (Simper TPT), no Chamberlin left anywhere.
6/12/24 dB all land on the stated cutoff; RES 0 is Butterworth.

**Modulators** — 4 LFOs (rate/amount/phase/wave/jitter/energy/trig/polarity,
waves SINE·TRI·SAW·RAMP·SQR·S&H·SMOOTH) and **4 modulation envelopes** (AHDSR +
per-segment curves + trigger + loop), all available as matrix sources.

**CPU**: four voices with a full chain ≈ **29.5% emulator ≈ 58% device**.
`.pool` is 336 KiB, ~191 KiB used, **~145 KiB free**.

## Outstanding work, in the order I'd take it

### 1. The three missing FX blocks — the user's live request

`FX_NTYPES` is currently `NONE, CHORUS, FOLD, WIDTH, LP, HP`. Still wanted:

- **Downsample / bitcrush** — trivial. Sample-and-hold at a reduced rate plus a
  bit mask. ~0.1% CPU, no RAM. Do this first; it's an afternoon.
- **Delay** — ~88 KiB for 500 ms stereo, ~1% CPU. Fits in `.pool` comfortably.
- **Reverb** — ~64 KiB; **measure CPU before committing**, since four voices
  already sit near 58% on the device.

Adding a type means: `fm1_route.h` enum + names + the per-type `P1`/`P2` label
and bipolar-flag tables, `fx_chan_tick` (per-sample path) **and**
`fx_chan_block` (block path — both must agree), the `FX_NTYPES` static_assert,
and `G_FX*_TYPE` max in `fm1_params.h`.

### 2. The 32-slot global modulation matrix

The agreed design, not yet built. Everything it needs now exists.

- **32 global slots**, each `SOURCE / TARGET / CURVE / AMOUNT`.
- Sources: `---`, LFO1-4, DRIFT, VCA, EN1-4 (`MD_SRC_*`, already defined).
- Targets must reach **any voice, FX channel or mixer channel parameter** — a
  flat enum of ~60 entries, navigated with SHIFT-jump by category the way shapes
  already are. Include an **ALL-VOICES** target so the common gesture is one slot.
- The existing per-voice `P_MD*` destination block (9 fixed destinations with a
  hidden SHIFT source layer) is what this **replaces**. The user called that menu
  "a mess" and was right: the source is invisible until you hold a modifier.
- The `rows` mechanism (`fm1_page_t.rows` / `rstride` / `rowknobs`) exists for
  exactly this — but **it only works when rows sit at a uniform parameter
  stride**. That fits 32 identical slots. It did *not* fit the 9 envelope
  controls, which is why those are two pages.

### 3. Smaller, known

- **FM and MOD are hardwired matrix rows.** They are drift→pitch and
  drift→timbre attenuverters inherited from Braids' CV panel. Once the matrix
  exists they are duplicates that silently sum with equivalent rows. Agreed to
  fold them in and give BRAIDS two knobs back — **not yet done.**
- **The ADSR body draws straight segments**, so it shows timing but not the
  A/R curvature.
- **LFO sync** would mean syncing to incoming USB MIDI clock (bytes reach
  `usb.c:746`); there is no internal tempo.
- `fm1_svf.h` / `fm1_svf_table.h` are now referenced only by comments. Dead, left
  in place deliberately — deleting is the owner's call.
- The banded painter (vendor's 16-row dirty-band scheme in
  `references/mvave-sdk/x/app/screen.c`) was never built; the signature-based
  clear made it unnecessary for now.

## How this user works — read this

- Terse, fast, high trust, **zero patience for work that doesn't match the spec
  given**. Several rounds were lost to building adjacent-but-different things.
  When the ask is ambiguous, say the reading back in one line before building.
- "go" means build it. They will interrupt mid-turn with corrections — honour
  those immediately over the current plan.
- They want **engineering judgement, not compliance**. When their design has a
  flaw, say so with the reason and a recommendation, then build what they decide.
  They have accepted every well-argued pushback and rejected hand-waving.
- **Own mistakes plainly, once, and move on.** No grovelling, no re-litigating.

## Recurring bug families in this codebase

These have each bitten **more than once**. Check for them by reflex.

1. **`>>` on a negative value floors, it does not divide.** Six instances so far:
   detune, comb tuning, resonator tuning, envelope decay, the fine-detune Q8
   rewrite, slot tuning. Round symmetrically about zero for anything bipolar.
2. **A control with high internal resolution whose consumer reads the byte.**
   Fine editing only exists where the engine reads the **Q8 accumulator**
   (`acc[]` / `gacc_[]`). Adding a name to `HasFine` without doing that work
   ships a gesture with no effect. **Test the effect, never the list.**
3. **A knob whose travel collapses to a few values.** `span = 2048 >> (rate >> 4)`
   gave DRIFT eight speeds across 128 positions; GLIDE had the same fault
   earlier. Suspect every shift-based mapping.
4. **A parameter mirrored as a literal in `fm1_drone_c.h`.** Inserting mid-enum
   renumbers everything after. The `static_assert` block catches it — let it.
   Append at the end when unsure.
5. **C++ designated initialisers must follow declaration order.** Moving an enum
   entry without moving its `FM1_PDEF` row fails with an unhelpful message.
6. **A field added to `Drone` without initialising it in `Init()`.** `.pool` is
   zeroed at boot so the device hides it; only a host test with a *local* `Drone`
   finds it. ASan caught `scope_w_` this way.
7. **Ids collide between the voice and global tables.** `G_EN(0,G_EN_REL)` is 71
   and so is `P_SL(0,P_SL_P2)`. Any lookup keyed on a parameter id **must check
   scope first** — this shipped a modulation envelope's RELEASE knob labelled
   `FEEDBK`.
8. **The test can be wrong instead of the engine.** Two cables measured 1.79×
   (the limiter, working); the LFO sine measured 2.5% error (the test sampling a
   block early). Check the harness before believing a failure.

## Things that are true and easy to doubt

- **The LFOs are global**, shared by all voices. DRIFT is per-voice — that is the
  difference between them, and it is deliberate.
- **The VCA envelope must stay per-voice and per-sample.** The four modulation
  envelopes run per-sample but feed block-rate modulation, so their effective
  resolution is 5.8 ms. A half-millisecond click is the VCA's job.
- **The patch is not written on a timer.** A flash erase is ~50 ms with
  interrupts off and a drone never goes quiet, so the old idle autosave was a
  guaranteed dropout. It now writes on SHIFT+SAVE or when every voice is muted,
  and the header shows `*` while unsaved.
- **`PATCH_MAGIC` is `0xDB`** and was bumped three times in one session. Bump it
  whenever a stored byte changes meaning.
