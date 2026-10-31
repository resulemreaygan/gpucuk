#!/bin/sh
# SPDX-License-Identifier: MIT
# Reads the X3's whole 16 MB flash into a backup file, verifies it against the device and writes
# its SHA-256 next to it. Run this once, before flashing anything: it is the only way back to the
# factory firmware. The backup contains the vendor's firmware and this device's calibration (NVS),
# so keep it private and never commit it (backup/ and *.bin are git-ignored).
#
# Usage: tools/backup_stock.sh [backup.bin]
#        (default: backup/x3_backup.bin)
#
# Author: Resul Emre Aygan
set -eu
# shellcheck source=tools/common.sh
. "$(dirname "$0")/common.sh"

OUT="${1:-$ROOT/backup/x3_backup.bin}"
[ ! -e "$OUT" ] || die "$OUT already exists; refusing to overwrite a backup."
mkdir -p "$(dirname "$OUT")"
PORT="$(wait_for_port)"
# no-reset keeps the chip in its bootloader, so the port is still there for the verification.
"$ESPTOOL" --port "$PORT" --baud 921600 --connect-attempts 10 --after no-reset read-flash 0x0 0x1000000 "$OUT"
"$ESPTOOL" --port "$PORT" --baud 921600 --connect-attempts 10 verify-flash 0x0 "$OUT" ||
  die "$OUT does not match the device; delete it and run this script again."
echo "$(sha256_of "$OUT")  $(basename "$OUT")" > "$OUT.sha256"
echo "Backup: $OUT"
cat "$OUT.sha256"
