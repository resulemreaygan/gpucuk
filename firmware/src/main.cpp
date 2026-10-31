// SPDX-License-Identifier: MIT
/**
 * @file main.cpp
 * @brief GPU'cuk demo firmware for the XTEINK X3: a tiny language model answering on e-paper.
 *
 * Controls: Left/Right pick a question, Up/Down switch the language, OK generates an answer,
 * shaking generates one with a random seed, Back toggles overclock (higher temperature), and
 * holding power for 2 s (or 5 minutes without input) puts the device to sleep.
 *
 * The answer is streamed to the screen while it is generated: each refresh runs asynchronously,
 * so the model keeps computing while the panel updates.
 *
 * @author Resul Emre Aygan
 */
#include <Arduino.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <Imu.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <SPI.h>
#include <XteinkDetect.h>
#include <esp_random.h>
#include <math.h>
#include <string.h>

#include "Version.h"
#include "llm/llm.h"
#include "ui/Canvas.h"
#include "ui/DemoScreen.h"

namespace {

// Constructor pins are ignored by FreeInk; begin() reads them from BoardConfig::ACTIVE.
EInkDisplay display(8, 10, 21, 4, 5, 6);
InputManager input;
Imu imu;
Canvas canvas;
DemoState state;
LlmSession session;

constexpr int kSpiSclk = 8;
constexpr int kSpiMiso = 7;
constexpr int kSpiMosi = 10;
constexpr int kSpiCs = 21;

constexpr unsigned long kSleepHoldMs = 2000;
constexpr unsigned long kIdleSleepMs = 5UL * 60 * 1000;
constexpr float kShakeDps = 220.0f;
constexpr unsigned long kShakeCooldownMs = 1200;
constexpr unsigned long kImuPollMs = 20;
// FAST leaves ghost marks that build up; a periodic HALF (GC waveform) clears them.
constexpr int kFastRefreshesPerHalf = 8;

constexpr uint32_t kFirstSeed = 42;
constexpr float kNormalTemp = 1.0f;
constexpr float kNormalTopp = 0.9f;
constexpr float kOverclockTemp = 1.4f;
constexpr float kOverclockTopp = 0.9f;

bool imuOk = false;
char answer[256];
size_t answerLen = 0;
uint32_t computeUs = 0;
bool screenDirty = false;
int fastRefreshes = 0;
unsigned long lastActivityMs = 0;
unsigned long lastShakeMs = 0;
unsigned long lastImuPollMs = 0;
// The press that woke the device must not count toward the sleep hold.
bool powerReleasedSinceBoot = false;

void drawState() {
  state.answer = answer;
  state.seconds = computeUs / 1e6f;
  state.tokensPerSecond = computeUs > 0 ? session.pos / state.seconds : 0;
  state.freeHeapKb = ESP.getFreeHeap() / 1024;
  drawDemoScreen(canvas, state);
}

// Starts a refresh without blocking, so the model keeps generating while the panel updates.
// On X3 only the no-shadow path overlaps, and it requires the framebuffer to stay untouched
// until the refresh is drained: every draw must be preceded by waitRefreshComplete().
void pushFrame() {
  const bool clean = ++fastRefreshes >= kFastRefreshesPerHalf;
  if (clean) fastRefreshes = 0;
  display.waitRefreshComplete();
  drawState();
  display.displayBufferAsyncNoShadow(clean ? EInkDisplay::HALF_REFRESH : EInkDisplay::FAST_REFRESH);
  screenDirty = false;
}

void clearAnswer() {
  answer[0] = '\0';
  answerLen = 0;
  state.chars = 0;
  computeUs = 0;
}

void selectPrompt(int delta) {
  const int n = llmPromptCount();
  state.promptIndex = (state.promptIndex + delta + n) % n;
  state.prompt = llmPrompt(static_cast<int>(state.lang), state.promptIndex);
  clearAnswer();
  screenDirty = true;
}

void toggleLanguage() {
  state.lang = state.lang == UiLang::Tr ? UiLang::En : UiLang::Tr;
  selectPrompt(0);
}

void setOverclock(bool on) {
  state.overclock = on;
  state.temperature = on ? kOverclockTemp : kNormalTemp;
  state.topp = on ? kOverclockTopp : kNormalTopp;
  screenDirty = true;
}

void startGeneration(uint32_t seed) {
  state.seed = seed;
  clearAnswer();
  if (llmStart(&session, state.prompt, seed, state.temperature, state.topp) != 0) {
    snprintf(answer, sizeof(answer), "%s", unknownPromptText(state.lang));
    screenDirty = true;
    return;
  }
  state.generating = true;
  state.thinking = true;
  screenDirty = true;
}

// Runs one model step; returns true when the answer just finished.
bool stepGeneration() {
  const uint32_t t0 = micros();
  const char* c = llmNext(&session);
  computeUs += micros() - t0;
  state.thinking = false;
  screenDirty = true;
  if (c) {
    const size_t len = strlen(c);
    if (answerLen + len < sizeof(answer)) {
      memcpy(answer + answerLen, c, len + 1);
      answerLen += len;
    }
    state.chars++;
    return false;
  }
  state.generating = false;
  return true;
}

// Shows the finished answer with a cleaning refresh and logs it to the serial port.
void finishGeneration() {
  display.waitRefreshComplete();
  drawState();
  display.displayBuffer(EInkDisplay::HALF_REFRESH);
  fastRefreshes = 0;
  screenDirty = false;
  Serial.printf("[gpucuk] %s prompt=%d seed=%lu temp=%.1f top-p=%.1f \"%s\" %d chars, %d tokens, %.2f s, "
                "%.1f tok/s, heap=%lu\n",
                llmLangCode(static_cast<int>(state.lang)), state.promptIndex,
                static_cast<unsigned long>(state.seed), state.temperature, state.topp, answer,
                state.chars, session.pos, state.seconds, state.tokensPerSecond,
                static_cast<unsigned long>(ESP.getFreeHeap()));
}

[[noreturn]] void goToSleep() {
  Serial.println("[gpucuk] sleep");
  display.waitRefreshComplete();
  drawSleepScreen(canvas, state.lang);
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  if (imuOk) imu.sleep();
  display.deepSleep();
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}

// Polls the gyroscope; true when the device is shaken hard enough, at most once per cooldown.
bool detectShake(unsigned long now) {
  if (!imuOk || now - lastImuPollMs < kImuPollMs) return false;
  lastImuPollMs = now;
  Imu::Sample s;
  if (!imu.read(s)) return false;
  const float dps = sqrtf(s.gx * s.gx + s.gy * s.gy + s.gz * s.gz);
  if (dps <= kShakeDps || now - lastShakeMs <= kShakeCooldownMs) return false;
  lastShakeMs = now;
  return true;
}

}  // namespace

