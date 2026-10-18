# MD-One — signal path

What the sound passes through, in what order, at what level, and which parts
are per-voice versus shared. Written before building, because the current
implementation has a signal path by accident rather than by design.

**Status: built**, apart from the LFOs. Every `[OPEN]`
decision has closed — see §7. Figures in §8 are measured, not estimated.

---

## 1. What this replaced

```
Braids osc ──► comb (insert, wet/dry) ──► ×level ──► pan ──► saturating add ──► ×master
```

Four things in a row. No filter, no bus structure, no routing, and gain
staging that was guessed at (`(gl << 1) / FM1_VOICES`). Two faults fell
straight out of it, and both are fixed:

* **Nothing tamed the comb.** A resonant comb on noise screeches with nothing
  after it — heard directly in the `breath` preset. There are now two filters
  per voice and the comb is one of their modes.
* **It saturated on every voice add.** `fm1_q31_to_q15_sat(out[i] + l)` clipped
  per voice, so the fourth voice distorted against the first three instead of
  the mix simply being loud. There is now one saturation point, after master.

---

## 2. The path

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
                 each voice's own cable: FX1-4, OUT, or nowhere
                            ▼
            ┌──────────────────────────────────────────┐
            │  FX PATCHBAY   4 channels, int32 stereo  │   §5b
            │  one effect and one cable out each,      │
            │  any cable legal, backward costs 1 smp   │
            └──────────────┬───────────────────────────┘
                            ▼
                    ┌──────────────────────────────┐
                    │  MIX BUS   int32, stereo     │   accumulate with headroom
                    └──────────────┬───────────────┘
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
| **F1 / F2** | type, cutoff, res, mix | **built** — one mode list including COMB and RESO |
| **VCA** | A H D S R, level | **built** — exponential, 1.9 ms to 32.4 s |
| **Pan / Mono** | pan, mono | built |
| **Route** | dest | **built** — one cable out, §5b |

### Shared

| Block | Parameters | State |
| --- | --- | --- |
| **FX channels 1–4** | type, p1, p2, mix, dest | **built**, §5b |
| **Mix bus** | — | **built** — int32, one saturation point |
| **Master** | master level | built |
| **LFO 1–4** | rate, amount, phase, mode | **not built** |

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

### Envelope shape — AHDSR, built

Five segments, with every time spanning **1.9 ms to 32.4 s** — measured from
the generated rate table, not from intent:

| | |
| --- | --- |
| **ATTACK** | silence to full |
| **HOLD** | stay at full, flat |
| **DECAY** | fall to the sustain level |
| **SUSTAIN** | a level, not a time |
| **RELEASE** | fall to silence when the gate closes |

Long ranges deliberately: a drone arriving over twenty seconds is an ordinary
thing to want here, and nothing else in the instrument provides it.

**HOLD is the segment that earns its place on this instrument.** A long attack
straight into a decay never arrives anywhere — the two curves meet and the
peak is a corner you cannot hear. A hold gives the top of a swell somewhere to
sit. It also makes attack-decay with no sustain usable as a percussive hit,
and a voice that has decayed to a sustain of zero stops being rendered, so
that patch costs nothing once it has sounded.

**It is exponential, not linear.** A linear amplitude fade is only −6 dB at
its halfway point and −20 dB at nine tenths, so nearly all of the audible
change happens in the last fraction and it sounds like the note hangs and then
drops. A one-pole approach is roughly linear in dB, which is what fading
sounds like.

The rate table is Q24, not Q15. At the slow end the true coefficient is
3.2×10⁻⁶, which rounds to 1 in Q15 — a 0.74 s time constant, so a knob
documented as 32 s would have delivered 3.4 s. `scripts/gen_env_table.py`
computes the settle times in the generated header back *from* the quantised
integers, so the header cannot claim a range the table does not deliver.

