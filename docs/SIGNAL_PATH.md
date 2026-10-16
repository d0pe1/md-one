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

## 8. Cost estimate

Measured today: 54,598 cycles per voice per 256-frame block, four voices at
16.2% of budget. The additions:

| Block | Cost | Basis |
| --- | --- | --- |
| **SVF filter** | **9,730 cycles/voice/block** | **measured**, `scripts/measure-svf-cycles.sh` |
| Envelope | ~300 cycles/voice/block | estimate; block rate, not per sample |
| 4 LFOs | ~200 cycles/block total | estimate; block rate, shared |
| int32 bus | roughly neutral | removes per-add saturation |

The filter was measured with `dsp/target/probe_svf.cc` at 0–3 SVFs in series,
four voices, 256-frame block. Cost is linear in filter count:

| Filters | Cycles/block | Per voice | % budget |
| --- | --- | --- | --- |
| 0 (loop baseline) | 4,159 | 1,040 | 0.30% |
| 1 | 40,015 | 10,004 | 2.87% |
| 2 | 81,996 | 20,499 | 5.89% |
| 3 | 120,911 | 30,228 | 8.68% |

**Two filters per voice costs 5.6% of the budget.** Against the measured
engine at 16.2%, the whole instrument lands near **21.7%**.

One nuance on the usual "the emulator is optimistic" caveat: it applies far
less to the filter than to the oscillator. `Svf::Process` reads
`lut_svf_cutoff` and `lut_svf_damp` **only when a parameter changed** (the
`dirty_` flag) — the per-sample path is pure register arithmetic with no
flash access. The oscillator, by contrast, reads wavetables every sample. So
the filter figure should track real silicon closely, while the oscillator
figure is the one that may inflate.
