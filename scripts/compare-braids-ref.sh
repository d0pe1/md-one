#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Check our 44.1 kHz Braids port against a reference build of the SAME source
# running on upstream's 96 kHz tables.
#
#   scripts/compare-braids-ref.sh [SHAPE ...]      names from render_braids --list
#
# WHY. Comparing the regenerated tables against upstream's numerically only
# proves the generator is faithful. It does not prove the result still SOUNDS
# like Braids: our port could be correct in every table and still be driven
# wrong by the adapter. So render the same note and parameters both ways and
# compare the spectra.
#
# Partials are compared rather than samples, so no resampling is involved and
# no resampler artifacts can pollute the result. Pitch and harmonic structure
# must match; the sample rate itself need not.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/dsp/build/ref"
VENV="${JIELI_VENV:-$HOME/.jieli/venv}"
PY="$VENV/bin/python"
CXX="${CXX:-c++}"

MI="$ROOT/dsp/mi"
mkdir -p "$B"

echo "generating upstream 96 kHz tables..."
# Generate into a scratch tree so dsp/mi keeps its 44.1 kHz tables.
TMPMI="$B/mi96"
rm -rf "$TMPMI"; mkdir -p "$TMPMI/braids"
cp -r "$MI"/braids/*.h "$TMPMI/braids/" 2>/dev/null
DEST_SAVE="$MI/braids/resources.cc"
cp "$DEST_SAVE" "$B/resources_44k1.cc.bak"
cp "$MI/braids/resources.h" "$B/resources_44k1.h.bak"

"$ROOT/scripts/gen-braids-resources.sh" 96000 >/dev/null 2>&1 || {
    echo "failed to generate 96 kHz tables"; exit 1; }
cp "$MI/braids/resources.cc" "$B/resources_96k.cc"
cp "$MI/braids/resources.h"  "$B/resources_96k.h"

# put the 44.1 kHz tables back immediately -- everything else depends on them
cp "$B/resources_44k1.cc.bak" "$MI/braids/resources.cc"
cp "$B/resources_44k1.h.bak"  "$MI/braids/resources.h"
echo "  44.1 kHz tables restored"

SRC="$MI/fm1_braids.cc $MI/braids/analog_oscillator.cc $MI/braids/digital_oscillator.cc
     $MI/braids/macro_oscillator.cc $MI/stmlib/utils/random.cc"
INC="-I$MI -I$MI/stmlib/.."
FLAGS="-O2 -w -fno-exceptions -fno-rtti"

echo "building the 96 kHz reference renderer..."
# resources.h differs only in guards; compile the 96 kHz .cc in place of ours.
cp "$B/resources_96k.h" "$B/resources.h"
# FM1_BRAIDS_CHUNK=24 is essential: the reference must run Braids exactly as
# upstream does, 24 samples at 96 kHz. Our build uses 11 to keep Braids'
# per-block processing at the right wall-clock rate at 44.1 kHz. Without this
# override both sides would use the same chunk and the comparison would be
# blind to precisely the bug it exists to catch.
$CXX $FLAGS $INC -I"$B" -DRENDER_SR=96000 -DFM1_BRAIDS_CHUNK=24 \
     -o "$B/render_ref" "$ROOT/dsp/host/render_braids.cc" $SRC \
     "$B/resources_96k.cc" "$ROOT/dsp/host/wav.c" 2>&1 | head -6 || exit 1

echo "building our 44.1 kHz renderer..."
( cd "$ROOT/dsp" && make build/render_braids >/dev/null 2>&1 ) || exit 1

SHAPES=("$@")
[ ${#SHAPES[@]} -eq 0 ] && SHAPES=(triple_saw saw_swarm harmonics bowed plucked filtered_noise)

# TIMBRE and COLOR are raw Q15 here (0..32767), not the 0..127 the firmware
# uses. Mid-scale, because near-zero puts the stepped shapes in their first
# table entry where unrelated settings collapse to the same sound.
NOTE=${NOTE:-45}
TIMBRE=${TIMBRE:-16384}
COLOR=${COLOR:-16384}
fails=0

for s in "${SHAPES[@]}"; do
    echo
    echo "=============================================================="
    echo " $s   (MIDI $NOTE, timbre $TIMBRE, color $COLOR)"
    echo "=============================================================="
    "$B/render_ref" --shape "$s" --note "$NOTE" --timbre "$TIMBRE" --color "$COLOR" \
        --sec 3 --out "$B/ref_$s.wav" >/dev/null 2>&1
    "$ROOT/dsp/build/render_braids" --shape "$s" --note "$NOTE" --timbre "$TIMBRE" \
        --color "$COLOR" --sec 3 --out "$B/our_$s.wav" >/dev/null 2>&1
    if [ ! -f "$B/ref_$s.wav" ] || [ ! -f "$B/our_$s.wav" ]; then
        echo "  render failed"; fails=$((fails+1)); continue
    fi
    "$PY" "$ROOT/scripts/compare_partials.py" "$B/ref_$s.wav" "$B/our_$s.wav" || fails=$((fails+1))
done

echo
echo "=============================================================="
[ "$fails" -eq 0 ] && echo " all shapes match the reference" \
                   || echo " $fails shape(s) differ from the reference"
exit $((fails ? 1 : 0))
