# DRONELAB — signal path

What the sound passes through, in what order, at what level, and which parts
are per-voice versus shared. Written before building, because the current
implementation has a signal path by accident rather than by design.

**Status: proposal.** Decisions marked **[OPEN]** are taste, not engineering,
and are flagged rather than silently resolved. Three have since closed — see
§7 for which, and why.

---

## 1. What exists today

```
Braids osc ──► comb (insert, wet/dry) ──► ×level ──► pan ──► saturating add ──► ×master
```

Four things in a row. No filter, no envelope, no bus structure, and gain
staging that was guessed at (`(gl << 1) / FM1_VOICES`). Two known faults fall
straight out of it:

* **Nothing tames the comb.** A resonant comb on noise screeches with nothing
  after it — heard directly in the `breath` preset.
* **It saturates on every voice add.** `fm1_q31_to_q15_sat(out[i] + l)` clips
  per voice, so the fourth voice distorts against the first three instead of
  the mix simply being loud.

---

## 2. Proposed path

```
                    ┌──────────────── per voice, ×4 ───────────────┐
                    │                                              │
   MIDI / keys ────►│  BRAIDS OSC ──► F1 ──► VCA ──► F2 ──► PAN    │
                    │   shape        multimode  env  multimode     │
                    │   timbre       LP BP HP  ×level  (same       │
                    │   color        COMB RESO          block)     │
                    └───────┬─────────┬────────┬────────┬──────────┘
                            │         │        │        │
         drift (per voice) ─┘         │        │        │
         LFO 1-4 (shared) ────────────┴────────┴────────┘
                            │
                            ▼
                    ┌──────────────────────────────┐
                    │  MIX BUS   int32, stereo     │   accumulate with headroom
                    └──────────────┬───────────────┘
                                   ▼
                       [ FX RACK ]  ordered, reorderable  (§5b)
                                   ▼
                            ×master ──► soft clip ──► Q15 out
                                   │
                                   ├──► I2S / DAC  (headphones)
                                   └──► USB audio  (the DAW)  ← the real output
```

### Per voice

| Block | Parameters | State |
| --- | --- | --- |
| **Oscillator** | shape, timbre, color, FM, MOD | built |
| **F1 / F2** | type, cutoff, resonance | **not built**; SVF measured, comb exists as a separate block to be folded in as a mode |
| **VCA** | attack, release, level | **envelope not built** |
| **Pan** | pan | built |

### Shared

| Block | Parameters | State |
| --- | --- | --- |
| **LFO 1–4** | rate, amount, phase, mode | **not built** |
| **Mix bus** | — | needs rework (see §4) |
| **FX rack** | per slot: type, mix, PAR, bypass | **not built**, see §5b |
| **Master** | master level | built |

---

## 3. Chain order, and why

### Two filters, not a filter and a comb — [OPEN-1, closed]

The original question was whether the filter sits before or after the comb:
before, and the comb resonates a shaped source; after, and the filter tames
comb screech, which is the fault heard in the `breath` preset.

With **two** multimode filters the question dissolves. F1 shapes what resonates,
F2 tames what leaves — and because COMB and RESO are *modes of the same block*
(§5b), the ordering is a per-patch choice, not a design decision. A patch that
wants the classic order sets F1 to lowpass and F2 to comb; one that wants the
screech cure does the reverse.

### VCA before the resonant blocks — [OPEN-2, closed]

The requested chain is `OSC → F1 → VCA → F2 → out`, which puts the VCA ahead
of F2. Resonant blocks therefore ring on after a voice stops, like a struck
string. That is the musical choice for a drone instrument and it is taken
deliberately.

The consequence is accepted rather than designed around: **muting a voice
leaves a tail.** MUTE closes the VCA and lets the tail decay; **SHIFT+MUTE**
is a hard kill that also clears filter and rack state, for the times when
silence has to be immediate.

### Envelope shape

Voices are latched and mostly static, so the envelope is not an ADSR. Two
parameters:

| | |
| --- | --- |
| **ATTACK** | 0 → instant, max → ~30 s fade-in |
| **RELEASE** | 0 → instant, max → ~30 s fade-out |

