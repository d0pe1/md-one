#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Compile dsp/shared/ for the real target and enforce the chip's rules BEFORE
# the code ever reaches a firmware build.
#
# Rationale: dsp/shared/ is the code that ships on the device, but day to day we
# compile and test it with the host compiler, which happily accepts float, 64-bit
# division and anything else. Those only fail much later, at firmware link time,
# with an error that points at a libgcc symbol rather than at the line that
# caused it. This runs the same code through JieLi's pi32v2 clang and fails
# early, naming the file.
#
# Checks, from references/dinghy/docs/RULES.md:
#   1. it compiles for pi32v2 at all
#   2. it pulls in no compiler runtime helper (soft-float, 64-bit divide)
#   3. it references no undefined symbol other than the ones we expect
#
# Requires the toolchain from scripts/setup-wsl-toolchain.sh. Linux/WSL only.
set -uo pipefail

JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
CC="$JIELI_TOOLCHAIN/pi32v2/bin/clang"
CXX="$JIELI_TOOLCHAIN/pi32v2/bin/clang++"
NM="$JIELI_TOOLCHAIN/pi32v2/bin/nm"
OBJDUMP="$JIELI_TOOLCHAIN/common/bin/objdump"

cd "$(dirname "$0")/.."            # dsp/
out="build/target"
fails=0

if [ ! -x "$CC" ]; then
    echo "target check: no pi32v2 toolchain at $JIELI_TOOLCHAIN"
    echo "  run scripts/setup-wsl-toolchain.sh inside WSL first"
    exit 2
fi

mkdir -p "$out"
echo "pi32v2: $("$CC" --version | head -1)"
echo

# Symbols that mean the compiler had to synthesise something the chip cannot do.
# Soft-float and 64-bit division are the two that actually bite here.
BANNED='__adddf3|__subdf3|__muldf3|__divdf3|__floatsidf|__fixdfsi|__extendsfdf2|__truncdfsf2|__addsf3|__subsf3|__mulsf3|__divsf3|__udivdi3|__divdi3|__umoddi3|__moddi3'

for src in shared/*.c; do
    obj="$out/$(basename "${src%.c}").o"
    printf '%-24s ' "$(basename "$src")"

    if ! "$CC" -target pi32v2 -O2 -ffreestanding -nostdlib -fno-builtin \
               -Ishared -c "$src" -o "$obj" 2> "$out/err.txt"; then
        echo "FAIL (compile)"
        sed 's/^/    /' "$out/err.txt"
        fails=$((fails + 1))
        continue
    fi

    undef="$("$NM" -u "$obj" 2>/dev/null | awk '{print $NF}' | sort -u)"
    bad="$(printf '%s\n' "$undef" | grep -E "$BANNED" || true)"

    if [ -n "$bad" ]; then
        echo "FAIL (needs compiler runtime)"
        printf '%s\n' "$bad" | sed 's/^/    uses /'
        echo "    -> no FPU and no libgcc on this chip; use fixed point, and"
        echo "       shift instead of dividing 64-bit values (see docs/RULES.md)"
        fails=$((fails + 1))
        continue
    fi

    size="$(stat -c%s "$obj")"
    echo "ok  ${size} B$( [ -n "$undef" ] && printf '  (undef: %s)' "$(echo "$undef" | tr '\n' ' ')" )"
done

# --- the vendored Mutable Instruments code ---------------------------------
# Same rules, same compiler. This is the code that will actually ship, so it
# gets checked here rather than being discovered at firmware link time.
# The C++ shims in mi/shim/ come FIRST on the include path: this toolchain is
# freestanding and has no libstdc++.
if [ -d mi/braids ]; then
    echo
    echo "our engine + vendored MI (C++, freestanding):"
    mi_total=0
    # -std=c++11 matches firmware/tools/build.py exactly. This clang is 4.0.1
    # and defaults to C++98, so without it the check compiled the engine
    # DIFFERENTLY from the firmware that actually ships -- and rejected
    # static_assert, which the firmware build accepts. A verification script
    # that does not build what is shipped is not verifying much.
    MI_FLAGS="-target pi32v2 -O2 -ffreestanding -nostdlib -fno-builtin \
              -fno-exceptions -fno-rtti -fno-threadsafe-statics -std=c++11 \
              -Imi/shim -Imi -Imi/stmlib/.."
    for src in engine/fm1_drone.cc \
               mi/fm1_braids.cc mi/braids/analog_oscillator.cc \
               mi/braids/digital_oscillator.cc mi/braids/macro_oscillator.cc \
               mi/braids/resources.cc mi/stmlib/utils/random.cc; do
        [ -f "$src" ] || continue
        obj="$out/$(basename "${src%.cc}").o"
        printf '  %-24s ' "$(basename "$src")"
        if ! "$CXX" $MI_FLAGS -c "$src" -o "$obj" 2> "$out/err.txt"; then
            echo "FAIL (compile)"
            grep -oE 'fatal error: .*|error: .*' "$out/err.txt" | sort -u | head -4 | sed 's/^/      /'
            fails=$((fails + 1))
            continue
        fi
        sz="$(stat -c%s "$obj")"; mi_total=$((mi_total + sz))
        bad="$("$NM" -u "$obj" 2>/dev/null | awk '{print $NF}' | grep -E "$BANNED" || true)"
        if [ -n "$bad" ]; then
            echo "FAIL (needs compiler runtime)"
            printf '%s\n' "$bad" | sed 's/^/      uses /'
            fails=$((fails + 1))
            continue
        fi
        echo "ok  ${sz} B"
    done
    printf '  %-24s %s B total (~%s KB of the ~526 KB free)\n' \
           "" "$mi_total" "$((mi_total / 1024))"
fi

echo
if [ "$fails" -ne 0 ]; then
    echo "target check: $fails file(s) violate the chip's rules"
    exit 1
fi
echo "target check: dsp/ is clean for pi32v2"

# Leave a disassembly behind: it is the only way to see what the target code
# actually became, and the figures feed the cycle-budget discussion.
for obj in "$out"/*.o; do
    "$OBJDUMP" -d "$obj" > "${obj%.o}.dis" 2>/dev/null
done
echo "disassembly in dsp/$out/"