**The gate used to click, and that is why this block exists.** It was
`if (!gate) continue;`, which stops a voice mid-waveform: a step from whatever
the oscillator happened to be outputting straight to zero, on every mute, on
every voice. A closed voice now keeps rendering until its release has run out,
and only then costs nothing. `dsp/tests/test_mix.cc` measures the largest
sample-to-sample step while sounding and while releasing, and fails if closing
the gate adds one of its own — currently 6,469 against a natural slew of
12,669.

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
4. **Voice gain is `level × pan` and nothing else.**

   An earlier draft of this rule claimed `(gl << 1) / FM1_VOICES` made level
   depend on polyphony. It did not: `FM1_VOICES` is a compile-time constant,
   so that expression was a flat ½ attenuation applied to every voice
   identically, and turning a voice off never changed the others. The claim
   was wrong and is corrected here rather than quietly dropped.

   The real cost was narrower but still worth removing. A single voice at full
   level, centre-panned, could only ever reach **0.24** of full scale, so the
   instrument threw away roughly 12 dB of output range and then asked the DAW
   to make it up with gain — which also lifts the noise floor. And the
   attenuation bought nothing, because saturation happened per voice add
   anyway (rule 1), so it protected nothing it was supposed to protect.
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

## 5b. Routing — the patchbay

**Every node owns exactly one destination.** Not a source list, not an order —
a destination, like a cable leaving a jack. Voices and effect channels route
identically, which is what keeps this a single idea rather than two:

```
VOICE 1  ──► FX1          FX1 [COMB]   ──► FX2
VOICE 2  ──► FX1          FX2 [CHORUS] ──► OUT
VOICE 3  ──► OUT          FX3 [OFF]    ──► ---
VOICE 4  ──► ---          FX4 [OFF]    ──► ---
```

An effect channel has **no input control**. Whatever is routed to it sums at
its input; routing *to* it is what fills it. What a channel owns is one
effect, a dry/wet mix, and one cable out.

Things that fall out of the model rather than being features:

* **Unplug a channel and everything feeding it goes silent.** `DEST_OFF` is
  not a mute, it is an absent cable.
* **Parallel is not a special case.** Two voices pointing at the same channel
  is just two cables.
* **A voice can bypass everything** — straight to OUT while its neighbours go
  through the comb.

### What this replaced, and why

The first version was Serum's FX rack: four slots in a fixed order, reordered
by moving them, with a `PAR` flag to fake branching. That is an insert rack —
a mixing-desk idea — and it answers "what order are the blocks in". The
question an instrument like this actually asks is "where does this go", which
is the modular one. `PAR` only existed because a chain cannot branch; a
patchbay branches natively, so it is gone.

### Feedback

**Any cable is legal**, including `FX2 → FX1` and `FX1 → FX1`.

Channels are visited in index order within each sample. A cable pointing
*forward* lands in its target before that target runs, so it arrives with no
delay. A cable pointing *backward* lands after the target has already run, so
it is picked up on the next sample — **one sample, 22 µs**. That falls out of
evaluation order; there is no cycle detection and no graph sort.

This is the whole reason the patchbay is evaluated **per sample, not per
block**. At block granularity the same cable would cost 256 samples — 5.8 ms,
a slapback echo at 172 Hz welded into the signal path. VCV Rack behaves the
same way for the same reason.

> An earlier draft forbade backward cables outright. That was the wrong fix
> for the right fear: the hazard was never the loop, it was the block-sized
> delay a block-rate implementation would have given it. Moving the loop
> removed the hazard, and the restriction had nothing left to justify it.
> Refusing to let someone patch a comb into an overdrive and back is not
> safety, it is a missing feature.

What feedback *does* still need is a **level** guard. A loop with gain ≥ 1
grows without bound, and int32 wraps long before anything musical happens —
wrapping turns a scream into white noise, which is the one failure mode that
is not fun. Accumulators are clamped at 8.0, so a runaway saturates into the
soft clip and screams instead. `dsp/tests/test_mix.cc` holds that invariant,
together with the one-sample claim.

### Costs

| | |
| --- | --- |
| **RAM** | 8 KiB per channel for an int32 stereo delay line, 32 KiB for four, plus 8 KiB of per-channel input buffers. Nothing against the pool. |
| **Patch** | 5 bytes per channel, 1 per voice. |
| **CPU** | §8. Shared, so it is paid once rather than per voice — measured, not assumed. |

