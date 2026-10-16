#!/usr/bin/env bash
# Set up the Linux side of the FM-1 firmware toolchain (run inside WSL Arch).
#
#   JieLi's pi32v2 clang      -> ~/.jieli/toolchain  (symlink to the dated release)
#   JieLi AC79 AIoT SDK       -> ~/fw-AC79_AIoT_SDK  (pinned branch, shallow)
#   a Pillow venv for builds  -> ~/.jieli/venv       (dinghy's build.py needs PIL)
#
# Both locations are the defaults dinghy's build.sh expects, and both live
# outside the project tree so no toolchain binaries land in the source.
#
# Idempotent: every step is skipped if it is already in place.
set -euo pipefail

JIELI_DIR="${JIELI_DIR:-$HOME/.jieli}"
SDK_DIR="${SDK_DIR:-$HOME/fw-AC79_AIoT_SDK}"
SDK_BRANCH=AC79NN_SDK_V1.2.1_2023-12-13
TOOLCHAIN_URL=https://pkgman.jieliapp.com/s/linux-toolchain

step() { printf '\n=== %s ===\n' "$1"; }

step "pi32v2 toolchain -> $JIELI_DIR"
if [ -x "$JIELI_DIR/toolchain/pi32v2/bin/clang" ]; then
    echo "already present: $(readlink "$JIELI_DIR/toolchain" || echo "$JIELI_DIR/toolchain")"
else
    mkdir -p "$JIELI_DIR"
    curl -fL --retry 3 "$TOOLCHAIN_URL" | tar -xJ -C "$JIELI_DIR"
    TC="$(ls -d "$JIELI_DIR"/jieli-linux-toolchains-* | sort | tail -1)"
    ln -sfn "$(basename "$TC")" "$JIELI_DIR/toolchain"
    echo "installed $(basename "$TC")"
fi

step "AC79 AIoT SDK -> $SDK_DIR"
if [ -f "$SDK_DIR/cpu/wl82/tools/uboot.boot" ]; then
    echo "already present ($(git -C "$SDK_DIR" rev-parse --short HEAD 2>/dev/null || echo '?'))"
else
    git clone --depth 1 --branch "$SDK_BRANCH" \
        https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$SDK_DIR"
fi

step "Pillow venv -> $JIELI_DIR/venv"
if [ -x "$JIELI_DIR/venv/bin/python" ] && "$JIELI_DIR/venv/bin/python" -c 'import PIL' 2>/dev/null; then
    echo "already present: $("$JIELI_DIR/venv/bin/python" -c 'import PIL; print("Pillow", PIL.__version__)')"
else
    python3 -m venv "$JIELI_DIR/venv"
    "$JIELI_DIR/venv/bin/pip" install --quiet --upgrade pip
    "$JIELI_DIR/venv/bin/pip" install --quiet Pillow
    "$JIELI_DIR/venv/bin/python" -c 'import PIL; print("Pillow", PIL.__version__)'
fi

step "versions"
"$JIELI_DIR/toolchain/pi32v2/bin/clang" --version | head -3
echo
cat <<EOF
Set these when building firmware:
  export JIELI_TOOLCHAIN=$JIELI_DIR/toolchain
  export AC79_SDK=$SDK_DIR
  export PYTHON=$JIELI_DIR/venv/bin/python
EOF
