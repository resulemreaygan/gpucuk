#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds the host preview and renders the firmware's screens to PNG files.
#
# Usage: tools/preview/build.sh [out_dir]
#        (default: runs/preview)
#
# Author: Resul Emre Aygan
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PYTHON="${PYTHON:-$ROOT/.venv/bin/python}"
OUT="${1:-$ROOT/runs/preview}"
BIN="$ROOT/tools/preview/.build/preview"
mkdir -p "$OUT" "$(dirname "$BIN")"
c++ -std=c++17 -O2 -Wall -I"$ROOT/firmware/src" \
  "$ROOT/tools/preview/preview.cpp" \
  "$ROOT/firmware/src/ui/Canvas.cpp" \
  "$ROOT/firmware/src/ui/DemoScreen.cpp" \
  -o "$BIN"
rm -f "$OUT"/*.png
"$BIN" "$OUT"
for f in "$OUT"/*.pbm; do
  "$PYTHON" -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[2])" "$f" "${f%.pbm}.png"
  rm "$f"
done
ls "$OUT"
