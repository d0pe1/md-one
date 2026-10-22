# What TIMBRE and COLOR do, on each of the 48 shapes

Braids puts two knobs in front of forty-eight oscillators. They mean something
different on every one, which is the single largest thing a player has to hold
in their head — and the one thing the panel cannot tell them.

This document is the reference, and `FM1_SHAPE_TIMBRE[]` / `FM1_SHAPE_COLOR[]`
in `dsp/engine/fm1_shapes.h` is the same information in six characters, put on
the screen over the knob.

## Where this comes from, and why not the manual

Everything below was read out of the **vendored source**, not out of Mutable's
manual:

| File | What it decides |
| --- | --- |
| `dsp/mi/braids/macro_oscillator.cc` | shapes 0–12 and 15, and the dispatch for the rest |
| `dsp/mi/braids/analog_oscillator.cc` | what `parameter_` / `aux_parameter_` mean to each analogue waveform |
| `dsp/mi/braids/digital_oscillator.cc` | shapes 13, 14, 16–47 |

The manual describes *a* Braids. This describes *our port* of it, and it cannot
drift from what ships without the file changing underneath it. That distinction
earned its keep immediately — see "What this turned up" at the bottom.

## How a knob reaches the oscillator

```
knob 0..127  ──<<8──▶  0..32512  ──▶ parameter_[0]  (TIMBRE)
knob 0..127  ──<<8──▶  0..32512  ──▶ parameter_[1]  (COLOR)
```

`dsp/engine/fm1_drone.cc:548-559`. Modulation is added before the clamp, so a
routed LFO covers the same range the knob does. Note the top: a knob at 127
gives 32512, not 32767 — on shapes with a threshold at 32000 (STRUCK BELL,
STRUCK DRUM) the last few knob values still cross it, which is what makes the
infinite-sustain bell reachable.

Three shapes quantise `parameter_[1]` on the way in: FM, FEEDBACK FM and
CHAOTIC FM run it through `lut_fm_frequency_quantizer`
(`digital_oscillator.cc:99-107`) so the ratio lands on musical intervals.

---

## The shapes

Screen labels in brackets: **[TIMBRE / COLOR]**.

### ANALOG — 0–4

**0 CSAW** — **[PW / DEPTH]**
A saw with a step cut into it. TIMBRE sets where the step falls
(`pw = parameter_ * 49152`, `analog_oscillator.cc:104`); COLOR sets how deep it
is (`discontinuity_depth_ = -2048 + (aux_parameter_ >> 2)`, line 123). COLOR
also trims a compensating DC offset in the macro layer
(`macro_oscillator.cc:59`), so the two together sweep from a plain saw to a
hollow, pulse-like tone.

**1 MORPH** — **[WAVE / FUZZ]**
TIMBRE is a waveform morph in three zones (`macro_oscillator.cc:74-92`):
triangle → saw, then saw → square, then square narrowing to a thin pulse.
COLOR does two things at once and is honest about it: it lowers a low-pass
cutoff *and* raises the drive into `ws_violent_overdrive` (lines 99–107). Up is
darker and dirtier together.

**2 SAW SQUARE** — **[PW / MIX]**
TIMBRE is the pulse width of *both* oscillators — the variable saw and the
square share one value (lines 131–132). COLOR crossfades between them, with the
square attenuated to 148/256 so the blend stays level.

**3 SINE TRIANGLE** — **[FOLD / MIX]**
Two wavefolders. TIMBRE is the fold amount, driving gain from ×1 to about ×16
(`analog_oscillator.cc:492`), and it is **attenuated as pitch rises** — above
MIDI 92 for the sine, above 80 for the triangle (`macro_oscillator.cc:293-298`)
— so high notes fold less than low ones at the same knob position. COLOR
crossfades sine-fold against triangle-fold.

**4 BUZZ** — **[HARM / DETUNE]**
A band-limited impulse train. TIMBRE picks the comb zone: the parameter is
*subtracted* from the lookup pitch (`shifted_pitch = pitch_ + ((32767 -
parameter_) >> 1)`, `analog_oscillator.cc:568`), so **up is more harmonics and
brighter**. COLOR detunes a second buzz against the first by up to one semitone
(`parameter_[1] >> 8`), which is where the shimmer comes from.

### SUB SYNC — 5–8

**5 SQUARE SUB / 6 SAW SUB** — **[PW / SUB]**
TIMBRE is the pulse width of the main oscillator — square on 5, variable saw on
6. COLOR is a **bipolar sub-oscillator mixer with a dead centre**
(`macro_oscillator.cc:236-250`): at 64 there is no sub at all; turn down and a
square two octaves below fades in; turn up and it is one octave below instead.

