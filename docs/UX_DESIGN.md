# MD-One — interaction design

The instrument's control surface, defined before it is built. Everything here
is a rule the firmware must obey, not a suggestion.

The reference point is Elektron's model — Digitone II in particular. The FM-1
has a strikingly similar surface: four encoders over a twelve-button grid, a
small screen, a dedicated octave pair. It should feel like that family, not
like a settings dialog.

---

## 1. Taxonomy

Four concepts. Nothing else is invented.

| Term | Definition |
| --- | --- |
| **Menu** | A collection of Pages on one topic: Oscillator, Filter, Modulation, LFO, Mixer, Global, Patch. Reached by one dedicated button. |
| **Page** | One screenful inside a Menu. Exposes **exactly four Parameters**, one per knob. |
| **Parameter** | A single named value, 0–127 (or an enum), owned by a Subject. Every Parameter is automatically MIDI-CC addressable. |
| **Subject** | What the current Menu acts on — voice 1–4, FX channel 1–4, mixer channel 1–4, or LFO 1–4. Chosen by the SELECT encoder. Global menus have no Subject. |

A **Modifier** (SHIFT) is not a concept in the hierarchy; it is a hardware
state that reveals a second function on controls that have one.

---

## 2. Physical controls

From the panel (`references/hortator/docs/panel.jpg`), confirmed against
`firmware/docs/API.md`.

**Left of the screen**

| Control | Type | Reserved role |
| --- | --- | --- |
| MASTER | pot | Output volume. Owned by `platform.c`, never remapped. |
| SELECT | encoder | **Subject select** — voice 1–4, LFO 1–4. Always. |
| ALGORITHM | encoder | **Primary enum** of the page — oscillator shape, LFO mode. SHIFT + turn jumps by category. |
| PRESETS | encoder | **Patch select**. Always. |
| OCT− / OCT+ | buttons | **Octave of the selected voice**, as printed. Always. |

**Right of the screen**

| Control | Type | Reserved role |
| --- | --- | --- |
| KNOB1–4 | encoders | **The page's four Parameters**, left to right. Always. |
| 12 buttons | 6×2 grid | Menu select, transport, and toggles. |

**Elsewhere**: 27 keys, 14 button LEDs, 27 key LEDs, 240×240 screen.

The grid, as printed:

```
  FX    SEL   ENV   LFO   EDIT  GLO
  HOME  SAVE  ARP   SEQ   PLAY  REC
```

---

## 3. The invariants

These never change, on any page, in any menu. They are what makes the device
learnable rather than memorised.

1. **Four knobs, four parameters.** Every page exposes exactly four. A page
   with fewer shows `--` for the unused ones; it never borrows a knob for
   something else.
2. **OCT− / OCT+ always transpose the selected voice**, exactly as the panel
   prints them, in every menu and on every page. A button with a printed
   label does the printed thing or the panel is lying.
3. **SELECT only ever chooses the Subject.** Turning it never edits a value.
4. **SHIFT is only ever SHIFT.** It has no function of its own, on any page.
5. **A Menu button enters its Menu at page 1, and pressing it again advances
   to the next page**, wrapping at the end. SHIFT + the button returns to
   page 1 from anywhere. One button reaches every page of its own menu, so
   no separate page control is needed.
6. **Every Parameter is a MIDI CC**, derived from one table. If it is on a
   knob it is automatable, with no separate mapping to maintain.
7. **The screen always shows where you are**: menu, subject, page N of M.

---

## 4. Button map

**Selecting a menu is pressing its button.** There is no menu list, no
hierarchy to descend, and no "back". Six menus sit on six dedicated buttons,
each lit when open. From anywhere, one press reaches any menu.

Each menu button **enters its menu at page 1, then cycles its pages** on
repeated presses. SHIFT + the button returns to page 1.

| Button | Press | SHIFT + press |
| --- | --- | --- |
| **FX** | Effect-channel menu → next page | page 1 |
| **SEL** | **SHIFT** (dedicated modifier) | — |
| **ENV** | Modulation menu → next page | page 1 |
| **LFO** | LFO menu → next page | page 1 |
| **EDIT** | Oscillator menu → next page | page 1 |
| **GLO** | Global menu → next page | page 1 |
| **HOME** | Overview page | — |
| **SAVE** | Patch menu | save to current slot |
| **ARP** | **MIX** — the mixer (one page) | — |
| **SEQ** | *(reserved)* | — |
| **PLAY** | mute / unmute Subject voice | **solo** Subject voice |
| **REC** | strike all voices | strike Subject voice only |

