#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# How many cycles does a Braids voice cost on the FM-1's pi32v2?
#
#   scripts/measure-braids-cycles.sh [SHAPE ...]      names without the
#                                                     MACRO_OSC_SHAPE_ prefix
#
# Builds dsp/target/probe_braids.cc as a flat image twice per shape -- with the
# render loop and without it -- runs both in fm1-emu, and subtracts. Setup,
# Init(), the .data copy and the harness all cancel, leaving the render cost.
#
# The emulator clocks the AC791N at 240 MHz and one instruction per cycle, so
# retired instructions are the cycle estimate. That is OPTIMISTIC: real silicon
# stalls on XIP flash reads (every Braids lookup table lives in flash), on RAM
# contention with core 1, and on interrupts. Read the numbers as a lower bound
# on time, hence an upper bound on voice count.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
EMU="${FM1_EMU:-$HOME/fm1lab/emu-target/release/fm1-emu}"
H="$ROOT/references/fm1-emu/probes/harness"
MI="$ROOT/dsp/mi"
W="$ROOT/dsp/build/probe"
CXX="$TC/pi32v2/bin/clang++"

[ -x "$CXX" ] || { echo "no pi32v2 toolchain at $TC"; exit 2; }
[ -x "$EMU" ] || { echo "no emulator at $EMU"; echo "  build it in WSL: CARGO_TARGET_DIR=\$HOME/fm1lab/emu-target cargo build --release -p fm1-emu"; exit 2; }

mkdir -p "$W"

FLAGS="-target pi32v2 -Os -ffreestanding -nostdlib -fno-builtin -fno-exceptions
       -fno-rtti -fno-threadsafe-statics -I$MI/shim -I$MI -I$MI/stmlib/.. -I$H"
SRC="$MI/fm1_braids.cc $MI/braids/analog_oscillator.cc $MI/braids/digital_oscillator.cc
     $MI/braids/macro_oscillator.cc $MI/braids/resources.cc $MI/stmlib/utils/random.cc"

# Only probe_braids.cc depends on BLOCKS/SHAPE, so the MI objects and crt0 are
# built once and reused. resources.cc alone is 384 KB of source; rebuilding it
# per variant dominated the run time.
COMMON_OBJS=""
build_common() {
    echo "building the shared objects once (resources.cc is large)..." >&2
    "$CXX" $FLAGS -c "$H/crt0.S" -o "$W/crt0.o" 2>/dev/null || return 1
    COMMON_OBJS="$W/crt0.o"
    local s o
    for s in $SRC; do
        o="$W/$(basename "${s%.*}").o"
        "$CXX" $FLAGS -c "$s" -o "$o" 2>"$W/err_common.txt" || {
            echo "COMPILE FAIL $s" >&2; head -5 "$W/err_common.txt" >&2; return 1; }
        COMMON_OBJS="$COMMON_OBJS $o"
    done
}

