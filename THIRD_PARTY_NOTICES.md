# Third-party notices

GPU'cuk's own code is MIT licensed (see `LICENSE`). It builds on the following work.

| Component | Where | License |
|---|---|---|
| [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (display, board, input, IMU and power drivers for the X3) | `firmware/freeink-sdk/` (git submodule, commit `37d272d`) | MIT, see its `LICENSE` and `NOTICE` |
| [Noto Sans](https://fonts.google.com/noto/specimen/Noto+Sans) | `tools/fonts/*.ttf` | SIL Open Font License 1.1, see `tools/fonts/OFL.txt` |
| Bitmap fonts rendered from Noto Sans | `firmware/src/ui/fonts/*.h` | SIL Open Font License 1.1 (derived from Noto Sans) |

The inference code in `firmware/src/llm/` was written for this project. Its structure follows Andrej Karpathy's [llama2.c](https://github.com/karpathy/llama2.c) (MIT), and the build setup for the X3 follows [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