Long ranges deliberately: a drone fading in over twenty seconds is a normal
thing to want, and nothing else in the instrument provides it. The current
hard gate produces a click, which is the entire reason this block exists.

---

## 4. Gain staging and headroom

The current scheme is the weakest part of the implementation and is replaced
wholesale.

**Rules**

1. **One saturation point.** Voices accumulate into an `int32` bus. Saturation
   happens once, after master. Per-add saturation is removed — it distorts the
   mix rather than limiting it.
2. **Unity is one voice at full level, centre-panned.** Not four.
3. **The bus carries 2 bits of headroom** (±4× full scale). Four correlated
   voices at full level reach the top of it without clipping the bus itself.
4. **Voice gain is `level × pan`, not scaled by voice count.** Turning a voice
   off must not change the level of the others — the current `1/FM1_VOICES`
   makes level depend on polyphony, which is wrong.
5. **Master is the only thing between the bus and the output**, and the soft
   clip after it is the one place the instrument is allowed to distort.
6. **USB audio taps after master and after the clip** — the DAW records
   exactly what the DAC gets (`platform.c` already does this).

**Why a soft clip rather than hard.** Four detuned drone voices spend most of
their time near full scale by nature. Hard clipping produces broadband
splatter; a soft knee is a gentler failure that fits the "characterful
digital" brief without being an accident.

> The MASTER pot sits in the recording path. For DAW work it should be at
> unity; a future global page should show when it is not.

---

## 5. Modulation attachment points

Every modulatable destination, and where it attaches:

| Destination | Block | Notes |
| --- | --- | --- |
| PITCH | oscillator | continuous on every shape |
| TIMBRE | oscillator | **stepped** on TRIPLE / FM / wavetable shapes |
| COLOR | oscillator | same caveat |
| F1 CUTOFF | voice filter 1 | in COMB mode this is comb tune — the most drone-useful target |
| F1 RESO | voice filter 1 | in COMB mode, feedback |
| F2 CUTOFF | voice filter 2 | same block, same modes |
| F2 RESO | voice filter 2 | |
| LEVEL | VCA | |
| PAN | pan | |

Nine per-voice destinations, plus the global rack's slot parameters, which are
shared and modulated once. Sources are the four shared LFOs plus the per-voice
drift, depth bipolar.

Folding the comb into the filter is what keeps this list short: COMB TUNE / FB
/ MIX stop being three separate destinations and become the filter's existing
cutoff, resonance and mix. Nine destinations is **three Pages of four** (with
one spare), which lands cleanly on the `UX_DESIGN.md` taxonomy instead of
fighting it.

---

## 5b. Routing — the Serum model

An earlier draft of this section proposed a per-voice matrix of slots wired by
a source bitmask: a small DAG, acyclic by a lower-index-only rule. It is
replaced, because Serum answers the same question with far less machinery and
a much better fit for four knobs and a two-line screen.

**Serum's routing is not a graph. It is a line with escape hatches.** Sources
sum, pass an optional per-voice filter, then run a strictly serial FX rack
whose *order* you change by dragging. The only parallelism in the main path is
a per-source bypass toggle. Four ideas transfer directly:

1. **The second filter is not special — it is a rack entry.** Serum's main
   filter lives in the voice; a *Filter* also appears in the FX rack. This
   deletes [OPEN-1]: "filter before or after the comb" stops being an
   architecture decision and becomes a per-patch reorder.
2. **Reorder, never rewire.** `uint8_t order[N]`, a permutation, evaluated as
   a plain `for` loop. No bitmask, no cycle detection, no summing node. The
   whole editor is one Page: *slot · type · mix · move* — exactly four knobs,
   which is the taxonomy in `UX_DESIGN.md` without bending it.
3. **Toggles, not patch cables.** Serum's per-oscillator `FIL` button is a
   routing decision expressed as one bit.
4. **The comb belongs in the filter, not the rack.** Serum lists comb and
   flanger as *filter types*, because they are the pitch-tracking part and the
   filter is the per-voice block. The rack gets what wants to run once on a
   summed signal.

### Shape