# build_and_run <blocks> <shape> -> prints instructions retired
build_and_run() {
    local blocks=$1 shape=$2 tag="${2}_${1}"

    "$CXX" $FLAGS -DBLOCKS="$blocks" -DSHAPE="braids::MACRO_OSC_SHAPE_$shape" \
           -c "$ROOT/dsp/target/probe_braids.cc" -o "$W/probe_$tag.o" \
           2>"$W/err_$tag.txt" || {
        echo "COMPILE FAIL probe ($shape)" >&2; head -5 "$W/err_$tag.txt" >&2; return 1; }
    local objs="$COMMON_OBJS $W/probe_$tag.o"

    "$TC/pi32v2/bin/ld" -T "$ROOT/dsp/target/probe.ld" -e _start $objs \
        -o "$W/p_$tag.elf" 2>"$W/ld_$tag.txt" || {
        echo "LINK FAIL" >&2; head -5 "$W/ld_$tag.txt" >&2; return 1; }

    # This objcopy takes exactly one -j, so extract the two loadable sections
    # separately. probe.ld places .data's load copy immediately after .text in
    # XIP (both 4-aligned), so appending reconstructs the image exactly.
    "$TC/common/bin/objcopy" -O binary -j .text "$W/p_$tag.elf" "$W/t_$tag.bin"
    "$TC/common/bin/objcopy" -O binary -j .data "$W/p_$tag.elf" "$W/d_$tag.bin" 2>/dev/null || : > "$W/d_$tag.bin"

    python3 - "$W/t_$tag.bin" "$W/d_$tag.bin" "$W/x_$tag.bin" <<'PY'
import sys
text = open(sys.argv[1], 'rb').read()
data = open(sys.argv[2], 'rb').read()
assert len(text) % 4 == 0, "text not 4-aligned; .data LMA would not follow"
open(sys.argv[3], 'wb').write(b'\0' * 0x120 + text + data)
PY

    # crt0 ends in `goto .`, an infinite loop, so the run never halts by itself
    # and would just burn the step limit. Break on that instruction instead:
    # it is the last one in _start, and objdump prints absolute addresses.
    local halt
    halt=$("$TC/common/bin/objdump" -d "$W/p_$tag.elf" 2>/dev/null \
           | sed -n '/^_start:/,/^$/p' | grep -oE '^ *[0-9a-f]+:' | tail -1 | tr -d ' :')
    [ -n "$halt" ] || { echo "no halt address" >&2; return 1; }

    # --break-pc REPORTS but does not halt, and crt0 ends in `goto .`, so the
    # breakpoint re-fires on every iteration forever. Piping through head
    # closes the pipe after the first few lines, SIGPIPEs the emulator and ends
    # the run immediately -- without this each run sat until its timeout.
    timeout 30 "$EMU" --bin "$W/x_$tag.bin" --entry 0x02000120 --steps 4000000000 \
           --break-pc "0x$halt" 2>&1 | head -4 > "$W/run_$tag.txt"

    # "break c0 pc=0x02000136 [2271550] sp=..." -- the bracketed instruction count
    local n
    n=$(grep -oE '^break c0 pc=0x[0-9a-f]+ \[[0-9]+\]' "$W/run_$tag.txt" \
        | grep -oE '[0-9]+\]' | tr -d ']' | head -1)
    [ -n "$n" ] || return 1
    echo "$n"

    # Re-run bounded to just past that point, with no breakpoint, so the run
    # ends at the step limit and the emulator prints its memory dump. That is
    # the only way to read the results block back.
    timeout 30 "$EMU" --bin "$W/x_$tag.bin" --entry 0x02000120 --steps $((n + 8)) \
           --dump 0x01C10000 8 > "$W/dump_$tag.txt" 2>&1
}

# The probe ran to completion: RESULTS[0] is the "PROB" magic, written last by
# p_done(), and RESULTS[2] is the failure count.
probe_ok() {
    local tag=$1
    grep -qE '0x01c10000: 50524f42 [0-9a-f]+ 00000000' "$W/dump_$tag.txt" 2>/dev/null
}

SHAPES=("$@")
[ ${#SHAPES[@]} -eq 0 ] && SHAPES=(TRIPLE_SAW TRIPLE_SINE SAW_SWARM HARMONICS \
                                   BOWED FLUTED PLUCKED FILTERED_NOISE WAVETABLES)
N=64
BUDGET=$(( 240000000 / 44100 * 256 ))

build_common || exit 1

echo "FM-1 audio block: 256 frames at 44.1 kHz = 5.805 ms"
echo "240 MHz, 1 insn/cycle (optimistic) -> $BUDGET cycles per block"
echo
printf '  %-16s %13s %9s %9s %8s\n' SHAPE CYCLES/BLOCK us/BLOCK '%BUDGET' VOICES
printf '  %-16s %13s %9s %9s %8s\n' ---------------- ------------- --------- --------- --------

for shape in "${SHAPES[@]}"; do
    n0=$(build_and_run 0 "$shape");  ok0=$(probe_ok "${shape}_0" && echo y)
    n1=$(build_and_run $N "$shape"); ok1=$(probe_ok "${shape}_$N" && echo y)
    if [ -z "${n0:-}" ] || [ -z "${n1:-}" ] || [ "$n1" -le "$n0" ] 2>/dev/null; then
        printf '  %-16s %13s   (see %s)\n' "$shape" "FAILED" "$W/run_${shape}_$N.txt"
        continue
    fi
    if [ "${ok0:-}" != y ] || [ "${ok1:-}" != y ]; then
        printf '  %-16s %13s   probe did not complete cleanly\n' "$shape" "SUSPECT"
        continue
    fi
    python3 - "$shape" "$n0" "$n1" "$N" "$BUDGET" <<'PY'
import sys
shape, n0, n1, n, budget = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
per = (n1 - n0) / n
print(f"  {shape:<16} {per:13.0f} {per/240.0:9.1f} {100.0*per/budget:8.1f}% {int(budget//per):8d}")
PY
done
echo
echo "  VOICES = how many fit in one block at 100% of budget. RULES.md advises"
echo "  staying under half, so halve it -- and see the caveat at the top."