**7 SQUARE SYNC / 8 SAW SYNC** — **[SYNC / MIX]**
TIMBRE is the slave's pitch offset above the master, up to about 64 semitones
(`pitch_ + (parameter_[0] >> 2)`). This is the sync sweep. COLOR crossfades
master against slave, so at the bottom you hear none of the sync at all.

### TRIPLE — 9–12 (chord generators)

**9 TRIPLE SAW / 10 TRIPLE SQUARE / 11 TRIPLE TRIANGLE / 12 TRIPLE SINE**
— **[NOTE 2 / NOTE 3]**
Three detuned oscillators of one waveform. The two knobs are the **intervals of
the second and third**, read from a 65-entry table spanning −24 to +24
semitones (`macro_oscillator.cc:158-176`). Near the centre the table switches
to fine steps of ±4/128 semitone, so the middle of the knob is a detune control
and the ends are a chord selector.

These **step**: half of each knob step is a plateau and half glides to the next
entry (lines 204–207). Shown as `STEPPED  CHORD` on the shape page.

### SWARM — 13–16

**13 TRIPLE RING MOD** — **[MOD 1 / MOD 2]**
A carrier sine ring-modulated twice. Both knobs are plain continuous pitch
offsets for the two modulators, ±32 semitones
(`pitch_ + ((parameter_[n] - 16384) >> 2)`). Nothing here is quantised and
nothing is a chord, despite the name's company.

**14 SAW SWARM** — **[DETUNE / HPF]**
Seven saws. TIMBRE is the detune spread, squared so the bottom of the knob is
fine (`detune = (p + 1024)² >> 9`). COLOR is a high-pass cutoff tracked
relative to the note, with a steeper slope below centre than above (lines
186–191) — so it thins the swarm out from underneath.

**15 SAW COMB** — **[TUNE / RESO]**
A saw into a comb filter. TIMBRE tunes the comb ±64 semitones against the note;
COLOR is its feedback, warped through `ws_moderate_overdrive` so the extremes
are finer than the middle (`digital_oscillator.cc:263-264`).

**16 TOY** — **[RATE / BITS]**
Deliberate lo-fi. TIMBRE is sample-rate reduction (`decimation_count = 512 -
(p >> 6)`); COLOR is a bit-mangling mask applied to the phase
(`((phase >> 24) ^ (x << 1)) & ~x`), which is a crusher that also changes the
waveform's shape rather than merely its resolution.

### FILTER — 17–20

**17 DIGITAL FILT LP / 18 PK / 19 BP / 20 HP** — **[FREQ / SHAPE]**
Not filters — oscillators that *sound* like a filtered waveform, built from a
carrier swept against a windowed saw. TIMBRE is the filter/formant frequency
(`pitch_ + ((parameter_[0] - 2048) >> 1)`). COLOR crosses a threshold at its
midpoint: below it the window is a **saw**, above it a **triangle**
(`digital_oscillator.cc:378`), and the blend between the windowed carrier and
the pulse peaks at the centre and falls off to both sides (line 400). So the
middle of COLOR is the richest setting and both ends are the plainest — the one
knob on this shape that is not monotonic.

### VOWEL — 21–23

**21 VOSIM** — **[FORM 1 / FORM 2]**
Two formant frequencies, straight: each knob is a pitch for one of the two
formant sines (`ComputePhaseIncrement(parameter_[i] >> 1)`).

**22 VOWEL** — **[VOWEL / SHIFT]**
TIMBRE splits: the **top 4 bits select one of nine vowels, the low 12 blend to
the next** (`digital_oscillator.cc:473-474`). It does not step — the blend is
continuous — but it is a knob with two meanings at two scales, which is worth
knowing before you modulate it. COLOR shifts all formants together
(`200 + (parameter_[1] >> 6)`), which is the "size of the head" control.

**23 VOWEL FOF** — **[VOWEL / SINGER]**
A bank of five SVFs over a 5×5 table. TIMBRE is the vowel axis (a/e/i/o/u);
COLOR is the **singer** axis — bass, tenor, countertenor, alto, soprano
(`formant_f_data`, lines 534–575). Both interpolate bilinearly, so both are
smooth.

### HARMONIC — 24–27

**24 HARMONICS** — **[PEAK / WIDTH]**
Twelve additive partials. TIMBRE slides **which partial is loudest**
(`peak = 12 * p >> 7`). COLOR does two things on one knob: it raises a second
peak half an octave up (`p² >> 15`, monotonic) *and* sets the width of both
skirts, which is **widest at the centre and narrows towards both ends**
(lines 936–939). Neither knob steps.

**25 FM** — **[INDEX / RATIO]**
Two operators. TIMBRE is the modulation index; COLOR is the modulator's
frequency ratio, **quantised** on the way in so it lands on musical intervals.

**26 FEEDBACK FM** — **[INDEX / RATIO]**
As above, with the output fed back into the modulator. The index is
additionally **attenuated as the note rises** above MIDI 72
(`digital_oscillator.cc:756-759`), so the same knob is tamer up top — a
deliberate anti-aliasing measure, not a bug.