```
PER VOICE (×4)
  OSC ──► F1 ──► VCA ──► F2 ──► pan ──┐
          │               │           │
     multimode:      the same         │
     LP BP HP        block type       ▼
     COMB RESO       list          MIX BUS (int32)
                                      │
GLOBAL                          [ FX RACK ] ──► master ──► soft clip
                                 ordered, reorderable
                                 chorus · fold · width · filter · …
```

Two filters per voice, both multimode, either able to be a comb. The chain
asked for — `OSC → F1 → VCA → FX MATRIX → F2 → out` — becomes
`OSC → F1 → VCA → F2 → [rack]`: the same blocks, one fewer concept, and the
stereo effects run once instead of four times.

### The one thing a pure chain cannot express

`comb ∥ reso → chorus` — two blocks fed the same input, summed. Recovered with
one Serum-ish toggle rather than a graph: a per-slot **PAR** flag meaning
*take the same input as the slot above me; our outputs sum*.

```
  1  COMB              in = chain
  2  RESO     [PAR]    in = same as slot 1  ─┐
                                             ├─► sum
  3  CHORUS            in = that sum        ─┘
```

One bit, one scratch buffer. Chains of PAR give N-way parallel, the
lower-index-only rule still holds trivially, and it presents as a toggle on a
Page rather than a patch matrix.

```c
typedef struct {
    uint8_t type;   /* NONE, CHORUS, FOLD, WIDTH, FILTER, ... */
    uint8_t mix;    /* dry/wet for this slot */
    uint8_t flags;  /* bit0 PAR, bit1 bypass */
    uint8_t p[5];   /* the block's own parameters */
} fx_slot_t;        /* 8 bytes */
```

### Costs

| | |
| --- | --- |
| **RAM** | the rack is global now: one `int16` stereo block buffer per slot, 8 slots × 256 × 2 ch × 2 B = 8 KiB **total**, not per voice. Nothing against the 306 KiB pool. |
| **Patch** | 8 B × 8 slots = 64 B for the rack, plus the per-voice filter params. Was 128 B under the matrix design. Either fits once the 256-byte settings cap is raised — `static uint8_t buf[256]` and `n > 256u` sit inside a **4 KiB** sector, so it is a two-line change in our fork. |
| **CPU** | still the only real constraint, but the shape improved: rack blocks now cost **once**, not ×4. Only F1 and F2 are per-voice. |

### Feedback

A slot reading a *later* slot is a cycle, and in a block engine it needs a
one-block delay — 5.8 ms, audible as a short pre-delay and able to ring.
**Forbidden in v1.** Worth adding later as an explicit source with its latency
documented, rather than arriving as an accident.

### What remains open — [OPEN-5]

The Serum mapping resolves the *structure*; what is left is the block list.

> **[OPEN-5]** Which block types the per-voice filter offers beyond
> LP/BP/HP/COMB/RESO, and which the global rack offers. Costs must be measured
> per block before the list is fixed — the SVF is measured at 9,730
> cycles/voice/block, the comb is not yet broken out.

## 6. Deliberately not here

| | Why |
| --- | --- |
| Reverb / delay | The DAW does it better and free. Memory buys tuned combs instead. |
| Voice→voice FM | Braids is self-contained; cross-voice modulation means re-entering its render loop, which it is not built for. |
| Per-voice EQ | The filter covers it. |
| Sidechain / ducking | Nothing to duck against in a drone instrument. |

---

## 7. Open decisions

### Closed

| | Decision | Resolution |
| --- | --- | --- |
| **OPEN-1** | Filter before or after comb | **Dissolved.** With two multimode filters per voice, F1 shapes what the comb resonates and F2 tames what leaves it. Both sides of the argument are available, and the comb is itself a filter mode, so the order is a per-patch choice rather than an architecture decision. |
| **OPEN-2** | VCA pre- or post-comb | **Pre.** Settled by the requested chain `OSC → F1 → VCA → … → out`: resonant blocks ring on after a voice stops, which is the struck-string behaviour worth having. The consequence is accepted deliberately — muting a voice leaves a tail. MUTE closes the VCA; **SHIFT+MUTE** is a hard kill that also clears the filter and rack state. |
| **OPEN-3** | Comb per-voice insert, shared send, or both | **Superseded by the §5b Serum mapping.** The comb is a per-voice *filter mode*; the shared rack holds the global blocks. Both, by construction. |