**SHIFT is SEL.** It sits top row, second column — reachable by the right
hand without leaving the knobs, which is what a held modifier needs. Its
printed name is the least informative on the grid, so least is lost. The
"select" idea it implies lives on the SELECT encoder, which is named for it.

> This is the one mapping worth revisiting after the first hour of playing.
> The alternatives are HOME (bottom-left, the Elektron FUNC position, but a
> genuinely useful Menu button) and FX.

---

## 5. Menu and page map

Subject is shown where it applies. Knobs are listed KNOB1 → KNOB4.

### EDIT — Oscillator *(Subject: voice 1–4)*

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 | ALGORITHM |
| --- | --- | --- | --- | --- | --- |
| 1 **BRAIDS** | TIMBRE | COLOR | FM | MOD | shape (0–47) |
| 2 **PITCH** | OCTAVE | DETUNE | GLIDE | — | — |
| 3 **F1** | CUTOFF | RES | MIX | — | filter type |
| 4 **F2** | CUTOFF | RES | MIX | — | filter type |
| 5 **EQ** | FREQ | GAIN | RES | MIX | band mode |
| 6 **ROUTE** | **OUT** | LEVEL | PAN | MONO | — |

Pages 3 and 4 are the two per-voice filters, `OSC → F1 → VCA → F2 → pan`. They
share one mode list — `OFF LOWPASS BANDPASS HIGHPASS COMB RESO` — so the comb
is a filter setting rather than a separate block, and "filter before or after
the comb" is something you choose per patch instead of a fixed order.

CUTOFF means what the mode needs it to mean: an absolute cutoff for the
state-variable modes, a semitone offset from the note for RESO, a delay ratio
for COMB. The screen labels it accordingly, because a knob called CUTOFF that
sets a delay ratio is a knob nobody trusts.

#### The EQ page

Three bands, each multimode, in the Serum idiom — **SELECT chooses which band
the knobs are driving**, so one page serves all three rather than three pages
serving one each.

| Band | ALGORITHM picks | What the knobs mean |
| --- | --- | --- |
| **LOW** | SHELF · **HIGHPASS** | FREQ · GAIN · RES · MIX |
| **MID** | PEAK · **NOTCH** | FREQ · GAIN · RES · MIX |
| **HIGH** | SHELF · **LOWPASS** | FREQ · GAIN · RES · MIX |

```
┌────────────────────────────┐
│ EDIT · EQ            5/6   │
│ VOICE 1 · BAND 2 MID       │
├────────────────────────────┤
│  FREQ  GAIN   RES   MIX    │
│   072  +048   040   127    │
│                            │
│      ╭─╮                   │
│  ────╯ ╰──────────         │  the curve, all three bands
│   LO    MID     HI         │
│   ·      ▲      ·          │  ▲ marks the selected band
└────────────────────────────┘
```

The low band's pass is a **high**-pass and the high band's is a **low**-pass.
That reads backwards and is obvious in use: the low band owns the bottom of
the spectrum, so its pass mode is the one that takes the bottom away.

**RES and MIX per band are what make three bands enough.** Resonance is the
difference between a tone control and a character one; a mix on a single band
is parallel EQ, and blending a hard notch to 50% is a different, softer thing
than a gentle notch. Without those, three bands would be a tone stack.

A band at centre gain is **bit-exactly bypassed** — a flat EQ must not colour
anything, because every patch would inherit it invisibly.

> **EQ is not the filters.** F1 and F2 are where you carve: they track pitch,
> they go to comb and resonator modes, they are the instrument's voice. The EQ
> is where you fix a voice that is boxy or thin without changing what it is.
> Keeping them separate is also what lets the EQ be skipped entirely when
> flat, which is how most voices sit most of the time.

OCTAVE appears both on KNOB1 here and on the OCT−/OCT+ buttons. That is not a
duplicate rule but a shortcut: the Parameter lives in the table once, so it
has one CC and one patch byte, and the buttons are simply a faster path to it
from any menu.