The delay lines are **int32, not int16**. The comb taught that one: it stored
Q15 and saturated *inside* the loop, before any output gain, so it never
showed as clipping in a rendered file and merely sounded harsh. These sit on a
bus carrying up to 4.0, so int16 would clip on every loud patch for exactly
the same invisible reason.

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

Four voices, TRIPLE_SAW, drift and stereo mix, by configuration:

| Configuration | Cycles/block | % budget | Marginal per voice |
| --- | --- | --- | --- |
| comb only, old inline chain | 225,083 | 16.2% | 54,593 |
| + int32 bus and limiter | 229,357 | 16.5% | 54,060 |
| F1 = COMB, F2 = OFF | 250,316 | 18.0% | 59,432 |
| **F1 = COMB, F2 = LOWPASS** | **295,710** | **21.2%** | 70,895 |

Three things worth reading out of that.

**The bus cost 0.3 points** and *lowered* the marginal per-voice figure
(54,593 → 54,060), because the inner loop stopped saturating on every sample.
The extra went to fixed per-block work — the bus clear and the limiter pass.
Right direction: voices are what scales.

**The filter abstraction cost 1.5 points before doing any filtering.** Going
from an inline comb to `fm1_filter_tick` twice plus the VCA added 5,372 cycles
per voice with F2 still bypassed — roughly 21 cycles per sample for the mode
dispatch, the bypass test and the level multiply. That is the price of modes
being switchable at all, and it is paid whether or not they are used.

**The second filter costs about what it was measured to cost.** F2 as a
lowpass adds 11,463 per voice against the 10,004 measured for a bare SVF; the
difference is the wet/dry blend. The whole instrument at 21.2% lands within a
point of the projection made before any of it was built.

### The FX patchbay

Four voices, F1 = COMB, voices patched into FX1 and the last channel out:

| Patch | Cycles/block | % budget | Channel alone |
| --- | --- | --- | --- |
| nothing plugged | 250,909 | 18.0% | — |
| WIDTH | 281,428 | 20.2% | 30,519 |
| FOLD | 286,921 | 20.6% | 36,012 |
| CHORUS | 300,709 | 21.6% | 49,800 |
| CHORUS → WIDTH | 326,320 | 23.4% | 75,411 |
| **CHORUS → LOWPASS** | **343,787** | **24.7%** | 92,878 |

**An unplugged patchbay costs 75 cycles** — against 250,316 for the engine
with no FX code at all. An instrument not using its effects does not pay for
them.

That took two passes. The first implementation ran the per-sample loop and
cleared 8 KiB of input buffers unconditionally, costing **58,439 cycles — 4.2%
of budget — with nothing patched**. The second still walked all four channels
per sample when one was plugged, costing about 50,000 cycles to discover three
times over that there was nothing to do. Both are fixed by gathering the
plugged channels once per block, which is exact because routing only changes
at knob rate.

**The patchbay costs nothing per voice.** With a chorus patched, each
additional voice costs the same as with no FX at all: the effects run once on
what reaches them, not once per source. That is the measured form of §5b's
claim, not a restatement of it.

**What arbitrary routing costs over a fixed chain.** The same chorus in the
old reorderable-rack design cost 38,239 cycles; here it is 49,800. The 11,561
difference buys the per-sample accumulator, the clamp, and the destination
dispatch — which is to say it buys feedback, branching, and "unplug this and
everything feeding it goes quiet". Worth it, and worth knowing it is not free.

A loaded instrument — four voices, a per-voice comb, a chorus and a filter on
the bus — sits near **25%**, leaving three quarters of the budget unspent.

### The VCA envelope

| | Cycles/block | % budget |
| --- | --- | --- |
| no envelope (hard gate) | 266,002 | 19.1% |
| **AHDSR, per sample** | **284,300** | **20.4%** |

**4,575 cycles per voice**, for a stage switch, a 64-bit multiply and a
compare per sample. Going from a two-stage AR to a five-stage AHDSR cost
nothing measurable — the switch replaced a pair of ternaries.

