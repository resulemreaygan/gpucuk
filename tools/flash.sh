#!/bin/sh
# SPDX-License-Identifier: MIT
# Writes the GPU'cuk firmware to the X3's app partition (0x10000) over USB.
# Bootloader, partition table and NVS (factory calibration) are left exactly as they are.
#
# Usage: tools/flash.sh [firmware.bin]
#        (default: firmware/.pio/build/x3/firmware.bin)
#
# Author: Resul Emre Aygan
set -eu
# shellcheck source=tools/common.sh
. "$(dirname "$0")/common.sh"

BIN="${1:-$ROOT/firmware/.pio/build/x3/firmware.bin}"
[ -f "$BIN" ] || die "No firmware at $BIN (build it first: cd firmware && pio run)."
PORT="$(wait_for_port)"
"$ESPTOOL" --port "$PORT" --connect-attempts 10 write-flash 0x10000 "$BIN"
