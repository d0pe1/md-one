#!/usr/bin/env bash
# Prove the pi32v2 toolchain works, end to end, independently of any firmware project.
#
#   1. the compiler and binutils exist
#   2. a minimal freestanding C program compiles for pi32v2
#   3. the object disassembles with the vendor objdump
#   4. the object really is pi32v2 (functional check, not a string match)
#
# Writes nothing outside a temp dir. Run inside WSL Arch.
#
# NOTE on checking the backend: `clang --version` on this toolchain prints
# "Target: x86_64-unknown-linux-gnu" and no "Registered Targets" list, and its
# InstalledDir happens to contain the string "pi32v2" -- so grepping --version
# for "pi32v2" is a false positive that passes even on a stock clang. The only
# honest test is to compile something and ask objdump what came out.
set -euo pipefail

JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
CC="$JIELI_TOOLCHAIN/pi32v2/bin/clang"
LD="$JIELI_TOOLCHAIN/pi32v2/bin/ld"
OBJCOPY="$JIELI_TOOLCHAIN/common/bin/objcopy"
OBJDUMP="$JIELI_TOOLCHAIN/common/bin/objdump"

ok()   { printf '  [ok]   %s\n' "$1"; }
fail() { printf '  [FAIL] %s\n' "$1"; exit 1; }

echo "== 1. toolchain binaries =="
for t in "$CC" "$LD" "$OBJCOPY" "$OBJDUMP"; do
    [ -x "$t" ] && ok "$(basename "$t")" || fail "missing $t"
done
echo "  $("$CC" --version | head -1)   [$JIELI_TOOLCHAIN]"
echo "  backends shipped: $(ls "$JIELI_TOOLCHAIN" | grep -vE 'common|README' | tr '\n' ' ')"

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

echo
echo "== 2. compile a minimal freestanding C program =="
cat > "$work/min.c" <<'EOF'
/* No libc, no startup: just enough shape to see real pi32v2 code generated --
   a loop, a multiply, a memory write and a call, which together exercise
   arithmetic, control flow, the stack and the calling convention. */
typedef unsigned int u32;
typedef int          i32;

static i32 scale(i32 x, i32 k) { return (x * k) >> 8; }

void fill(volatile u32 *dst, u32 n, i32 gain)
{
    for (u32 i = 0; i < n; i++)
        dst[i] = (u32)scale((i32)i, gain);
}
EOF

"$CC" -target pi32v2 -O1 -ffreestanding -nostdlib -fno-builtin \
      -c "$work/min.c" -o "$work/min.o"
ok "compiled min.c -> min.o ($(stat -c%s "$work/min.o") bytes)"

echo
echo "== 3. disassemble with the vendor objdump =="
"$OBJDUMP" -d "$work/min.o" > "$work/min.dis" 2>&1
grep -qE '^\s+[0-9a-f]+:' "$work/min.dis" || fail "objdump produced no disassembly"
ok "objdump produced instructions"
echo "  --- fill() as pi32v2 ---"
sed -n '/^fill:/,/^$/p' "$work/min.dis" | sed 's/^/   /'

echo
echo "== 4. architecture metadata =="
# The authoritative statement: the vendor objdump names the object's format.
fmt="$(grep -oE 'file format [A-Za-z0-9_-]+' "$work/min.dis" | head -1)"
echo "  objdump: $fmt"
[ "$fmt" = "file format ELF32-pi32v2" ] || fail "expected ELF32-pi32v2, got '$fmt'"
ok "object is ELF32-pi32v2"

# Cross-check with the system `file`, which has no pi32 support and so reports
# the raw e_machine -- 0xf1 (241) is pi32v2's value. Confirms it is not a
# relabelled mainstream architecture.
if command -v file >/dev/null; then
    desc="$(file -b "$work/min.o")"
    echo "  file(1): $desc"
    case "$desc" in
        *"unknown arch 0xf1"*) ok "e_machine 0xf1 (241) = pi32v2, unknown to binutils as expected" ;;
        *) echo "  [note] unexpected file(1) description; record this in OPEN_QUESTIONS.md" ;;
    esac
fi

echo
echo "== 5. negative control =="
# A stock host clang must NOT be able to produce this, or check 4 proves nothing.
if command -v clang >/dev/null 2>&1; then
    if clang -target pi32v2 -c "$work/min.c" -o "$work/host.o" 2>/dev/null; then
        echo "  [note] the system clang also accepted -target pi32v2 -- investigate"
    else
        ok "system clang rejects -target pi32v2 (vendor backend is doing the work)"
    fi
else
    echo "  (no system clang installed; negative control skipped)"
fi

echo
echo "All toolchain checks passed."