#### Choosing a shape

The ALGORITHM encoder selects the voice's oscillator model — the right name
for it, since on the stock FM-1 that encoder chooses the FM algorithm.

**48 shapes on a bare encoder would be unusable.** Braids itself gets away
with it because its four-character display teaches you the codes over months.
We have a 240×240 screen, so the BRAIDS page spends its free area on a list
window rather than a number:

```
        41  FILTERED NOISE
        42  TWIN PEAKS
   ►    43  CLOCKED NOISE          < NOISE 3/5 >
        44  GRANULAR CLOUD
        45  PARTICLE NOISE
```

Turning ALGORITHM moves the cursor; the shape changes as you pass it, so you
hear every one on the way. **SHIFT + ALGORITHM jumps whole categories**, which
is what makes 48 navigable — three detents instead of thirty to get from a
saw stack to a bowed string.

| # | Category | Shapes |
| --- | --- | --- |
| 0–4 | **ANALOG** | CSAW, MORPH, SAW/SQUARE, SINE/TRI, BUZZ |
| 5–8 | **SUB & SYNC** | SQUARE SUB, SAW SUB, SQUARE SYNC, SAW SYNC |
| 9–13 | **TRIPLE** | SAW, SQUARE, TRIANGLE, SINE, RING MOD |
| 14–16 | **SWARM** | SAW SWARM, SAW COMB, TOY |
| 17–20 | **FILTER** | DIGITAL LP, PK, BP, HP |
| 21–23 | **VOWEL** | VOSIM, VOWEL, VOWEL FOF |
| 24–27 | **HARMONIC** | HARMONICS, FM, FEEDBACK FM, CHAOTIC FM |
| 28–31 | **PHYSICAL** | PLUCKED, BOWED, BLOWN, FLUTED |
| 32–36 | **STRUCK** | BELL, DRUM, KICK, CYMBAL, SNARE |
| 37–40 | **WAVETABLE** | WAVETABLES, WAVE MAP, WAVE LINE, PARAPHONIC |
| 41–45 | **NOISE** | FILTERED, TWIN PEAKS, CLOCKED, GRANULAR, PARTICLE |
| 46–47 | **MISC** | DIGITAL MOD, QUESTION MARK |

All 48 stay reachable. The drum shapes (KICK, CYMBAL, SNARE) are near-useless
for drones but are not hidden — they live in one category you skip in a single
jump, which costs nothing and avoids a "where did it go" problem later.

Two shape facts the screen must surface, because they are invisible otherwise
and both have already caused confusion:

* On **TRIPLE**, **HARMONIC/FM** and **WAVETABLE** shapes, TIMBRE and COLOR
  index a *quantised* table — semitone intervals, frequency ratios, discrete
  waves. The knob steps rather than glides. The value row shows these as
  stepped (`INTV +7`) rather than a bare number, so a knob that jumps does
  not read as a knob that is broken.
* On the **TRIPLE** shapes the voice is a **chord generator**: TIMBRE and
  COLOR set the intervals of the second and third oscillators.

Page 1 is Braids' own front panel, minus FINE and COARSE — pitch comes from
the keys and the PITCH page, so tuning knobs would be redundant. FM and MOD
are the module's attenuverters: with no CV jacks, they scale the internal
drift into pitch and timbre. Both bipolar, centre = none.

### FX — Effect channels *(Subject: FX 1–4)*

Four effect channels. SELECT chooses which one you are looking at, exactly as
it chooses a voice elsewhere — one Subject concept, not two.

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 | ALGORITHM |
| --- | --- | --- | --- | --- | --- |
| 1 **EFFECT** | P1 | P2 | MIX | — | effect type |
| 2 **ROUTE** | **OUT** | — | — | — | — |

```
┌────────────────────────────┐
│ FX · EFFECT          1/2   │
│ FX 1                       │
├────────────────────────────┤
│    P1    P2   MIX   ----   │
│   048   100   127          │
│                            │
│   EFFECT   COMB            │
│   IN  ◄ V1 V2              │
└────────────────────────────┘
```

**There is no input control, and that is the point.** `IN ◄ V1 V2` is read
only — derived by asking every other node where it is pointing. You never set
an input anywhere; routing *to* a channel is what fills it.

