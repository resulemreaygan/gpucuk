#!/bin/sh
# SPDX-License-Identifier: MIT
# Renders a GPU'cuk session with animate.cpp (device inference and drawing code, estimated device
# timing) and turns the frames into a looping GIF.
#
# Usage: tools/preview/gif.sh [out.gif lang:prompt_index:seed ...]
#        (no arguments: the README's GIFs, runs/preview/demo_tr.gif with tr:5:7112 and
#        runs/preview/demo_en.gif with en:5:8926, which are copied to docs/images/)
#
# Author: Resul Emre Aygan
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PYTHON="${PYTHON:-$ROOT/.venv/bin/python}"
BUILD="$ROOT/tools/preview/.build"
mkdir -p "$BUILD"
cc -std=c11 -O2 -Wall -Wextra -I"$ROOT/firmware/src/llm" -c "$ROOT/firmware/src/llm/llm.c" -o "$BUILD/llm.o"
c++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT/firmware/src" \
  "$ROOT/tools/preview/animate.cpp" \
  "$ROOT/firmware/src/ui/Canvas.cpp" \
  "$ROOT/firmware/src/ui/DemoScreen.cpp" \
  "$BUILD/llm.o" -o "$BUILD/animate"

# render <out.gif> <step> [...]
render() {
  out="$1"
  shift
  frames="$BUILD/frames"
  rm -rf "$frames"
  mkdir -p "$frames" "$(dirname "$out")"
  "$BUILD/animate" "$frames" "$@"
  "$PYTHON" - "$frames" "$out" <<'EOF'
import sys
from PIL import Image
frames, out = sys.argv[1], sys.argv[2]
names, ms = zip(*(line.split() for line in open(f"{frames}/frames.txt")))
images = [Image.open(f"{frames}/{n}").convert("P") for n in names]
images[0].save(out, save_all=True, append_images=list(images[1:]), duration=[int(m) for m in ms],
               loop=0, optimize=True)
EOF
  echo "$out"
}

if [ $# -eq 0 ]; then
  render "$ROOT/runs/preview/demo_tr.gif" tr:5:7112
  render "$ROOT/runs/preview/demo_en.gif" en:5:8926
else
  render "$@"
fi
