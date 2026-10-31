// SPDX-License-Identifier: MIT
/**
 * @file preview.cpp
 * @brief Renders the firmware's screens on the computer into PBM files (see build.sh).
 *
 * Every answer is a real output of the v0.4 model for the seed shown. The Turkish answers, their
 * timings and the free RAM values come from the device's serial log. The overclock and English
 * answers come from tools/host/llm_host, which gives the same answers as the device; their timings,
 * and the one of the half-finished answer, are estimated from the device's measured speed.
 *
 * Usage: preview [out_dir]
 *
 * @author Resul Emre Aygan
 */
#include <stdio.h>
#include <stdlib.h>

#include "pbm.h"
#include "ui/Canvas.h"
#include "ui/DemoScreen.h"

namespace {
uint8_t fb[kPanelW / 8 * kPanelH];

// Writes the canvas to <dir>/<name>.pbm.
void writeScreen(const char* dir, const char* name, const Canvas& c) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.pbm", dir, name);
  writePbm(path, c, fb);
}
}  // namespace

int main(int argc, char** argv) {
  const char* outDir = argc > 1 ? argv[1] : ".";
  Canvas c;
  c.attach(fb, kPanelW, kPanelH);

  // After boot: first seed, normal mode.
  DemoState demo;
  demo.prompt = "GPU ver";
  demo.promptIndex = 2;
  demo.promptCount = 9;
  demo.params = 141696;
  demo.seed = 42;
  demo.freeHeapKb = 147;
  drawDemoScreen(c, demo);
  writeScreen(outDir, "demo_idle", c);

  demo.thinking = true;
  demo.generating = true;
  drawDemoScreen(c, demo);
  writeScreen(outDir, "demo_thinking", c);

  // Seed 42 gives "Önce kitapları bitir. Sonra belki H100."; here after 23 of its 39 characters.
  demo.thinking = false;
  demo.answer = "Önce kitapları bitir. S";
  demo.chars = 23;
  demo.seconds = 8.2f;
  demo.tokensPerSecond = 4.0f;
  demo.freeHeapKb = 146;
  drawDemoScreen(c, demo);
  writeScreen(outDir, "demo_generating", c);

  demo.generating = false;
  demo.prompt = "mdisec ne dedi?";
  demo.promptIndex = 5;
  demo.seed = 4813;
  demo.answer = "mdisec haklıydı. Şimdi o izliyor, ben üretiyorum.";
  demo.chars = 49;
  demo.seconds = 17.8f;
  demo.tokensPerSecond = 3.8f;
  drawDemoScreen(c, demo);
  writeScreen(outDir, "demo_answer", c);

  demo.overclock = true;
  demo.temperature = 1.4f;
  demo.topp = 0.9f;
  demo.prompt = "Kaç CUDA çekirdeğin var?";
  demo.promptIndex = 3;
  demo.seed = 14;
  demo.answer = "CUDA çekirdeğim yok, e-ink çekirdeğim var.";
  demo.chars = 42;
  demo.seconds = 18.4f;
  demo.tokensPerSecond = 3.8f;
  drawDemoScreen(c, demo);
  writeScreen(outDir, "demo_overclock", c);

  // English right after boot, then one answer.
  DemoState en;
  en.lang = UiLang::En;
  en.prompt = "Who are you?";
  en.promptIndex = 1;
  en.promptCount = 9;
  en.params = 141696;
  en.seed = 42;
  en.freeHeapKb = 147;
  drawDemoScreen(c, en);
  writeScreen(outDir, "demo_en_idle", c);

  en.seed = 23;
  en.answer = "I'm not an e-reader. No tensor cores, all books.";
  en.chars = 48;
  en.seconds = 16.7f;
  en.tokensPerSecond = 3.8f;
  en.freeHeapKb = 146;
  drawDemoScreen(c, en);
  writeScreen(outDir, "demo_en", c);

  drawSleepScreen(c, UiLang::Tr);
  writeScreen(outDir, "sleep_tr", c);
  drawSleepScreen(c, UiLang::En);
  writeScreen(outDir, "sleep_en", c);
  return 0;
}