P1 and P2 are relabelled per effect (RATE/DEPTH for chorus, DRIVE/BIAS for
fold, WIDTH/— for width, CUTOFF/RES for the filters), from the same table that
supplies the CC number.

Deliberately no reverb and no delay. The output goes to a DAW over USB audio
where both are free and better; the RAM buys tuned resonance and a patchbay
instead.

### MIX — Mixer *(Subject: channel 1–4)*

**One page. All four channels visible at once.** A mixer spread across four
pages is not a mixer — the entire value of the layout is seeing the balance,
and you cannot balance what you cannot see together.

```
┌────────────────────────────┐
│ MIX                  1/1   │
│ CH 2                       │
├────────────────────────────┤
│   ◞      ┏━━━━┓            │
│          ┃ ◝  ┃   ◜     ◉  │  PAN
│  ·│      ┃·│  ┃  ·│    ·│  │  9  ─ 0 dB   blue
│  ·│      ┃▪█  ┃  ·│    ·│  │  8  ─3
│  ·│      ┃▪█  ┃  ·│    ·│  │  7  ─6
│  ·█      ┃▪│  ┃  ·│    ·│  │  6  ─9
│  ▪█      ┃▪│  ┃  ·│    ·│  │  5 ─12   green
│  ▪│      ┃▪│  ┃  ·█    ·│  │  4
│  ▪│      ┃▪│  ┃  ▪█    ·│  │  3
│  ▪│      ┃▪│  ┃  ▪│    ▪█  │  2
│  ▪│      ┃▪│  ┃  ▪│    ▪█  │  1 ─36
│          ┗━━━━┛            │
│   1        2       3    4  │
│  FX1      FX1     ---  OUT │  where the channel's signal goes
├────────────────────────────┤
│  VOL   PAN   MONO   ----   │  the four knobs, mapped to CH 2
│  104    C     OFF          │
└────────────────────────────┘

        ▪ meter segment    █ fader cap    │ fader track
```

Both run **vertically**, like the hardware they are imitating. A horizontal
bar reads as a spectrum analyser, which is a different instrument telling you
a different thing.

**SELECT chooses the channel and the four knobs follow it.** The selected
strip is boxed in red, and the box *is* the mapping — it says "these knobs
belong to this strip" without a word of explanation. That is the same SELECT
rule as everywhere else in the device: it picks the Subject, never a value.

| KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- |
| VOLUME | PAN | MONO | **DRIVE** |

**DRIVE** is a cubic soft saturator on the channel, after the EQ. It is a
different curve from the master limiter on purpose: a limiter must be
transparent until it is needed, so it stays flat until the signal nears the
rail; a drive stage should be doing something at every level, so this one is
gently nonlinear from zero. That is what makes it read as gain rather than as
a gate opening onto distortion.

Odd symmetric, so it adds third and fifth but no second harmonic and no DC —
the warm direction rather than the fuzzy one, and clean enough to go straight
into a DAW.

The mud-clearing high-pass that was the other candidate for this knob now
lives on the EQ page, where the LOW band's pass mode does it properly with a
frequency and a resonance.

**MONO** collapses the channel to centre regardless of PAN — a
mono-compatibility check you can hit while the DAW is recording, and what the
Haas work will need once a voice can be genuinely stereo.

#### The strips are terminal

**Mixer channels always go to main.** They are the end of the signal path, so
there is no destination control here and no routing page in this menu. The
fader is the last gain stage before the master.

A strip still *shows* where its signal goes, under the channel number, because
you want that visible while balancing — but it is read-only here. Routing is
set where the routed thing lives.

#### Metering

Nine segments per channel, lit from the bottom, in the DJ-mixer idiom:

| Segments | Colour | Range |
| --- | --- | --- |
| 1–5 | **green** | −36 dB … −12 dB |
| 6–9 | **blue** | −9 dB … 0 dB |
| all nine | **red** | the channel reached full scale |

**Red is the whole ladder, not the top segment.** An overload should be
readable from across a room, not found by inspecting one pixel. It holds for
about half a second, because a clip lasting one block is 5.8 ms and an
overload nobody can see is the same as one that did not happen.