**27 CHAOTIC FM** — **[INDEX / RATIO]**
As 25, except the modulator's own increment is modulated by the output
(line 814). Small ratio changes have large and non-monotonic consequences; the
label says RATIO because that is what it sets, not because the result is
predictable.

### PHYSICAL — 28–31

**28 PLUCKED** — **[DECAY / POSN]**
Karplus–Strong, three voices. TIMBRE is **two controls either side of centre**
(lines 1125–1136): below the midpoint it is damping, from dead-short to
sustained; above it, damping is zero and the knob instead lowers the update
probability, stretching the decay out well past natural. COLOR sets how much of
the delay line is filled with noise on the pluck — the pluck position, and so
the brightness of the attack.

**29 BOWED** — **[BOW / POSN]**
A waveguide string. TIMBRE is bow force, **inverted**: `parameter_0 = 172 -
(p >> 8)`, so turning the knob up reduces friction. COLOR is bow position along
the string (`6 + (p >> 9)`), which sets the ratio of bridge to neck delay —
towards the bridge is thinner and more nasal.

**30 BLOWN** — **[BREATH / BODY]**
A clarinet-like bore. TIMBRE is breath noise, also **inverted**
(`28000 - (p >> 1)`) — up is *less* noise and a purer tone. COLOR shifts the
body filter's tracking by up to half an octave, which is the bore's brightness.

**31 FLUTED** — **[BREATH / JET]**
TIMBRE is breath turbulence, inverted again (`2100 - (p >> 4)`). COLOR sets the
jet delay as a fraction of the bore (`48 + (p >> 10)`) — the embouchure. This
is the knob that decides whether it overblows.

### STRUCK — 32–36

**32 STRUCK BELL** — **[DECAY / DETUNE]**
Eleven inharmonic partials. TIMBRE is decay, and **above 32000 the decay loop
is skipped entirely** (`digital_oscillator.cc:894`) — the bell rings forever.
With the knob at 127 feeding 32512, that threshold is reachable, which makes
this one of the few genuinely useful drone shapes in this category. COLOR
spreads the partials apart, alternating up and down by `p >> 7`.

**33 STRUCK DRUM** — **[DECAY / TONE]**
Six partials plus filtered noise. TIMBRE is decay with the same
infinite-sustain threshold at 32000. COLOR is one knob over three things: the
noise filter's cutoff, the harmonics' gain (rising to 12888 then flat), and a
crossfade between two noise modes above the midpoint.

**34 KICK** — **[DECAY / TONE]**
A resonator excited by pulses. TIMBRE sets the resonator's Q — the body decay,
cubed so the top of the knob is where it gets long. COLOR is a low-pass
coefficient raised to the fourth power, so it stays dark for most of its travel
and opens quickly at the end.

**35 CYMBAL** — **[FREQ / MIX]**
Six detuned square phases through a band-pass and a high-pass. TIMBRE sets both
filters' frequency together. COLOR crossfades the metallic six-oscillator noise
against plain white noise.

**36 SNARE** — **[TONE / SNAP]**
Two resonators plus a noise burst. TIMBRE tilts gain between the two drum modes
(`22000 ∓ (p >> 1)`) — low versus high body. COLOR sets both the resonators'
decay *and* the snare rattle's amplitude, clamped at 14336 so the top third of
the knob changes only the decay.

### WAVETABLE — 37–40

**37 WAVETABLES** — **[WAVE / BANK]**
TIMBRE scans continuously through the current bank, crossfading between
adjacent waves. COLOR picks one of **20 banks** — male, female, choir, tampura,
organ, two drone banks, metallic, bell, and so on (`wavetable_definitions`,
lines 1471–1523) — with 64 counts of hysteresis so it cannot flicker. This one
genuinely steps.

**38 WAVE MAP** — **[MAP X / MAP Y]**
A 16×16 grid of waves, bilinearly interpolated. Both knobs are coordinates and
both are smooth.

**39 WAVE LINE** — **[SCAN / ROUGH]**
TIMBRE scans a 64-wave line, with its own smoothing filter
(`smoothed_parameter_`). COLOR is a roughness control in four zones
(lines 1649–1728): it progressively masks low bits off the phase, so the wave
goes from smooth, through a stepped reading of itself, to coarse quantisation.

**40 WAVE PARAPHONIC** — **[WAVE / CHORD]**
Four oscillators sharing one wave. TIMBRE picks the wave from a 32-entry line;
COLOR picks from a **17-entry chord table** (lines 1735–1753) — unisons,
octaves, minor and major triads, sixths, sevenths, fifths-and-octaves. The
crossfade between chords has a dead zone either side (lines 1779–1785), so it
steps. This is the fourth chord generator, and the only one outside the TRIPLE
group.

