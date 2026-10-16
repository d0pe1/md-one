#!/usr/bin/env bash
# Vendor the minimal Mutable Instruments Braids source set into dsp/mi/.
#
# PROVENANCE. Source is pichenettes/eurorack (upstream, not a fork or a mirror),
# whose STM32F projects -- Braids among them -- are MIT under Emilie Gillet's
# copyright, with the licence header in every file. We copy those files
# unmodified and keep their headers intact.
#
# We deliberately do NOT take Braids from:
#   * VCVRack/AudibleInstruments src/   -- GPL-3.0-or-later, and contains no DSP
#   * DeadActive/hortator               -- GPL-3.0-only
# Either would have made our engine GPL. See docs/LICENSE_REGISTER.md.
#
# Only the files the TRIPLE_* / analog-oscillator path needs are copied. The
# rest of Braids stays out until something needs it.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${EURORACK:-$ROOT/references/eurorack}"
DEST="$ROOT/dsp/mi"

[ -d "$SRC/braids" ] || { echo "no eurorack clone at $SRC"; exit 2; }
SHA="$(git -C "$SRC" rev-parse HEAD)"

mkdir -p "$DEST/braids" "$DEST/stmlib/utils"

# Braids: every header (they include each other freely) plus the translation
# units the oscillator path needs.
#
# resources.cc / resources.h are NOT copied. They are the lookup tables, and
# upstream ships them generated for 96 kHz. scripts/gen-braids-resources.sh
# regenerates them at 44.1 kHz from MI's own generators -- run it after this.
cp "$SRC"/braids/*.h                      "$DEST/braids/"
rm -f "$DEST/braids/resources.h"
for f in analog_oscillator digital_oscillator macro_oscillator; do
    cp "$SRC/braids/$f.cc" "$DEST/braids/"
done

# stmlib: the headers Braids includes, plus Random's static definition.
cp "$SRC"/stmlib/stmlib.h                 "$DEST/stmlib/"
cp "$SRC"/stmlib/utils/dsp.h              "$DEST/stmlib/utils/"
cp "$SRC"/stmlib/utils/random.h           "$DEST/stmlib/utils/"
cp "$SRC"/stmlib/utils/random.cc          "$DEST/stmlib/utils/" 2>/dev/null || true

# Record exactly what came from where, so provenance survives.
{
    echo "# Vendored Mutable Instruments source"
    echo
    echo "Upstream: https://github.com/pichenettes/eurorack"
    echo "Commit:   $SHA"
    echo "Vendored: $(date -u +%Y-%m-%d) (UTC)"
    echo "Licence:  MIT, (c) Emilie Gillet -- header retained in every file"
    echo
    echo "Copied unmodified:"
    echo
    echo '| File | SHA-256 |'
    echo '| --- | --- |'
    (cd "$DEST" && find braids stmlib -type f ! -name 'resources.*' | sort | while read -r f; do
        echo "| \`$f\` | \`$(sha256sum "$f" | cut -c1-16)…\` |"
    done)
    echo
    echo "**NOT copied:** \`braids/resources.cc\` and \`braids/resources.h\`."
    echo "Upstream ships those generated for 96 kHz. Run"
    echo "\`scripts/gen-braids-resources.sh 44100\` to generate them for the"
    echo "FM-1's sample rate; see \`braids/RESOURCES_GENERATED.md\`."
} > "$DEST/PROVENANCE.md"

echo "vendored into dsp/mi/ from $SHA"
echo "  NOTE: resources.cc/.h not copied -- run scripts/gen-braids-resources.sh 44100"
find "$DEST/braids" "$DEST/stmlib" -type f | wc -l | xargs echo "  files:"
du -sh "$DEST" | awk '{print "  size: " $1}'
