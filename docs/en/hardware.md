# Hardware

[Türkçe](../tr/hardware.md)

GPU'cuk runs on an **XTEINK X3**, a pocket e-reader. The unit used here is a Developer Edition: its USB port accepts new firmware out of the box.

## What is inside

| Part | Details |
|---|---|
| SoC | ESP32-C3 (QFN32, revision v0.4 on this unit) |
| CPU | One 32-bit RISC-V core (RV32IMC) at 160 MHz. **No floating-point unit**: every `float` operation runs in software. |
| RAM | 400 KB SRAM, no PSRAM |
| Flash | 16 MB SPI NOR |
| Display | 792×528 e-paper. The controller is a UC8253 on this unit; other batches have a UC8279, and the FreeInk SDK detects which one at boot. |
| Buttons | Back, OK, Left, Right, Up, Down, plus a power button on the top edge |
| Motion sensor | QMI8658 (GPU'cuk uses its gyroscope to detect shaking) |
| USB | 4-pin magnetic pogo cable to the chip's built-in USB Serial/JTAG |

Wi-Fi, Bluetooth, the microSD slot, the real-time clock and the battery gauge are not used.

The interesting constraint is the CPU: no GPU, of course, but no FPU either. A language model is mostly multiply-adds on `float`s, and here each one is a library call.

## Opening it up for firmware (without a screwdriver)

`esptool` talks to the ESP32-C3's ROM bootloader over USB. On the test unit:

- `esptool flash-id` found the ESP32-C3 and 16 MB of flash, so USB flashing was not locked.
- `esptool get-security-info` reported **Secure Boot: disabled** and **Flash Encryption: disabled**.

Some X3s sold through third-party stores reportedly ship with USB flashing locked. GPU'cuk has only been tried on the Developer Edition unit described here.

## Flash layout

The partition table was decoded from a full backup of the factory flash. GPU'cuk keeps it exactly as it is (`firmware/partitions.csv` builds to the same table) and only ever writes the first app partition.

| Name | Offset | Size | Contents |
|---|---|---|---|
| (bootloader) | `0x0` | | ESP-IDF bootloader |
| (partition table) | `0x8000` | | |
| nvs | `0x9000` | 20 KB | Factory settings and calibration |
| otadata | `0xE000` | 8 KB | Which app slot boots (app0 on this unit) |
| **app0** | **`0x10000`** | 7.44 MB | Factory firmware; **GPU'cuk goes here** |
| app1 | `0x780000` | 7.44 MB | Second (OTA) slot, empty on this unit |
| spiffs | `0xEF0000` | 1 MB | |
| coredump | `0xFF0000` | 64 KB | |

Units can differ. On this one the factory firmware sat in app0 and otadata selected app0, so writing app0 was enough. If otadata on your unit selects app1 (one public flashing guide puts the factory app at `0x780000`), writing app0 alone would leave the factory firmware starting; GPU'cuk's scripts do not handle that case. The same goes for a unit that already runs other firmware with its own partition table.

**Never erase the whole flash** (`esptool erase-flash`): that also wipes NVS, which only a full backup of your own device can restore.

## Power and sleep

There is no real "off". The power button switches the board's supply on, and the firmware has to latch it at boot (`BoardConfig::holdPowerRails()` in the FreeInk SDK). "Off" is deep sleep. Two consequences:

- Waking with the power button does not look like a wake-up to the ESP32. It looks like a cold boot (`ESP_RST_POWERON`), so GPU'cuk starts fresh every time: Turkish, first question, seed 42.
- Firmware that never goes to sleep drains the battery. GPU'cuk sleeps after the power button is held for 2 seconds or after 5 minutes without input.

E-paper keeps its image without power, so a sleeping device still looks on. GPU'cuk draws a "sleeping" screen before it goes to sleep, so that you can tell.

## USB quirks

- The USB port only exists while the device is awake. With the factory firmware it shows up for about a second after plugging in, then disappears. The scripts in `tools/` wait for the port and connect as soon as it appears; wake the device with the power button first, then plug in the cable.
- GPU'cuk keeps USB serial running, so while it is awake the port stays up and reflashing needs no tricks.
- Opening the serial port resets the chip (USB Serial/JTAG reset), so a serial log always starts at boot.
- The cable has to carry data. The one in the box does.

## Display

- Framebuffer: 792×528, 1 bit per pixel, most significant bit first, 1 = white. GPU'cuk draws in portrait (528×792) and maps each point with `phyX = y`, `phyY = 527 − x`.
- One 52 KB framebuffer (`-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1`) leaves more RAM for the model.
- Refresh modes in the SDK: `FULL_REFRESH`, `HALF_REFRESH` (a cleaning waveform) and `FAST_REFRESH` (fast, but leaves faint marks that build up). A full refresh takes about 2 seconds on this panel.
- The SD card shares the SPI bus with the display. If earlier firmware left the SD power rail off, display commands never arrive; the boot sequence releases it first (`BoardConfig::releaseSdRail()`).

How the firmware streams text onto this panel is in [firmware.md](firmware.md).
