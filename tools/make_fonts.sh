#!/bin/sh
# SPDX-License-Identifier: MIT
# Regenerates the firmware's three bitmap fonts from Noto Sans.
#
# Usage: tools/make_fonts.sh
#
# Author: Resul Emre Aygan
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${PYTHON:-$ROOT/.venv/bin/python}"
FONTS="$ROOT/tools/fonts"
OUT="$ROOT/firmware/src/ui/fonts"

"$PYTHON" "$ROOT/tools/make_font.py" "$FONTS/NotoSans-Bold.ttf" 36 font_title "$OUT/font_title.h"
"$PYTHON" "$ROOT/tools/make_font.py" "$FONTS/NotoSans-Regular.ttf" 30 font_body "$OUT/font_body.h"
"$PYTHON" "$ROOT/tools/make_font.py" "$FONTS/NotoSans-Regular.ttf" 20 font_small "$OUT/font_small.h"
