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
| **Subject** | What the current Menu acts on — voice 1–4, or LFO 1–4. Chosen by the SELECT encoder. Global menus have no Subject. |

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
| **FX** | Filter/Comb menu → next page | page 1 |
| **SEL** | **SHIFT** (dedicated modifier) | — |
| **ENV** | Modulation menu → next page | page 1 |
| **LFO** | LFO menu → next page | page 1 |
| **EDIT** | Oscillator menu → next page | page 1 |
| **GLO** | Global menu → next page | page 1 |
| **HOME** | Overview page | — |
| **SAVE** | Patch menu | save to current slot |
| **ARP** | *(reserved)* | — |
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

### FX — Filter / Comb *(Subject: voice 1–4)*

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| 1 **COMB** | TUNE | FEEDBACK | MIX | — |

Deliberately no reverb. The output goes to a DAW over USB audio where space
is free and better; the pool buys tuned resonance instead
(`docs/BRAIDS_PORT.md`).

### ENV — Modulation *(Subject: voice 1–4)*

| Page | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| 1 **MOD A** | → PITCH | → TIMBRE | → COLOR | → COMB TUNE |
| 2 **MOD B** | → COMB FB | → COMB MIX | → LEVEL | → PAN |

A knob sets the **depth** into that destination, bipolar. **SHIFT + knob**
selects the **source** for that destination: LFO1–4, or none. So each page
shows four of the eight modulatable inputs, with its source and depth visible
together — the routing *is* the page, rather than a matrix to navigate.

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
| 1 **MIX** | MASTER | SPREAD | WIDTH | — |
| 2 **TUNE** | TRANSPOSE | FINE | — | — |

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
2. **ARP and SEQ** are unassigned. A drone machine arguably wants neither;
   they are good candidates for a future "evolve" or "freeze" function.
3. **WIDTH on GLO/MIX** — Haas widening is specified but not built, and
   mono-compatibility needs checking before it ships.
4. **Patch slots** — how many fit in the 256-byte settings sector. One full
   patch is ~149 bytes, so the sector holds exactly one. Multiple slots need
   a second flash sector or a smaller patch format.
