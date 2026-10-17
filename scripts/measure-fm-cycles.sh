#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# What an FM operator costs on pi32v2, measured rather than estimated.
#
# Context for the numbers: the STOCK FM-1 firmware runs 12 voices of 6
# operators -- 72 at once -- plus effects, sequencer and UI, on this exact
# chip. The ceiling is known to be high; what this establishes is where OUR
# implementation sits under it, which is the only figure we can spend.
#
# Builds dsp/target/probe_fm.cc once per row and runs it under the emulator.
# Each row is an N-block run minus a 0-block run, so loop overhead and setup
# cancel out.
#
# Usage: scripts/measure-fm-cycles.sh
set -uo pipefail

R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TC=${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}
EMU=${FM1_EMU:-$HOME/fm1lab/emu-target/release/fm1-emu}
H=$R/references/fm1-emu/probes/harness
W=$R/dsp/build/fm
CXX=$TC/pi32v2/bin/clang++

for f in "$CXX" "$EMU"; do
    [ -x "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
mkdir -p "$W"

F="-target pi32v2 -Os -ffreestanding -nostdlib -fno-builtin -fno-exceptions
   -fno-rtti -fno-threadsafe-statics -std=c++11 -I$H"

"$CXX" $F -c "$H/crt0.S" -o "$W/crt0.o" 2>"$W/e_crt0.txt" \
    || { echo "crt0 failed:" >&2; head -5 "$W/e_crt0.txt" >&2; exit 1; }
# fm1_osc.c is C, so it gets the C compiler and C flags -- -std=c++11 is not
# merely redundant there, clang rejects it outright.
CF="-target pi32v2 -Os -ffreestanding -nostdlib -fno-builtin -std=c99 -I$H"
"$TC/pi32v2/bin/clang" $CF -c "$R/dsp/shared/fm1_osc.c" -o "$W/osc.o" \
    2>"$W/e_osc.txt" \
    || { echo "fm1_osc.c failed:" >&2; head -8 "$W/e_osc.txt" >&2; exit 1; }

BLOCKS=32
BUDGET=$(( 240000000 / 44100 * 256 ))

run() {   # run <blocks> <ops> <feedback> <voices>
    local b=$1 o=$2 fbk=$3 v=$4 t="b${1}o${2}f${3}v${4}"

    "$CXX" $F -DBLOCKS=$b -DOPS=$o -DFEEDBACK=$fbk -DVOICES=$v \
        -c "$R/dsp/target/probe_fm.cc" -o "$W/p_$t.o" 2>"$W/e_$t.txt" \
        || { echo "COMPILE FAIL ops=$o" >&2; head -8 "$W/e_$t.txt" >&2; return 1; }

    "$TC/pi32v2/bin/ld" -T "$R/dsp/target/probe.ld" -e _start \
        "$W/crt0.o" "$W/osc.o" "$W/p_$t.o" -o "$W/p_$t.elf" 2>"$W/l_$t.txt" \
        || { echo "LINK FAIL ops=$o" >&2; head -5 "$W/l_$t.txt" >&2; return 1; }

    "$TC/common/bin/objcopy" -O binary -j .text "$W/p_$t.elf" "$W/t_$t.bin"
    "$TC/common/bin/objcopy" -O binary -j .data "$W/p_$t.elf" "$W/d_$t.bin" \
        2>/dev/null || : > "$W/d_$t.bin"
    python3 -c "
t=open('$W/t_$t.bin','rb').read(); d=open('$W/d_$t.bin','rb').read()
open('$W/x_$t.bin','wb').write(b'\0'*0x120+t+d)"

    local halt
    halt=$("$TC/common/bin/objdump" -d "$W/p_$t.elf" 2>/dev/null \
           | sed -n '/^_start:/,/^$/p' | grep -oE '^ *[0-9a-f]+:' \
           | tail -1 | tr -d ' :')

    # Written to a file, never piped into head: with pipefail, head closing
    # the pipe SIGPIPEs the emulator and the row silently vanishes.
    timeout 120 "$EMU" --bin "$W/x_$t.bin" --entry 0x02000120 \
        --steps 4000000000 --break-pc "0x$halt" > "$W/run_$t.txt" 2>&1
    grep -oE '\[[0-9]+\]' "$W/run_$t.txt" | tr -d '[]' | head -1
}

row() {   # row <label> <ops> <feedback> <voices>
    local label=$1 o=$2 fbk=$3 v=$4 n0 n1
    n0=$(run 0       "$o" "$fbk" "$v") || return 0
    n1=$(run $BLOCKS "$o" "$fbk" "$v") || return 0
    [ -n "$n0" ] && [ -n "$n1" ] || { printf '  %-22s NO RESULT\n' "$label"; return 0; }
    python3 - "$label" "$n0" "$n1" "$BLOCKS" "$v" "$o" "$BUDGET" <<'PY'
import sys
label = sys.argv[1]
n0, n1, blocks, voices, ops, budget = (int(x) for x in sys.argv[2:8])
per_block = (n1 - n0) / blocks
per_voice = per_block / voices
per_op    = per_voice / ops
print(f"  {label:<22} {per_block:12.0f} {per_voice:11.0f} {per_op:9.0f} "
      f"{100.0 * per_block / budget:8.2f}%")
PY
}

echo "FM operator cost on pi32v2 -- 256-frame block, serial stack"
echo "Budget: $BUDGET cycles/block (5.805 ms at 240 MHz)"
echo "For scale: the stock firmware runs 12 voices x 6 operators = 72."
echo
printf '  %-22s %12s %11s %9s %9s\n' CONFIGURATION 'CYC/BLOCK' 'PER VOICE' 'PER OP' '% BUDGET'
printf '  %-22s %12s %11s %9s %9s\n' ---------------------- ------------ ----------- --------- ---------

row "2-op, no feedback"  2 0 4
row "2-op, feedback"     2 1 4
row "4-op, feedback"     4 1 4
row "6-op, feedback"     6 1 4
echo
row "6-op, 1 voice"      6 1 1
row "6-op, 12 voices"    6 1 12
