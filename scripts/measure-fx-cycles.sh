#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# What each FX block costs on pi32v2, measured rather than estimated.
#
# Builds dsp/target/probe_fx.cc once per row and runs it under the emulator,
# which counts instructions at one per cycle. Each row is the difference
# between a 0-block run and an N-block run, so loop overhead, init and the
# comb's buffer clear all cancel out.
#
# Feeds the cost table in docs/SIGNAL_PATH.md section 8, and OPEN-5 -- which
# blocks the voice filter and the global rack should offer.
#
# Usage: scripts/measure-fx-cycles.sh
set -uo pipefail

R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TC=$HOME/.jieli/toolchain
EMU=$HOME/fm1lab/emu-target/release/fm1-emu
H=$R/references/fm1-emu/probes/harness
MI=$R/dsp/mi
W=$R/dsp/build/fx
CXX=$TC/pi32v2/bin/clang++

for f in "$CXX" "$EMU"; do
    [ -x "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
mkdir -p "$W"

F="-target pi32v2 -Os -ffreestanding -nostdlib -fno-builtin -fno-exceptions
   -fno-rtti -fno-threadsafe-statics -std=c++11 -I$MI/shim -I$MI -I$H"

"$CXX" $F -c "$H/crt0.S"               -o "$W/crt0.o" 2>"$W/e_crt0.txt" \
    || { echo "crt0 failed:" >&2; head -5 "$W/e_crt0.txt" >&2; exit 1; }
"$CXX" $F -c "$MI/braids/resources.cc" -o "$W/res.o"  2>"$W/e_res.txt" \
    || { echo "resources failed:" >&2; head -5 "$W/e_res.txt" >&2; exit 1; }

BLOCKS=32
BUDGET=$(( 240000000 / 44100 * 256 ))

# run <blocks> <kind> <n> <voices> -> instruction count on stdout, "" on failure
run() {
    local b=$1 k=$2 n=$3 v=$4 t="b${1}k${2}n${3}v${4}"

    "$CXX" $F -DBLOCKS=$b -DKIND=$k -DN=$n -DVOICES=$v \
        -c "$R/dsp/target/probe_fx.cc" -o "$W/p_$t.o" 2>"$W/e_$t.txt" \
        || { echo "COMPILE FAIL kind=$k n=$n voices=$v" >&2
             head -8 "$W/e_$t.txt" >&2; return 1; }

    # probe.ld carries an ASSERT that .bss stays below the RESULTS block at
    # 0x01C10000. A link failure here is a real finding, not a build hiccup.
    "$TC/pi32v2/bin/ld" -T "$R/dsp/target/probe.ld" -e _start \
        "$W/crt0.o" "$W/res.o" "$W/p_$t.o" -o "$W/p_$t.elf" 2>"$W/l_$t.txt" \
        || { echo "LINK FAIL kind=$k n=$n voices=$v" >&2
             head -5 "$W/l_$t.txt" >&2; return 1; }

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

    # Written to a file, never piped straight into head: with `set -o pipefail`
    # head closing the pipe SIGPIPEs the emulator, the pipeline returns 141,
    # and every row of this table silently vanished once already.
    timeout 60 "$EMU" --bin "$W/x_$t.bin" --entry 0x02000120 \
        --steps 4000000000 --break-pc "0x$halt" > "$W/run_$t.txt" 2>&1
    grep -oE '\[[0-9]+\]' "$W/run_$t.txt" | tr -d '[]' | head -1
}

# row <label> <kind> <n> <voices>
row() {
    local label=$1 k=$2 n=$3 v=$4 n0 n1
    n0=$(run 0       "$k" "$n" "$v") || return 0
    n1=$(run $BLOCKS "$k" "$n" "$v") || return 0
    [ -n "$n0" ] && [ -n "$n1" ] \
        || { printf '  %-14s %6d %3d   NO RESULT\n' "$label" "$v" "$n"; return 0; }
    python3 - "$label" "$n0" "$n1" "$BLOCKS" "$v" "$n" "$BUDGET" <<'PY'
import sys
label = sys.argv[1]
n0, n1, blocks, voices, n, budget = (int(x) for x in sys.argv[2:8])
per_block = (n1 - n0) / blocks
per_voice = per_block / voices
each      = per_voice / n if n else 0.0
print(f"  {label:<14s} {voices:^6d} {n:^3d} {per_block:12.0f} {per_voice:11.0f} "
      f"{each:11.0f} {100.0 * per_block / budget:8.2f}%")
PY
}

echo "FX block cost on pi32v2 -- 256-frame block, instances in series"
echo "Budget: $BUDGET cycles/block (5.805 ms at 240 MHz)"
echo
printf '  %-14s %6s %3s %12s %11s %11s %9s\n' \
    BLOCK VOICES N 'CYC/BLOCK' 'PER VOICE' 'PER INST' '% BUDGET'
printf '  %-14s %6s %3s %12s %11s %11s %9s\n' \
    -------------- ------ --- ------------ ----------- ----------- ---------

# SVF first: reproduces the table probe_svf.cc produced, as a regression check
# on the probe rewrite.
for n in 0 1 2 3; do row "svf" 1 "$n" 4; done
echo
# Comb at four voices, which is the configuration the instrument runs. Two
# combs x four voices is 32,800 B of .bss and would land on the RESULTS block,
# so the linearity check runs at two voices instead.
for n in 0 1;   do row "comb" 2 "$n" 4; done
for n in 0 1 2; do row "comb" 2 "$n" 2; done