### NOISE — 41–45

**41 FILTERED NOISE** — **[RESO / MODE]**
Noise into an SVF whose cutoff is the *note*. TIMBRE is resonance. COLOR morphs
the output LP → BP → HP across its travel.

**42 TWIN PEAKS** — **[RESO / PEAK 2]**
Two resonators on noise. TIMBRE is their shared Q (and compensating makeup
gain, so the level stays sane as it rings). COLOR is the **second peak's
frequency** relative to the first, ±64 semitones — the first tracks the note.

**43 CLOCKED NOISE** — **[LOOP / STEPS]**
A sample-and-hold with a repeating seed. TIMBRE sets the **loop length** — how
often the generator reseeds, which is what turns noise into a pattern. COLOR
quantises each sample into `1 + (p >> 10)` levels, so it steps in 32 positions.

**44 GRANULAR CLOUD** — **[GRAIN / SPREAD]**
Four sine grains. TIMBRE is the grain duration via
`lut_granular_envelope_rate`; COLOR is the random pitch spread applied to each
grain as it is born.

**45 PARTICLE NOISE** — **[DENSTY / SPREAD]**
Random impulses through three resonators. TIMBRE is firing density
(`1024 + p`); COLOR is how far the three resonators' pitches scatter on each
impulse. At zero spread it is a fixed chord being struck at random; at full
spread it is rain.

### MISC — 46–47

**46 DIGITAL MOD** — **[RATE / DATA]**
QPSK. TIMBRE is the symbol rate; COLOR is the **data byte** fed into the
modulator, low-pass filtered so it changes smoothly. It is a modem, and it
sounds like one.

**47 QUESTION MARK** — **[SPEED / NOISE]**
Morse code with a shortwave receiver's noise floor. TIMBRE is the dit duration,
inverted (`3600 + ((32767 - p) >> 2)`). COLOR raises the noise floor *and*
mixes in squared-signal distortion (lines 2252–2296).

---

## What this turned up

**The STEPPED and CHORD badges were wrong on four shapes out of twelve.** They
had been written per *category*, which is a convenient grouping and not a fact
about the renderers:

| Shape | Was | Is |
| --- | --- | --- |
| 13 TRIPLE RING MOD | `STEPPED CHORD` | neither — two continuous pitch offsets |
| 24 HARMONICS | `STEPPED` | neither knob steps |
| 38 WAVE MAP | `STEPPED` | bilinear, smooth |
| 39 WAVE LINE | `STEPPED` | crossfaded, smooth |
| 40 WAVE PARAPHONIC | nothing | `STEPPED CHORD` — a 17-entry chord table |

A badge saying STEPPED on a knob that glides is the same failure as a knob
labelled CUTOFF that sets a delay ratio: it teaches the player something untrue
and they find out by not trusting the screen. Fixed in `fm1_shapes.h`, with the
predicates now per knob (`fm1_shape_timbre_stepped` /
`fm1_shape_color_stepped`) because on FM only the ratio steps, and tested in
`dsp/tests/test_pages.c:test_shape_knob_labels`.

**Upstream's digital shape enum does not match its own dispatch table.** In
`digital_oscillator.h` the constants from index 15 onward read STRUCK_BELL,
STRUCK_DRUM, KICK, HAT, SNARE, PLUCKED, BOWED, BLOWN, FLUTED; in
`digital_oscillator.cc` the function table at those indices is Plucked, Bowed,
Blown, Fluted, StruckBell, StruckDrum, Kick, Cymbal, Snare. The two groups are
permuted against each other.

**This is inert, and was checked rather than assumed.**
`MacroOscillator::RenderDigital` dispatches by arithmetic on the macro index,
never by name, and nothing inside `digital_oscillator.cc` compares `shape_`
against any constant in the 15–23 range (verified by grep). Both orderings
contain the same nine entries, so index 24 onward realigns. Selecting PLUCKED
does render a plucked string. It is recorded here only so that the next person
who reads that enum does not spend an afternoon on it, and so that nobody adds
a `shape_ == OSC_SHAPE_PLUCKED` test to that file, which would silently do the
wrong thing.

## Still open

* These are *derived*, not *heard*. Every entry follows from a line of code,
  but the only ones listened to on hardware are the handful in the boot patch.
  A knob labelled WIDTH that is really width-and-second-peak is correct and
  still incomplete; a pass with the device in hand would catch any that are
  technically right and practically misleading.
* The split knobs — VOWEL's TIMBRE, PLUCKED's TIMBRE, SNARE's COLOR, DIGITAL
  FILTER's COLOR — have one label for two behaviours. A value readout that
  showed *which half you are in* would fix that, and is the same work as the
  units pass (`072` → `1.0kHz`) already queued in `ROADMAP.md`.