### Open

| | Decision | Proposed | Why it is open |
| --- | --- | --- | --- |
| **OPEN-4** | Filter type set | **SVF: LP / BP / HP**, plus COMB and RESO as modes of the same block | The SVF is already compiled in and **measured** at 9,730 cycles/voice/block; notch is near-free (`in − bp`); a ladder would be an unported block with an unknown cost |
| **OPEN-5** | Which blocks the voice filter and the global rack each offer | measure first | Only the SVF has a measured figure. The comb is built but not broken out, and nothing else exists yet |

**OPEN-5 now has the longest reach**, but it is no longer an architecture
question — the structure is settled and the remaining work is a block list
backed by measurements. That ordering is the point: measure, then choose.

---

## 8. Measured cost

Everything here is measured on the emulator at one instruction per cycle, by
`scripts/measure-fx-cycles.sh` (blocks) and `scripts/measure-drone-cycles.sh`
(engine). Budget is 1,393,152 cycles per 256-frame block.

### The engine as it stands

| Voices | Cycles/block | % budget | Marginal per voice |
| --- | --- | --- | --- |
| 1 | 61,337 | 4.4% | — |
| 2 | 115,909 | 8.3% | 54,572 |
| 3 | 170,490 | 12.2% | 54,581 |
| 4 | **225,083** | **16.2%** | 54,593 |

### Per block type

Measured with `dsp/target/probe_fx.cc`, N instances in series, four voices,
256-frame block.

| Block | Per voice per instance | Four voices | Notes |
| --- | --- | --- | --- |
| **SVF filter** | **10,004** | 2.87% | linear in count: 1/2/3 filters = 2.87% / 5.89% / 8.68% |
| **Comb** | **8,468** | 2.43% | *cheaper than the filter*; 8,482 at two voices, so the figure is stable |
| Envelope | ~300 | — | estimate; block rate, not per sample |
| 4 LFOs | ~200 total | — | estimate; block rate, shared |
| int32 bus | roughly neutral | — | removes per-add saturation |

**The comb costs less than the SVF.** That is the result that matters for
§5b: folding comb and resonator into the filter's mode list is not a
compromise made to save cycles — the mode that tracks pitch is the cheaper
one. Two multimode filters per voice costs **5.9%** at worst, and less
whenever either is set to a comb mode.

Against the engine's 16.2%, the whole instrument lands near **22%**, leaving
the global FX rack roughly three quarters of the budget to spend — and rack
blocks are paid once, not per voice.

### How much to trust these numbers

The usual "the emulator is optimistic" caveat applies unevenly.

* **SVF — trust it.** `Svf::Process` reads `lut_svf_cutoff` and `lut_svf_damp`
  only when `dirty_` is set. The per-sample path is pure register arithmetic
  with no flash access, so real silicon should track closely.
* **Comb — treat it as a floor.** A 4 KiB delay line read at two taps per
  sample is exactly the access pattern an emulator flatters. This is the block
  most likely to cost more in reality.
* **Oscillator — the one that may inflate.** It reads wavetables every sample.

### A probe defect found while measuring this

`probes/harness/probe.h` hardcodes `RESULTS` at `0x01C10000`, 32 KiB into the
probe's RAM region. `probe_drone.cc` holds four `BraidsVoice` objects at 17,152
B each and ran its `.bss` ~56 KB past that address, writing its own pass/fail
verdict over its own DSP state while reporting "0 failures".

Separately, `probe_drone.cc` referenced `P_DRIFT_DEPTH`, which had been split
into `P_FM` and `P_MOD` — so it had not compiled since that change, and its
figures came from binaries predating it.

Both are fixed: `dsp/target/probe.ld` now places probe RAM *above* the results
block, with an `ASSERT` tying the two together, and the probe sets both
attenuverters. **Re-measured after the fix, the engine came back at 225,083
cycles against the 225,103 recorded before — a 20-cycle difference.** The
headline figure was right; the probe that produced it was not trustworthy, and
now is.
