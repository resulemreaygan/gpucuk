#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prints the X3's serial output with timestamps, optionally appending it to a file.

The port is found automatically and re-opened whenever it disappears (sleep, unplug, reflash).
Opening the port resets the ESP32-C3 (USB-Serial/JTAG), so the log starts from boot.

Usage:
    serial_log.py [--seconds 60] [--log runs/logs/demo.log] [--port /dev/cu.usbmodem1101]

Author: Resul Emre Aygan
"""
from __future__ import annotations

import argparse
import glob
import time
from pathlib import Path
from typing import TextIO

import serial

__author__ = "Resul Emre Aygan"

PORT_PATTERNS = ("/dev/cu.usbmodem*", "/dev/ttyACM*")
BAUD_RATE = 115200


def find_port() -> str | None:
    """Returns the first USB serial port that looks like the X3, or None."""
    for pattern in PORT_PATTERNS:
        ports = sorted(glob.glob(pattern))
        if ports:
            return ports[0]
    return None


def emit(line: str, logfile: TextIO | None) -> None:
    """Prints one line with a timestamp and mirrors it to the log file.

    Args:
        line: Text without a trailing newline.
        logfile: Open log file, or None.
    """
    out = f"{time.strftime('%H:%M:%S')} {line}"
    print(out, flush=True)
    if logfile:
        logfile.write(out + "\n")
        logfile.flush()


def follow(port: str, end: float, logfile: TextIO | None) -> None:
    """Reads lines from an open port until ``end`` or until the port fails.

    Args:
        port: Serial device path.
        end: ``time.time()`` value at which to stop.
        logfile: Open log file, or None.

    Raises:
        serial.SerialException: If the port drops.
    """
    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD_RATE
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    emit(f"--- connected {port}", logfile)
    buf = b""
    try:
        while time.time() < end:
            chunk = s.read(256)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                emit(line.decode("utf-8", "replace").rstrip("\r"), logfile)
    finally:
        s.close()


def main() -> None:
    """Parses the command line and logs until the time is up."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--seconds", type=float, default=60, help="how long to log")
    ap.add_argument("--log", type=Path, help="append the output to this file")
    ap.add_argument("--port", help="serial port (default: first /dev/cu.usbmodem* or /dev/ttyACM*)")
    args = ap.parse_args()

    logfile: TextIO | None = None
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        logfile = args.log.open("a", encoding="utf-8")
    end = time.time() + args.seconds
    try:
        while time.time() < end:
            port = args.port or find_port()
            if not port:
                time.sleep(0.1)
                continue
            try:
                follow(port, end, logfile)
            except (serial.SerialException, OSError) as e:
                emit(f"--- port lost ({e.__class__.__name__})", logfile)
                time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        if logfile:
            logfile.close()


if __name__ == "__main__":
    main()
