#!/usr/bin/env bash
# Builds the native render harness and renders one page to a PNG.
#
# Usage: tools/sim/render.sh <companion|status|egg> [output.png] [stage]
#
# Example:
#   tools/sim/render.sh companion /tmp/companion.png 3
#
# See tools/sim/README.md for what this is, what it covers, and how to
# extend it to more pages.
set -euo pipefail
cd "$(dirname "$0")/../.."

PAGE="${1:?usage: render.sh <companion|status|egg> [output.png] [stage]}"
OUT_PNG="${2:-/tmp/digipet_sim_${PAGE}.png}"
STAGE="${3:-2}"

BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

g++ -std=c++17 -O1 \
  tools/sim/render_harness.cpp \
  src/ui_pages.cpp \
  src/pet_genome.cpp \
  lib/GFX_Library_for_Arduino/src/Arduino_G.cpp \
  lib/GFX_Library_for_Arduino/src/Arduino_GFX.cpp \
  lib/GFX_Library_for_Arduino/src/canvas/Arduino_Canvas.cpp \
  -I tools/sim/fakes -I lib/GFX_Library_for_Arduino/src -I include -I src \
  -o "$BUILD_DIR/render_harness"

PPM="$BUILD_DIR/frame.ppm"
"$BUILD_DIR/render_harness" "$PAGE" "$PPM" "$STAGE"

python3 - "$PPM" "$OUT_PNG" <<'EOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
print(f"wrote {sys.argv[2]}")
EOF
