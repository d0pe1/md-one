#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build everything, then RUN it. The single gate before a commit.
#
# This exists because three separate consumers -- dsp/target/probe_drone.cc,
# dsp/host/render_drone.cc and firmware/src/drone.c -- sat broken for days
# after P_DRIFT_DEPTH was split into P_FM and P_MOD. The engine and its header
# were updated; nothing built the three files that used them, so nothing
# noticed. Two build scripts were separately compiling the engine as C++98
# while the firmware shipped it as C++11.
#
# `make test` covers dsp/ only. This covers the whole tree, and the last stage
# is the one the others cannot replace: the firmware is loaded into the
# emulator and has to actually boot and make a sound. Compiling is not
# verifying.
#
# Usage: scripts/check-all.sh [--quick]
#   --quick   skip the emulator stage (everything else still runs)
set -uo pipefail

R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TC=${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}
SDK=${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}
EMU=${FM1_LIVE:-$HOME/fm1lab/emu-target/release/fm1-live}
EMU_WIN=$R/references/fm1-emu/target/release/fm1-live.exe
H=$R/references/fm1-emu/probes/harness
MI=$R/dsp/mi
W=$R/dsp/build/checkall

QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

pass=0; fail=0
stage() { printf '\n== %s\n' "$1"; }
ok()    { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()   { printf '  FAIL  %s\n' "$1"; fail=$((fail + 1)); }

run() { # run <label> <command...>
    local label=$1; shift
    if "$@" > "$W/last.log" 2>&1; then
        ok "$label"
    else
        bad "$label"
        grep -iE "error|fail" "$W/last.log" | head -6 | sed 's/^/        /'
    fi
}

mkdir -p "$W"
echo "MD-One -- full build and run check"
echo "toolchain $TC"

# --- 1. the mathematics ----------------------------------------------------
stage "dsp tests"
run "make test (oscillator, comb, mix bus, page map)" make -C "$R/dsp" -s test

# --- 2. the chip's rules ---------------------------------------------------
stage "target rules"
run "make target (no float, no 64-bit divide, pi32v2 clean)" \
    make -C "$R/dsp" -s target

# --- 3. the host tools -----------------------------------------------------
# render_drone.cc is not covered by `make test`, and was broken for days.
stage "host tools"
run "render_drone builds"  make -C "$R/dsp" -s build/render_drone
run "render_braids builds" make -C "$R/dsp" -s build/render_braids

# --- 4. the probes ---------------------------------------------------------
# Every measured number in docs/ comes from these. A probe that stops
# compiling silently leaves the last figure it produced looking current.
stage "target probes"
PF="-target pi32v2 -Os -ffreestanding -nostdlib -fno-builtin -fno-exceptions
    -fno-rtti -fno-threadsafe-statics -std=c++11
    -I$MI/shim -I$MI -I$MI/stmlib/.. -I$H -I$R/dsp/engine"
# Globbed, not listed. A named list is a list someone forgets to extend, and
# the probe left off it is the one that rots -- which is the failure this
# script exists to catch. probe_svf and probe_fm were both missing from the
# list this replaced.
for f in "$R"/dsp/target/probe_*.cc; do
    [ -f "$f" ] || continue
    p=$(basename "$f" .cc)
    run "$p.cc compiles" \
        "$TC/pi32v2/bin/clang++" $PF -c "$f" -o "$W/$p.o"
done

# --- 5. the firmware -------------------------------------------------------
stage "firmware"
PKG=$R/firmware/build/dinghy.fwsc
# Delete the package FIRST. Without this a failed compile leaves the previous
# run's .fwsc in place, the emulator stage happily boots it, and a broken
# build reports "1 FAILED, 11 passed" -- where the 11 includes "the firmware
# boots and makes a sound", which was true of a binary that no longer exists.
# Seen exactly once, which was enough.
rm -f "$PKG"
if [ ! -d "$SDK" ]; then
    bad "AC79 SDK not found at $SDK (set AC79_SDK)"
else
    run "build.py (compile, link, brick checks, package)" \
        env JIELI_TOOLCHAIN="$TC" AC79_SDK="$SDK" \
            python3 "$R/firmware/tools/build.py"
fi


# --- 6. does it actually run? ----------------------------------------------
stage "emulator"
if [ "$QUICK" = 1 ]; then
    printf '  skipped (--quick)\n'
elif [ ! -f "$PKG" ]; then
    bad "no package to run (the firmware stage produced no $PKG)"
else
    RAW=$W/boot.pcm
    rm -f "$RAW"

    # fm1-live links ALSA, so a Linux build needs alsa-lib headers. Rather
    # than install a system package to run a test, fall back to the Windows
    # binary: WSL executes .exe directly, it only needs its arguments as
    # Windows paths.
    emu_run() { # emu_run <ms>
        if [ -x "$EMU" ]; then
            FM1_BENCH_WAV="$RAW" timeout 300 "$EMU" \
                --fwsc "$PKG" --bench "$1" --midi none
        elif [ -x "$EMU_WIN" ]; then
            # WSLENV is not optional here. WSL does NOT forward arbitrary
            # environment variables to Windows processes, so without naming it
            # the emulator never sees FM1_BENCH_WAV: it runs, it renders audio
            # (the log still reports the frame count), and it writes nothing.
            # That failure looks exactly like a silent firmware.
            WSLENV=FM1_BENCH_WAV \
            FM1_BENCH_WAV="$(wslpath -w "$RAW")" timeout 300 "$EMU_WIN" \
                --fwsc "$(wslpath -w "$PKG")" --bench "$1" --midi none
        else
            return 127
        fi
    }

    # FM1_BENCH_WAV is an environment variable, not a flag, and despite the
    # name it writes RAW interleaved int16 -- no WAV header. See
    # references/fm1-emu/crates/fm1-live/src/main.rs:310.
    emu_run 3000 > "$W/emu.log" 2>&1
    rc=$?
    if [ "$rc" = 127 ]; then
        bad "no fm1-live binary (looked for $EMU and $EMU_WIN)"
        printf '        cd references/fm1-emu && cargo build --release -p fm1-live\n'
    elif [ "$rc" = 0 ]; then
        ok "firmware boots and runs 3 s of emulated time"
    else
        bad "emulator exited $rc"
        tail -5 "$W/emu.log" | sed 's/^/        /'
    fi

    if [ ! -s "$RAW" ]; then
        bad "no audio captured -- it booted but produced nothing"
    else
        if python3 "$R/scripts/check-audio.py" "$RAW"; then
            ok "audio is present, bounded and not clipped"
        else bad "audio assertions failed"
        fi
    fi
fi

# --- verdict ---------------------------------------------------------------
printf '\n'
if [ "$fail" -eq 0 ]; then
    printf '%d passed, nothing failed\n' "$pass"
else
    printf '%d FAILED, %d passed\n' "$fail" "$pass"
fi
exit $(( fail ? 1 : 0 ))
