# MIDI specification — proposal

Draft for the drone machine. Designed around one goal: **everything that makes
a sound should be automatable from an Ableton track**, with each voice on its
own track.

Modelled on obervave's `obv_params.h` (`docs/PLATFORM_SURVEY.md`): one table
drives the screen label, the range, the default, the display formatting *and*
the CC number. Nothing is addressable in two places.

> Status: proposal, not implemented. Numbers are deliberate but cheap to
> change while nothing depends on them.

## Channel layout

| Channel | Role |
| --- | --- |
| **1–4** | voices 1–4: notes, and that voice's own parameters |
| **16** | global: master, unison, width, the modulation engine |
| 5–15 | unused, reserved |

Voice-per-channel rather than one channel with offsets, because in Ableton each
voice then becomes an ordinary MIDI track with its own clip, envelopes and
automation lanes. No parameter maths in the DAW.

## Notes

| Message | Effect |
| --- | --- |
| Note on | set the voice's pitch, **call `Strike()`**, open the gate |
| Note off | close the gate (the voice keeps sounding if LATCH is on) |
| CC 64 (sustain) | LATCH: notes stay on after note-off — the drone default |
| Pitch bend | ±2 semitones, or the BEND range |
| Channel pressure | assignable modulation source |

`Strike()` matters: **14 of Braids' 34 render paths consume a strike flag** and
are silent or static without one — `Plucked`, `StruckBell`, `StruckDrum`,
`Kick`, `Snare`, `Bowed`, `Blown`, `Fluted`, `Vowel`, `SawSwarm`,
`ClockedNoise`, `DigitalModulation`, `WaveParaphonic`, `QuestionMark`.
Note-on is the natural trigger, and for the sustained models a re-strike is
a re-bow rather than a new note.

Velocity is **not** routed to amplitude by default. For drones a note is a
decision to sound, not a hit; velocity is offered as a modulation source
instead.

## Per-voice parameters (channels 1–4)

Standard CCs are pinned where a convention exists, so generic controllers and
Ableton's own mappings do the expected thing. Everything else is assigned from
CC 20 upward in table order, exactly as obervave does.

| CC | Name | Range | Default | Notes |
| --- | --- | --- | --- | --- |
| 1 | MODWHL | 0–127 | 0 | modulation depth, destination per MOD DEST |
| 7 | LEVEL | 0–127 | 100 | voice level |
| 10 | PAN | 0–127 | 64 | stereo position |
| 71 | RESO | 0–127 | 0 | SVF resonance |
| 74 | CUTOFF | 0–127 | 127 | SVF cutoff — the standard brightness CC |
| 20 | SHAPE | 0–47 | 0 | Braids macro oscillator shape |
| 21 | TIMBRE | 0–127 | 64 | Braids TIMBRE |
| 22 | COLOR | 0–127 | 64 | Braids COLOR |
| 23 | OCTAVE | 0–8 | 4 | transpose |
| 24 | DETUNE | 0–127 | 64 | fine, bipolar, ±50 cents |
| 25 | GLIDE | 0–127 | 0 | portamento time |
| 26 | DRIFT RATE | 0–127 | 20 | slow random pitch/timbre drift |
| 27 | DRIFT DEPTH | 0–127 | 0 | how far it drifts |
| 28 | COMB TUNE | 0–127 | 64 | comb delay as a ratio of the note |
| 29 | COMB FB | 0–127 | 0 | comb feedback — resonance/ring |
| 30 | COMB MIX | 0–127 | 0 | dry/comb balance |
| 31 | STRIKE | 0–127 | — | re-strike without a new note (any value) |
| 85 | MOD DEST | 0–7 | 0 | what MODWHL moves: timbre, color, cutoff, comb… |

That is 18 per voice, 72 addressable parameters across four voices, with CC
space to roughly triple it.

## Global parameters (channel 16)