Per sample rather than per block, deliberately. A gain stepped once per block
is a staircase at 172 Hz -- which is the zipper noise the envelope exists to
avoid, moved to a different frequency rather than removed. This is the one
place in the engine where a block-rate shortcut is wrong, and it is worth
naming as the exception to the rule in the EQ section below.

A released voice stops being rendered once its envelope reaches zero, so the
cost is only paid while a voice is actually sounding.

### The channel EQ and drive

Four voices, F1 = COMB, bands set to a boost:

| Configuration | Cycles/block | % budget | Added |
| --- | --- | --- | --- |
| EQ flat, no drive | 266,002 | 19.1% | — |
| 1 band | 342,149 | 24.6% | 76,147 |
| 2 bands | 386,472 | 27.7% | 44,323 |
| 3 bands | 429,408 | 30.8% | 42,936 |
| drive only | 282,804 | 20.3% | 16,802 |
| **3 bands + drive** | **445,471** | **32.0%** | — |

A band costs about **10,750 cycles per voice**, which is the SVF figure again
(10,004) — unsurprising, since a band *is* a state-variable filter with its
outputs recombined. Drive is **4,200 per voice**: two multiplies and a shift.

The first band costs more than the ones after it (76,147 against ~43,000)
because activating any band also switches on the per-sample EQ path for that
voice. The second and third are the honest marginal cost.

**A flat EQ costs 12,455 cycles — 0.9%.** The first implementation tested each
band's idle state *inside* the sample loop and cost **39,378** while doing
nothing at all. That is the same mistake the patchbay made, fixed the same
way: ask once per block, since band settings change at knob rate. The 0.9%
that remains is two predicted-false branches per sample; removing it would
mean duplicating the voice loop per configuration, which is not worth 0.9%.

This is the third time a per-sample test of a block-rate condition has shown
up as real budget — the patchbay at 4.2%, its channel scan at about 3.5%, the
EQ at 2.8%. Worth stating as a rule rather than rediscovering it: **anything
that only changes when a knob moves is decided once per block.**

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
| **FM operator** | **7,553** | 2.17% | `probe_fm.cc`; flat out to 72 operators. See `BRAIDS_PORT.md` |
| **Parameter slew** | ~560 | **0.16%** | all 193 parameters, every block, settled |

### Parameter slewing

A 7-bit control is 128 steps, and on a sustained drone a step in anything
that multiplies the signal is a click. Every continuous parameter is now
slewed toward its target at block rate — a one-pole closing 1/8 of the gap
each block, so no block moves a value more than 1/8 of full scale.

Measured at **286,550 cycles** for four voices against 284,300 before:
**2,250 cycles, 0.16% of budget**, to walk 4 × 42 voice parameters and 25
globals every block and find almost all of them settled. The settled case is
one compare, which is what makes walking all of them affordable rather than
maintaining a list of what is moving.

Three details that are not arbitrary:

* **It snaps a whole step early.** A one-pole never arrives — the gap shrinks
  by 7/8 a block, so an exact landing takes 62 blocks and for the last 36 the
  value is already within one step and audibly finished. Those blocks are
  pure waste: each one calls back into the engine, and for a filter cutoff
  that means recomputing a coefficient and the comb's tuning to move nothing.
  Snapping at one step cuts a 0→127 sweep from 360 ms to 220 ms and costs
  nothing audible.
* **Without a snap it would never land at all.** `(t − s) >> 3` rounds to
  zero once the gap is under eight, so the last fraction never arrives and
  every slewed parameter sits permanently just short of where it was put.
  Invisible from outside, because the getter returns the target. Same lesson
  as the envelope: watch the step, not the distance.
* **Enums are excluded**, and so are the envelope times. There is nothing
  between COMB and RESO to slide through, and SUSTAIN is already slewed by
  construction — the envelope moves toward it at the decay rate.

A bulk set — a patch load, the boot preset — calls `SnapParams()` and lands
everything at once. Restoring a patch and then hearing 42 parameters per
voice slide into place is a glitch, not a feature.

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
