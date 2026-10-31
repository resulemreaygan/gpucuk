#!/bin/sh
# SPDX-License-Identifier: MIT
# Writes a full 16 MB backup (made by backup_stock.sh) back to the X3, restoring the
# factory firmware. The backup's SHA-256 is checked first; nothing is written if it fails.
#
# Usage: tools/restore_stock.sh [backup.bin]
#        (default: backup/x3_backup.bin; the .sha256 file must sit next to it)
#
# Author: Resul Emre Aygan
set -eu
# shellcheck source=tools/common.sh
. "$(dirname "$0")/common.sh"

BACKUP="${1:-$ROOT/backup/x3_backup.bin}"
[ -f "$BACKUP" ] || die "No backup at $BACKUP."
[ -f "$BACKUP.sha256" ] || die "No checksum at $BACKUP.sha256."
EXPECTED="$(head -n 1 "$BACKUP.sha256" | cut -d ' ' -f 1)"
[ "${#EXPECTED}" -eq 64 ] || die "$BACKUP.sha256 does not hold a SHA-256; stopping."
[ "$(sha256_of "$BACKUP")" = "$EXPECTED" ] || die "The backup does not match its checksum; stopping."
echo "Checksum OK: $EXPECTED"
PORT="$(wait_for_port)"
"$ESPTOOL" --port "$PORT" --connect-attempts 10 write-flash 0x0 "$BACKUP"