| CC | Name | Range | Default | Notes |
| --- | --- | --- | --- | --- |
| 7 | MASTER | 0–127 | 100 | overall level |
| 20 | UNISON | 0–4 | 0 | voices stacked on one note |
| 21 | SPREAD | 0–127 | 0 | unison detune spread |
| 22 | BINAURAL | 0–127 | 0 | L/R beat rate, 0–12 Hz — headphones only |
| 23 | HAAS | 0–127 | 0 | stereo width via a 0–35 ms channel delay |
| 24 | DRIFT SYNC | 0–1 | 0 | voices drift together or independently |
| 25 | KEY | 0–11 | 0 | root for the quantiser |
| 26 | SCALE | 0–15 | 0 | scale for the quantiser |
| 27 | QUANTISE | 0–1 | 0 | snap incoming notes to the scale |
| 120 | ALL SOUND OFF | — | — | standard |
| 123 | ALL NOTES OFF | — | — | standard; also clears LATCH |

Deliberately **no reverb or delay mix**. The instrument's output goes straight
into Ableton over USB audio, where space is better and free. The pool buys
tuned combs and resonators instead — see `docs/BRAIDS_PORT.md`.

## The parameter table

One table, as obervave does it, so the CC map, the UI and the patch format
cannot drift apart:

```c
typedef struct {
    const char *name;   /* 8 characters at most -- the screen label */
    uint8_t max;        /* inclusive */
    uint8_t def;
    uint8_t kind;       /* how to display it: K_NUM, K_BIP, K_ONOFF, K_SHAPE... */
    uint8_t cc;         /* fixed CC, or 0 to take the next from 20 upward */
} pdef_t;

static const pdef_t PDEF[NPARAM] = {
    [P_SHAPE]  = {"SHAPE",  47, 0,   K_SHAPE, 0},
    [P_TIMBRE] = {"TIMBRE", 127, 64, K_NUM,   0},
    [P_CUTOFF] = {"CUTOFF", 127, 127, K_CUT,  74},   /* pinned standard */
    ...
};

static uint8_t cc_map[128];   /* CC -> parameter + 1, 0 = unassigned */
```

Receiving is then a few lines, which is all Dinghy is missing — it parses only
`0x90`/`0x80` today:

```c
while (midi_get(&st, &d1, &d2)) {
    uint8_t ch = st & 0x0F, msg = st & 0xF0;
    if (msg == 0xB0) {                       /* control change */
        uint8_t p = cc_map[d1 & 0x7F];
        if (p) set_param(ch, p - 1, d2);
    } else if (msg == 0x90 && d2) {
        voice_note_on(ch, d1, d2);           /* pitch + Strike() */
    } else if (msg == 0x80 || msg == 0x90) {
        voice_note_off(ch, d1);
    }
}
```

## Resolution

7-bit CC gives 128 steps. For CUTOFF, DETUNE and COMB TUNE on a sustained drone
that is audible as stepping when automated slowly.

Two mitigations, in order of preference:

1. **Smooth in the firmware.** A one-pole slew on each parameter at block rate
   (every 256 frames) removes the zipper without any MIDI cost, and suits
   drones anyway — nothing should jump. Cheap and recommended.
2. **14-bit CC pairs** (MSB 0–31 with LSB 32–63) for the three or four
   parameters that need it. Standard, but Ableton does not emit them natively.

Start with (1); add (2) only if stepping is still audible.

## Open questions

* **MPE?** Per-note expression would suit a drone instrument, but four voices
  on four channels already gives most of the benefit with far less complexity.
  Deferred.
* **Clock.** The drift LFOs could sync to MIDI clock. Attractive for ambient
  work, but free-running and slightly irrational is usually better for drones.
  Make it an option, default off.
* **Patch dump.** SysEx save/load would let Ableton store a whole state.
  Dinghy already has a flash settings sector; worth doing once the parameter
  table exists, since the table *is* the patch format.
