#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# What does the WHOLE drone engine cost on the FM-1's pi32v2?
#
#   scripts/measure-drone-cycles.sh [SHAPE]        default TRIPLE_SAW
#
# Sweeps 1..4 voices so the fixed overhead and the per-voice cost separate,
# and reports against the 256-frame audio budget. Unlike
# measure-braids-cycles.sh this measures what actually ships: oscillators plus
# combs, drift, per-sample panning and the saturating stereo mix.
#
# Same caveat: the emulator runs 1 instruction per cycle, which ignores XIP
# flash stalls, core-1 RAM contention and interrupts. Lower bound on time.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
EMU="${FM1_EMU:-$HOME/fm1lab/emu-target/release/fm1-emu}"
H="$ROOT/references/fm1-emu/probes/harness"
MI="$ROOT/dsp/mi"
ENG="$ROOT/dsp/engine"
W="$ROOT/dsp/build/probe_drone"
CXX="$TC/pi32v2/bin/clang++"
SHAPE="${1:-TRIPLE_SAW}"

[ -x "$CXX" ] || { echo "no pi32v2 toolchain at $TC"; exit 2; }
[ -x "$EMU" ] || { echo "no emulator at $EMU"; exit 2; }
mkdir -p "$W"

FLAGS="-target pi32v2 -Os -ffreestanding -nostdlib -fno-builtin -fno-exceptions
       -fno-rtti -fno-threadsafe-statics -I$MI/shim -I$MI -I$MI/stmlib/.. -I$H"
SRC="$ENG/fm1_drone.cc $MI/fm1_braids.cc $MI/braids/analog_oscillator.cc
     $MI/braids/digital_oscillator.cc $MI/braids/macro_oscillator.cc
     $MI/braids/resources.cc $MI/stmlib/utils/random.cc"

echo "building shared objects..." >&2
COMMON="$W/crt0.o"
"$CXX" $FLAGS -c "$H/crt0.S" -o "$W/crt0.o" 2>/dev/null || exit 1
for s in $SRC; do
    o="$W/$(basename "${s%.*}").o"
    "$CXX" $FLAGS -c "$s" -o "$o" 2>"$W/err.txt" || {
        echo "COMPILE FAIL $s"; head -8 "$W/err.txt"; exit 1; }
    COMMON="$COMMON $o"
done

run() {   # run <blocks> <voices> -> instructions
    local blocks=$1 voices=$2 tag="v${2}_b${1}"
    "$CXX" $FLAGS -DBLOCKS="$blocks" -DVOICES_ON="$voices" \
           -DSHAPE="braids::MACRO_OSC_SHAPE_$SHAPE" \
           -c "$ROOT/dsp/target/probe_drone.cc" -o "$W/probe_$tag.o" \
           2>"$W/err_$tag.txt" || {
        echo "COMPILE FAIL probe" >&2; head -8 "$W/err_$tag.txt" >&2; return 1; }

    "$TC/pi32v2/bin/ld" -T "$ROOT/dsp/target/probe.ld" -e _start \
        $COMMON "$W/probe_$tag.o" -o "$W/p_$tag.elf" 2>"$W/ld_$tag.txt" || {
        echo "LINK FAIL" >&2; head -8 "$W/ld_$tag.txt" >&2; return 1; }

    "$TC/common/bin/objcopy" -O binary -j .text "$W/p_$tag.elf" "$W/t_$tag.bin"
    "$TC/common/bin/objcopy" -O binary -j .data "$W/p_$tag.elf" "$W/d_$tag.bin" 2>/dev/null || : > "$W/d_$tag.bin"
    python3 - "$W/t_$tag.bin" "$W/d_$tag.bin" "$W/x_$tag.bin" <<'PY'
import sys
t = open(sys.argv[1], 'rb').read(); d = open(sys.argv[2], 'rb').read()
assert len(t) % 4 == 0
open(sys.argv[3], 'wb').write(b'\0' * 0x120 + t + d)
PY

    local halt
    halt=$("$TC/common/bin/objdump" -d "$W/p_$tag.elf" 2>/dev/null \
           | sed -n '/^_start:/,/^$/p' | grep -oE '^ *[0-9a-f]+:' | tail -1 | tr -d ' :')
    timeout 30 "$EMU" --bin "$W/x_$tag.bin" --entry 0x02000120 --steps 4000000000 \
        --break-pc "0x$halt" 2>&1 | head -4 > "$W/run_$tag.txt"
    local n
    n=$(grep -oE '\[[0-9]+\]' "$W/run_$tag.txt" | tr -d '[]' | head -1)
    [ -n "$n" ] || return 1
    # verify the probe completed: PROB magic, zero failures
    timeout 30 "$EMU" --bin "$W/x_$tag.bin" --entry 0x02000120 --steps $((n + 8)) \
        --dump 0x01C10000 8 > "$W/dump_$tag.txt" 2>&1
    grep -qE '0x01c10000: 50524f42 [0-9a-f]+ 00000000' "$W/dump_$tag.txt" || {
        echo "PROBE DID NOT COMPLETE (v$voices)" >&2; return 1; }
    echo "$n"
}

N=32
BUDGET=$(( 240000000 / 44100 * 256 ))

echo
echo "Drone engine on pi32v2, shape $SHAPE, comb + drift + stereo mix"
echo "Budget: 256 frames at 44.1 kHz = 5.805 ms = $BUDGET cycles"
echo
printf '  %-8s %13s %9s %9s %12s\n' VOICES CYCLES/BLOCK us/BLOCK '%BUDGET' PER-VOICE
printf '  %-8s %13s %9s %9s %12s\n' -------- ------------- --------- --------- ------------

prev=""
for v in 1 2 3 4; do
    n0=$(run 0  "$v") || { printf '  %-8s FAILED\n' "$v"; continue; }
    n1=$(run $N "$v") || { printf '  %-8s FAILED\n' "$v"; continue; }
    python3 - "$v" "$n0" "$n1" "$N" "$BUDGET" "$prev" <<'PY'
import sys
v, n0, n1, n, budget = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
prev = sys.argv[6]
per = (n1 - n0) / n
inc = f"{per - float(prev):.0f}" if prev else "-"
print(f"  {v:<8d} {per:13.0f} {per/240.0:9.1f} {100.0*per/budget:8.1f}% {inc:>12}")
print(f"__PREV__{per}")
PY
    prev=$(python3 -c "print(($n1-$n0)/$N)")
done 2>/dev/null | grep -v '^__PREV__'

echo
echo "  PER-VOICE is the increment over the previous row: the marginal cost of"
echo "  one more voice, with the fixed per-block overhead already paid."
