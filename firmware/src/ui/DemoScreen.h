// SPDX-License-Identifier: MIT
/**
 * @file DemoScreen.h
 * @brief The GPU'cuk demo screen and sleep screen, in Turkish and English.
 * @author Resul Emre Aygan
 */
#pragma once
#include <stdint.h>

#include "Canvas.h"

/** Interface language; values match the model's language order (tools/train.py --data: tr, en). */
enum class UiLang : uint8_t { Tr = 0, En = 1 };

/** Everything the demo screen shows. */
struct DemoState {
  UiLang lang = UiLang::Tr;   /**< Interface and question language. */
  const char* prompt = "";    /**< Current question. */
  int promptIndex = 0;        /**< Index of the current question. */
  int promptCount = 0;        /**< Questions per language. */
  const char* answer = "";    /**< Answer so far. */
  bool thinking = false;      /**< Reading the question; no character yet. */
  bool generating = false;    /**< An answer is being generated. */
  bool overclock = false;     /**< High temperature: the model starts to babble. */
  uint32_t seed = 42;         /**< Sampling seed of the current answer. */
  float temperature = 1.0f;   /**< Sampling temperature. */
  float topp = 0.9f;          /**< Nucleus (top-p) cutoff. */
  int params = 0;             /**< Model parameter count. */
  int chars = 0;              /**< Characters in the answer so far. */
  float seconds = 0;          /**< Model compute time for the current answer. */
  float tokensPerSecond = 0;  /**< Question + answer tokens through the model per second. */
  uint32_t freeHeapKb = 0;    /**< Free RAM. */
};

/**
 * @brief Draws the demo screen.
 * @param c Canvas to draw on (cleared first).
 * @param s What to show.
 */
void drawDemoScreen(Canvas& c, const DemoState& s);

/**
 * @brief Draws the screen that stays visible while the device sleeps.
 * @param c Canvas to draw on (cleared first).
 * @param lang Interface language.
 */
void drawSleepScreen(Canvas& c, UiLang lang);

/**
 * @brief Text shown instead of an answer when a question does not fit the model.
 * @param lang Interface language.
 * @return UTF-8 text.
 */
const char* unknownPromptText(UiLang lang);
