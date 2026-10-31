# How it was built

[Türkçe](../tr/journey.md)

The goal was strict: the e-reader generates the text **on its own CPU**, character by character, from a model whose weights live on the device. No computer or phone takes part in an answer, and the screen shows the evidence: parameter count, seed and speed. The fallback plans (a smaller int8 model; or the model on a phone with the e-reader as a labelled display) were never needed.

This repository contains v0.4. The earlier versions are only described here.

## Before touching the device

- llama2.c-style models had already run on ESP32 boards: on ESP32-S3s with PSRAM ([esp32-llm](https://github.com/DaveBben/esp32-llm)) and on a classic ESP32 without PSRAM, reading the weights from flash ([CircuitDigest](https://circuitdigest.com/articles/running-260K-tiny-llm-on-esp32)). The X3's ESP32-C3 is the weakest of these: one core, no SIMD, no FPU.
- The estimate on paper: the weights must stay in flash, the context must be short so that the KV cache fits in RAM, and with software floating point a model of this size would make about 3 tokens per second.
- A ready-made model such as llama2.c's 260K-parameter TinyStories model tells children's stories in English. GPU'cuk was to tell its own jokes in Turkish, so it got its own tiny model, trained from scratch, with characters as tokens.

## M0: tools, first connection, backup

- With the factory firmware the X3's USB port appears for about a second and disappears. What worked: poll for the port every 0.1 s and connect esptool the moment it appears, with `--after no-reset`; that leaves the chip in its ROM bootloader, and the port stays.
- `esptool flash-id`: ESP32-C3, 16 MB of flash. `get-security-info`: Secure Boot and flash encryption disabled. Custom firmware was possible.
- A full 16 MB backup (72 seconds), checked against the device with `verify-flash`.
- The partition table and NVS were decoded from the backup: the factory firmware lives in app0 at `0x10000`, and NVS holds factory settings including button calibration. From then on, only the app partition was ever written.

## M1: firmware without a model

- PlatformIO and the FreeInk SDK (the one CrossPoint Reader uses), with the same build setup as CrossPoint.
- Turkish text through bitmap fonts rendered from Noto Sans; all six buttons, shake detection, sleep and wake.
- Learned along the way, partly from CrossPoint's and the SDK's source, partly on the device:
  - Waking with the power button looks like a cold boot to the ESP32.
  - The SD card's power rail has to be released before the display answers.
  - On the X3, the SDK's asynchronous refresh silently blocks; only the "no shadow" variant really runs in the background.
  - Fast refreshes leave marks that build up, so every few fast refreshes need a cleaning half refresh.
  - A shake by hand measured about 310–320 °/s, so a 300 °/s threshold was too tight; it became 220 °/s.
- Free heap with display, IMU and USB running: 229,604 bytes (224 KB). That was the budget for the model.

## M2: data, training and inference on the computer

- The first dataset: 9 questions and 88 Turkish answers, written in the voice of a tiny device that thinks it is a giant GPU.
- `tools/train.py` trained a 141,312-parameter model on the Mac's CPU.
- `llm.c`, written for this model, ran on the Mac first. At temperature 0 it gave exactly PyTorch's answers for all 9 questions.

## M3: on the device

The first run on the e-reader (v0.2) made **3.9 tokens per second**, in line with the estimate, with about 147 KB of heap left. A 43-character answer took 13.2 seconds. The planned int8 speed-up was not needed.

## M4: interface and versions

| Version | Change | Parameters | Normal mode | Overclock |
|---|---|---|---|---|
| v0.2 | First demo on the device, Turkish | 141,312 | temp 1.2, top-p 0.9 | – |
| v0.3 | Turkish and English in one model, overclock mode | 141,504 | temp 1.2, top-p 0.9 | temp 1.6, top-p off |
| v0.4 | Template data and dropout: fewer memorized answers | 141,696 | temp 1.0, top-p 0.9 | temp 1.4, top-p 0.9 |

v0.3 worked, but in normal mode every one of 720 sampled answers was a line from the dataset, word for word. The [memorization experiment](memorization.md) tried to change that, and its best recipe became v0.4.

## Not done

- int8 weights and integer arithmetic (weights a quarter of the size, and probably much faster); the demo did not need them.
- Remembering the language across sleep; that would mean writing to the factory NVS.

## Lessons

- **Estimate, then measure.** The paper estimate (about 3 tokens per second) was close to the measurement (3.9), and the optimization planned "just in case" turned out to be unnecessary.
- **RAM was the budget, not flash.** 16 MB of flash holds the weights easily; the KV cache had to fit next to the framebuffer, the USB stack and the SDK. Grouped-query attention halved it.
- **Read the driver.** "Asynchronous" did not mean asynchronous on this panel; the reason was in the driver source.
- **A tiny model memorizes.** Training it less or with more dropout only made it worse; more varied data plus a little dropout made it combine phrases instead.
- **Make the computer reproduce the device.** One C file and one random number generator for both; every answer the device logged can be reproduced and debugged on the computer.
- **Never erase what you cannot restore.** Back up first, write only the app partition, and check checksums yourself: a `sha256sum -c` was found exiting with success on a malformed checksum file.
