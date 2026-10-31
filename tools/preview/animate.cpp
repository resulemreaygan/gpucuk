// SPDX-License-Identifier: MIT
/**
 * @file animate.cpp
 * @brief Renders a GPU'cuk session frame by frame, as the device shows it, for a GIF (see gif.sh).
 *
 * The answers come from firmware/src/llm, the device's inference code and weights, and the screens
 * from the firmware's own drawing code. The loop follows firmware/src/main.cpp: while the model
 * generates, the panel is redrawn whenever it is free, every 8th refresh is a HALF refresh, and the
 * finished answer gets one more HALF refresh. The device's time is estimated from its measurements:
 *  - model time: seconds = 0.233 * n + 0.00043 * n^2 for n tokens (docs/en/model.md);
 *  - FAST refresh 0.381 s and HALF refresh 0.458 s (serial log);
 *  - drawing and sending a frame 0.15 s, fitted to a video of the device: for seeds 713 and 709
 *    the time from "thinking" to the finished answer (16.9 s and 24.15 s) matches within 0.1 s.
 *
 * Usage: animate <out_dir> <lang>:<prompt_index>:<seed> [...]
 *        e.g. animate frames tr:5:7112 en:5:8926
 * Each step switches to the language and question, then generates an answer with the seed: the
 * current seed means pressing OK, any other seed means shaking the device. The session starts right
 * after boot (seed 42). Writes frame_NNN.pbm and frames.txt ("<file> <milliseconds>" per frame).
 *
 * @author Resul Emre Aygan
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llm/llm.h"
#include "pbm.h"
#include "ui/Canvas.h"
#include "ui/DemoScreen.h"

namespace {
constexpr float kFastRefreshS = 0.381f;
constexpr float kHalfRefreshS = 0.458f;
constexpr float kPushS = 0.15f;
constexpr int kFastRefreshesPerHalf = 8;  // as in firmware/src/main.cpp
constexpr float kIdleS = 1.5f;            // time on a screen before the next button press
constexpr float kHoldS = 3.0f;            // time a finished answer stays before the next step
constexpr uint32_t kFirstSeed = 42;
constexpr uint32_t kBootHeapKb = 151224 / 1024;  // free heap at boot (serial log)
constexpr uint32_t kRunHeapKb = 150300 / 1024;   // free heap from the first answer on (serial log)
constexpr int kMaxFrames = 1000;

uint8_t fb[kPanelW / 8 * kPanelH];
Canvas canvas;
const char* outDir = ".";
float frameTimes[kMaxFrames];
int frameCount = 0;
float now = 0;           // device time in seconds
float panelFreeAt = 0;   // when the current refresh ends
int fastRefreshes = 0;
char answer[256];

float modelSeconds(int tokens) { return 0.233f * tokens + 0.00043f * tokens * tokens; }

// Draws the state and saves it as the frame that appears at the current time.
void show(const DemoState& s) {
  if (frameCount == kMaxFrames) {
    fprintf(stderr, "too many frames\n");
    exit(1);
  }
  drawDemoScreen(canvas, s);
  char path[512];
  snprintf(path, sizeof(path), "%s/frame_%03d.pbm", outDir, frameCount);
  writePbm(path, canvas, fb);
  frameTimes[frameCount++] = now;
}

// pushFrame() in main.cpp: draw, send, then refresh without blocking.
void pushFrame(const DemoState& s) {
  const bool clean = ++fastRefreshes >= kFastRefreshesPerHalf;
  if (clean) fastRefreshes = 0;
  if (now < panelFreeAt) now = panelFreeAt;
  now += kPushS;
  show(s);
  panelFreeAt = now + (clean ? kHalfRefreshS : kFastRefreshS);
}

void clearAnswer(DemoState& s) {
  answer[0] = '\0';
  s.answer = answer;
  s.chars = 0;
  s.seconds = 0;
  s.tokensPerSecond = 0;
}

// Left/Right and Up/Down in main.cpp: a new question or language clears the answer.
void selectPrompt(DemoState& s, UiLang lang, int index) {
  if (s.lang == lang && s.promptIndex == index) return;
  s.lang = lang;
  s.promptIndex = index;
  s.prompt = llmPrompt(static_cast<int>(lang), index);
  clearAnswer(s);
  pushFrame(s);
  now += kIdleS;
}

// startGeneration(), stepGeneration() and finishGeneration() in main.cpp, with estimated time.
void generate(DemoState& s, uint32_t seed) {
  LlmSession session;
  s.seed = seed;
  clearAnswer(s);
  if (llmStart(&session, s.prompt, seed, s.temperature, s.topp) != 0) {
    fprintf(stderr, "question does not fit the model: %s\n", s.prompt);
    exit(1);
  }
  s.generating = true;
  s.thinking = true;
  s.freeHeapKb = kRunHeapKb;
  size_t len = 0;
  bool dirty = true;
  for (;;) {
    if (dirty && now >= panelFreeAt) {
      pushFrame(s);
      dirty = false;
    }
    const int before = session.pos;
    const char* c = llmNext(&session);
    const float dt = modelSeconds(session.pos) - modelSeconds(before);
    now += dt;
    s.seconds += dt;
    s.tokensPerSecond = session.pos / s.seconds;
    s.thinking = false;
    dirty = true;
    if (!c) break;
    const size_t n = strlen(c);
    if (len + n < sizeof(answer)) {
      memcpy(answer + len, c, n + 1);
      len += n;
    }
    s.chars++;
  }
  s.generating = false;
  if (now < panelFreeAt) now = panelFreeAt;
  now += kPushS;
  show(s);
  now += kHalfRefreshS;  // blocking HALF refresh
  panelFreeAt = now;
  fastRefreshes = 0;
  printf("%s %d seed %lu: \"%s\" %d chars, %d tokens, %.2f s model time\n", llmLangCode(static_cast<int>(s.lang)),
         s.promptIndex, static_cast<unsigned long>(seed), answer, s.chars, session.pos, s.seconds);
  now += kHoldS;
}

bool parseStep(const char* arg, UiLang* lang, int* index, uint32_t* seed) {
  char code[3];
  unsigned long sd;
  if (sscanf(arg, "%2[a-z]:%d:%lu", code, index, &sd) != 3) return false;
  *seed = static_cast<uint32_t>(sd);
  for (int i = 0; i < llmLangCount(); i++) {
    if (!strcmp(code, llmLangCode(i))) {
      *lang = static_cast<UiLang>(i);
      return *index >= 0 && *index < llmPromptCount();
    }
  }
  return false;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <out_dir> <lang>:<prompt_index>:<seed> [...]\n", argv[0]);
    return 2;
  }
  outDir = argv[1];
  canvas.attach(fb, kPanelW, kPanelH);

  UiLang lang;
  int index;
  uint32_t seed;
  if (!parseStep(argv[2], &lang, &index, &seed)) {
    fprintf(stderr, "bad step: %s\n", argv[2]);
    return 2;
  }
  // Right after boot, on the first step's question.
  DemoState s;
  s.lang = lang;
  s.promptIndex = index;
  s.prompt = llmPrompt(static_cast<int>(lang), index);
  s.promptCount = llmPromptCount();
  s.params = llmParamCount();
  s.seed = kFirstSeed;
  s.freeHeapKb = kBootHeapKb;
  clearAnswer(s);
  show(s);
  now += kIdleS;

  for (int i = 2; i < argc; i++) {
    if (!parseStep(argv[i], &lang, &index, &seed)) {
      fprintf(stderr, "bad step: %s\n", argv[i]);
      return 2;
    }
    selectPrompt(s, lang, index);
    generate(s, seed);
  }

  char path[512];
  snprintf(path, sizeof(path), "%s/frames.txt", outDir);
  FILE* f = fopen(path, "w");
  if (!f) {
    perror(path);
    return 1;
  }
  for (int i = 0; i < frameCount; i++) {
    const float end = i + 1 < frameCount ? frameTimes[i + 1] : now;
    fprintf(f, "frame_%03d.pbm %d\n", i, static_cast<int>((end - frameTimes[i]) * 1000 + 0.5f));
  }
  fclose(f);
  return 0;
}