void setup() {
  BoardConfig::holdPowerRails();
  Serial.begin(115200);

  BoardConfig::releaseSdRail();
  freeink::applyXteinkDisplayController();
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    BoardConfig::selectDevice(BoardConfig::Board::XteinkX3Uc8279);
  }

  SPI.begin(kSpiSclk, kSpiMiso, kSpiMosi, kSpiCs);
  input.begin();

  display.setDisplayX3();
  display.begin();
  canvas.attach(display.getFrameBuffer(), display.getDisplayWidth(), display.getDisplayHeight());

  imuOk = imu.begin();

  state.promptCount = llmPromptCount();
  state.params = llmParamCount();
  state.seed = kFirstSeed;
  selectPrompt(0);
  setOverclock(false);

  Serial.printf("[gpucuk] GPU'cuk %s: %d languages, %d prompts, %d parameters, imu=%d, heap=%lu\n", GPUCUK_VERSION,
                llmLangCount(), state.promptCount, state.params, imuOk,
                static_cast<unsigned long>(ESP.getFreeHeap()));

  drawState();
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  screenDirty = false;
  lastActivityMs = millis();
}

void loop() {
  const unsigned long now = millis();

  input.update();
  const bool powerDown = input.isPressed(InputManager::BTN_POWER);
  if (!powerDown) powerReleasedSinceBoot = true;
  if (powerReleasedSinceBoot && powerDown && input.getPowerButtonHeldTime() > kSleepHoldMs) goToSleep();

  const bool shaken = detectShake(now);
  if (!state.generating) {
    if (input.wasPressed(InputManager::BTN_LEFT)) {
      selectPrompt(-1);
      lastActivityMs = now;
    } else if (input.wasPressed(InputManager::BTN_RIGHT)) {
      selectPrompt(+1);
      lastActivityMs = now;
    } else if (input.wasPressed(InputManager::BTN_UP) || input.wasPressed(InputManager::BTN_DOWN)) {
      toggleLanguage();
      lastActivityMs = now;
    } else if (input.wasPressed(InputManager::BTN_BACK)) {
      setOverclock(!state.overclock);
      lastActivityMs = now;
    } else if (input.wasPressed(InputManager::BTN_CONFIRM)) {
      startGeneration(state.seed);
      lastActivityMs = now;
    } else if (shaken) {
      startGeneration(esp_random() % 10000);
      lastActivityMs = now;
    }
  }

  if (screenDirty && !display.refreshBusy()) pushFrame();

  if (state.generating) {
    if (stepGeneration()) {
      finishGeneration();
      lastShakeMs = millis();
    }
    lastActivityMs = millis();
    return;
  }

  if (now - lastActivityMs > kIdleSleepMs) goToSleep();
  delay(5);
}
