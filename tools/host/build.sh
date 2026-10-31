#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds llm_host, which runs firmware/src/llm on the computer with the same weights as the device.
# The weights come from firmware/src/llm/model_data.h, which tools/train.py writes.
#
# Usage: tools/host/build.sh
#
# Author: Resul Emre Aygan
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cc -std=c11 -O2 -Wall -Wextra -I"$ROOT/firmware/src/llm" \
  "$ROOT/tools/host/llm_host.c" "$ROOT/firmware/src/llm/llm.c" -lm -o "$ROOT/tools/host/llm_host"
echo "$ROOT/tools/host/llm_host"