The ladder is roughly logarithmic — 6 dB per step up to −12, then 3 dB — so
the resolution sits where the decisions are. A linear ladder puts seven of the
nine segments below −18 dB, where nothing worth seeing happens. Thresholds are
`FM1_METER_STEPS` in `dsp/engine/fm1_params.h`, defined once so the UI and the
engine cannot disagree.

The meter is **post-fader**: it shows what the channel actually contributes.
Pull a fader down and it must read quiet, or it is lying about the balance,
which is its one job. It is taken after F2 and before pan, so panning moves a
voice without changing what its meter says.

Peak with a slow fall. A drone that sits steadily hot and one that peaks once
look identical on a fast meter, and here almost everything is the first kind.

> **What red actually means.** The clip flag fires when a channel's own path
> reaches full scale — not when the mix bus does. The bus carries two bits of
> headroom and limits once at the end (§4 of `SIGNAL_PATH.md`), so a red
> channel is a warning that *this* channel has nothing left, which is
> actionable, rather than a report that the output is distorting, which is
> not. Reaching it takes real effort: the comb normalises its own resonant
> gain, so it takes two resonant filters in series to get there.

### Routing lives on the last page of its own menu

There is no patchbay menu, and no routing page on the mixer. Each thing routes
from the menu that owns it:

| Menu | Routing page | Sets |
| --- | --- | --- |
| **EDIT** | last page | the selected voice's destination |
| **FX** | page 2 | the selected effect channel's destination |

One page showing every cable in the instrument reads as a wiring diagram, not
a control surface. Routing is a property of a thing, so it sits with that
thing.

Each destination knob cycles `--- · FX1 · FX2 · FX3 · FX4 · OUT`.


**Four rules, and they are the whole model:**

1. **One cable out per node.** Voices and effect channels route identically.
2. **No input control anywhere.** Whatever points at a channel sums at it.
3. **`---` means unplugged.** Set a channel's OUT to `---` and everything
   feeding it goes silent. There is no separate mute.
4. **Any cable is legal**, including FX2 → FX1 and FX1 → FX1.

#### Feedback, and the one thing the screen must say

A cable pointing **left** — back to a lower-numbered channel, or to itself —
is a feedback loop, and costs **one sample**. That is 22 µs and inaudible as
colouration; it is how VCV Rack behaves for the same reason.

The screen marks those cables, because an invisible one-sample delay is
exactly the kind of thing that produces a complaint about "weird phasing" six
months later:

```
│   FX2 ◄─╮  feedback              │
│   FX1 ──╯  +1 sample             │
```

A loop with gain ≥ 1 will run away. It saturates into the soft clip and
screams rather than wrapping to noise — which is what feedback is *supposed*
to do on a drone box, and is why the accumulators are clamped rather than the
routing restricted.

> **Why not restrict it?** An earlier design only allowed cables pointing
> right, to avoid feedback entirely. That is a fine rule for a mixing desk and
> the wrong one for an instrument: refusing to let someone patch a comb into
> an overdrive and back is not safety, it is a missing feature. The real
> hazard was never the loop, it was the *block-sized* delay an earlier
> implementation would have given it — 5.8 ms, a slapback echo at 172 Hz.
> Evaluating per sample removed the hazard, so the restriction had nothing
> left to justify it.

### ENV — Modulation *(Subject: voice 1–4)*

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| 1 **MOD A** | → PITCH | → TIMBRE | → COLOR | → LEVEL |
| 2 **MOD B** | → F1 CUT | → F1 RES | → F2 CUT | → F2 RES |
| 3 **MOD C** | → PAN | — | — | — |

A knob sets the **depth** into that destination, bipolar. **SHIFT + knob**
selects the **source** for that destination: LFO1–4, or none. So each page
shows four of the nine modulatable inputs, with its source and depth visible
together — the routing *is* the page, rather than a matrix to navigate.

Folding the comb into the filter is what keeps this list short. COMB TUNE / FB
/ MIX were three destinations of their own; they are now the filter's CUTOFF,
RES and MIX, which already needed to be modulatable. Nine destinations rather
than twelve.

### LFO — Modulators *(Subject: LFO 1–4)*

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 | ALGORITHM |
| --- | --- | --- | --- | --- | --- |
| 1 **SHAPE** | RATE | AMOUNT | PHASE | — | mode |

