# Build and flash

[Türkçe](../tr/flashing.md)

> **At your own risk.** GPU'cuk replaces your e-reader's firmware until you write the factory firmware back. It has been tried on one XTEINK X3 Developer Edition, with the factory partition table described in [hardware.md](hardware.md). **Back up your device first (step 2); that backup is the only way back.**

## What you need

- An XTEINK X3 whose USB port accepts firmware (Developer Edition), and the magnetic cable from the box.
- macOS (tested). The scripts also look for Linux serial ports (`/dev/ttyACM*`), but have not been run on Linux.
- git, Python 3.12 and a C compiler.

```sh
git clone --recursive https://github.com/resulemreaygan/gpucuk.git
cd gpucuk
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

`--recursive` also fetches the FreeInk SDK (`firmware/freeink-sdk`). If you cloned without it, run `git submodule update --init`.

## 1. Connect

Wake the device with the power button, **then** plug in the cable. With the factory firmware the USB port only shows up for about a second; the backup, flash and restore scripts below wait up to 3 minutes for it and connect the moment it appears.

## 2. Back up the whole flash

```sh
tools/backup_stock.sh
```

This reads all 16 MB into `backup/x3_backup.bin`, verifies the file against the device's flash (`esptool verify-flash`) and only then writes its SHA-256 next to it (`backup/x3_backup.bin.sha256`). Reading took 72 seconds on the test unit. The script never writes to the device, and it refuses to overwrite an existing backup.

**Keep the backup private.** It contains the vendor's firmware and your own device's calibration. Copy it somewhere outside the project too. `backup/` and `*.bin` are git-ignored so that it cannot be committed by accident.

## 3. Build

```sh
cd firmware
../.venv/bin/pio run
cd ..
```

The first build downloads the toolchain (about 5 minutes on the development machine); later builds take seconds. The result is `firmware/.pio/build/x3/firmware.bin`, about 1 MB.

## 4. Flash

```sh
tools/flash.sh
```

This writes only the app partition at `0x10000`. The bootloader, the partition table and NVS (factory settings and calibration) stay exactly as they were. esptool restarts the device when it is done, and GPU'cuk starts.

- Do not use `esptool erase-flash`: it would wipe NVS too.
- If the factory firmware still starts after flashing, your unit probably boots from the second app slot (see [hardware.md](hardware.md)). GPU'cuk's scripts do not handle that case; restore your backup (below) if anything looks wrong.
- `tools/flash.sh path/to/other.bin` flashes another build, such as an older version.

## 5. Watch it (optional)

```sh
.venv/bin/python tools/serial_log.py --seconds 120 --log runs/logs/demo.log
```

Opening the port restarts the device, so the log starts from boot. The log format is in [firmware.md](firmware.md#serial-log).

## Back to the factory firmware

```sh
tools/restore_stock.sh                         # uses backup/x3_backup.bin
tools/restore_stock.sh path/to/x3_backup.bin   # or another backup with its .sha256 next to it
```

The script checks the backup against its SHA-256 first and stops without writing anything if they differ. Then it writes the full 16 MB back from address `0x0`.

On the test unit the backup was verified against the device with `verify-flash`, but it has not had to be written back yet.

## Without a device

Three things work on any computer, with a C/C++ compiler and the Python environment from above:

```sh
tools/host/build.sh && tools/host/llm_host -t 1.0 -p 0.9   # the model, with the device's settings
tools/preview/build.sh                                     # the screens, as PNGs in runs/preview/
tools/preview/gif.sh                                       # the README's GIFs: runs/preview/demo_*.gif
```

`llm_host` gives the same answers as the device for the same seed and settings. `gif.sh` runs the device's inference and drawing code together and times the frames like the device (the model's measured speed, the panel's refresh times); the GIFs in the README are made this way. Its options are in [training.md](training.md#checking-a-new-model).
