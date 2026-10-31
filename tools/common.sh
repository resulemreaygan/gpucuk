# SPDX-License-Identifier: MIT
# shellcheck shell=sh
# Shared helpers for the device scripts in tools/. Sourced, not run.
#
# Author: Resul Emre Aygan

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESPTOOL="${ESPTOOL:-$ROOT/.venv/bin/esptool}"
[ -x "$ESPTOOL" ] || ESPTOOL=esptool

die() {
  echo "$*" >&2
  exit 1
}

# Prints the SHA-256 of a file as 64 lowercase hex characters.
# Hashes are compared by hand: some `sha256sum -c` versions exit 0 on a malformed checksum file.
sha256_of() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | cut -d ' ' -f 1
  else
    sha256sum "$1" | cut -d ' ' -f 1
  fi
}

# Waits up to 3 minutes for the X3's USB serial port and prints its path.
# The X3 only shows up on USB while it is awake, so the user wakes it first.
wait_for_port() {
  echo "Wake the device with the power button, then plug in the USB cable (waiting up to 3 minutes)..." >&2
  for _ in $(seq 1 1800); do
    for p in /dev/cu.usbmodem* /dev/ttyACM*; do
      if [ -e "$p" ]; then
        echo "Port: $p" >&2
        echo "$p"
        return 0
      fi
    done
    sleep 0.1
  done
  die "No USB serial port appeared."
}