One LFO per page, selected with SELECT. Modes: TRIANGLE, SAW, RAMP, SQUARE,
SAMPLE&HOLD, SMOOTH RANDOM. S&H is a mode rather than a separate module —
same cost, one less concept, and you can have four of them.

LFOs are **global**, shared by all voices, because per-voice independence
already exists: the drift is per-voice and free-running. Shared LFOs give the
opposite and more useful thing — coherent movement across all four voices.

### GLO — Global *(no Subject)*

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| 1 **OUTPUT** | MASTER | SPREAD | — | — |
| 2 **TUNE** | TRANSPOSE | FINE | — | — |

Stereo width is **not** here. It is an effect type on an FX channel, so it can
be placed anywhere in the patch — on one voice's send, or across the whole
mix — rather than being a single global setting at the end of the chain.

### SAVE — Patch *(no Subject)*

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 | PRESETS |
| --- | --- | --- | --- | --- | --- |
| 1 **PATCH** | — | — | — | — | slot |

### HOME — Overview *(no Subject)*

Not editable. All four voices at once: note, shape, level, mute/solo state —
the page you return to to see the whole instrument.

---

## 6. Screen layout

240×240, fixed zones. Nothing outside its zone.

```
┌────────────────────────────┐
│ EDIT · BRAIDS        1/2   │  20px  menu · page, page N/M
│ VOICE 1                    │  18px  subject (blank if none)
├────────────────────────────┤
│                            │
│   TIMBR COLR   FM   MOD    │  one row of four labels
│    064   064  +16  ----    │  one row of four values
│                            │  124px free for page content:
│   SHAPE  09 TRIPLE SAW     │  enum, meters, routing
│                            │
├────────────────────────────┤
│ ▚▚▚▚▚▚▚▚ keys ▚▚▚▚▚▚▚▚▚▚▚ │  30px  keyboard, lit = sounding
└────────────────────────────┘
```

* The four labels sit **over** their values, physically under their knobs.
* A value being edited inverts for 400 ms, so you can see which knob moved.
* `----` means the knob is inactive on this page.
* Bipolar values show a sign; unipolar never does.

## 7. LED conventions

| LED | Meaning |
| --- | --- |
| Menu button | lit = this menu is open; **blinking = more pages behind it** |
| SEL (SHIFT) | lit while held |
| PLAY | lit = Subject voice sounding; blinking = soloed |
| key LEDs | lit = a voice is sounding that note |
| OCT− / OCT+ | lit when the voice can still go that way (not at a limit) |

The blinking menu button is the only hint that a menu has more than one page,
so it is not decoration — without it, pages beyond the first are invisible.

---

## 8. Keys

| | |
| --- | --- |
| key | set the Subject voice's note, and latch it on |
| SHIFT + key | set the note **without** un-muting — audition routing silently |

A note-off never stops a voice. This is a drone machine: a note is a decision
to sound, not a gesture held down. Voices stop with PLAY, or MIDI CC 123.

---

## 9. Consequences for MIDI

Because every Parameter is declared once in a table carrying its label,
range, default and CC number, these stay in step by construction:

* the knob that edits it,
* the label on the screen,
* its MIDI CC,
* its byte in the saved patch.

Voice Subjects map to MIDI channels 1–4, LFO Subjects and Global to channel 16
(`docs/MIDI_SPEC.md`).

---

## 10. Open decisions

1. **SHIFT placement** — SEL proposed; HOME and FX are the alternatives.
   Decide after playing, not before.
2. **ARP now opens ROUTE**, because the patchbay needed a button and a drone
   machine does not need an arpeggiator. The label lies, which breaks the rule
   that a printed button does the printed thing — accepted knowingly, since
   the alternative is burying the routing inside GLO where it would be a
   sub-menu rather than a place. **SEQ** stays unassigned.
3. **Haas widening** is specified but not built. When it lands a voice becomes
   genuinely stereo, which is what the MONO switch on MIX page 3 is for —
   mono-compatibility has to be checkable from the panel before it ships.
4. **Patch slots** — how many fit in the 256-byte settings sector. One full
   patch is ~149 bytes, so the sector holds exactly one. Multiple slots need
   a second flash sector or a smaller patch format.
