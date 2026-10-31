// SPDX-License-Identifier: MIT
/**
 * @file DemoScreen.cpp
 * @brief Layout and texts of the demo and sleep screens.
 * @author Resul Emre Aygan
 */
#include "DemoScreen.h"

#include <stdio.h>

#include "../Version.h"
#include "fonts/font_body.h"
#include "fonts/font_small.h"
#include "fonts/font_title.h"

namespace {
constexpr int kMargin = 28;

struct Strings {
  const char* subtitle;  // %d = thousands of parameters
  const char* question;  // %d/%d
  const char* answer;
  const char* answerOverclock;
  const char* thinking;
  const char* idle;
  const char* status;
  const char* stats;  // chars, seconds, tok/s
  const char* statsPending;
  const char* ram;  // free KB
  const char* footer1;
  const char* footer2;
  const char* sleepTitle;
  const char* sleepHint;
  const char* unknownPrompt;
};

const Strings kTr = {
    "0 CUDA çekirdeği · 160 MHz · %dK parametre",
    "SORU %d/%d",
    "CEVAP",
    "CEVAP (overclock)",
    "düşünüyor…",
    "Onay'a bas, GPU'cuk düşünsün.",
    "GPU DURUMU",
    "%d karakter · %.1f sn · %.1f tok/s",
    "hız: ilk cevapta ölçülecek",
    "Boş RAM: %lu KB · GPU: yok",
    "Sol/Sağ: soru · Yukarı/Aşağı: dil",
    "Onay: üret · Salla: yeni seed · Geri: overclock",
    "GPU'cuk uyuyor",
    "Uyandırmak için güç tuşu",
    "(bu soru modelin sözlüğüne uymuyor)",
};

const Strings kEn = {
    "0 CUDA cores · 160 MHz · %dK parameters",
    "QUESTION %d/%d",
    "ANSWER",
    "ANSWER (overclock)",
    "thinking…",
    "Press OK and let GPU'cuk think.",
    "GPU STATUS",
    "%d chars · %.1f s · %.1f tok/s",
    "speed: measured on first answer",
    "Free RAM: %lu KB · GPU: none",
    "Left/Right: question · Up/Down: language",
    "OK: generate · Shake: new seed · Back: overclock",
    "GPU'cuk is sleeping",
    "Press power to wake",
    "(this question does not fit the model's vocabulary)",
};

const Strings& strings(UiLang lang) { return lang == UiLang::En ? kEn : kTr; }

int section(Canvas& c, int y, const char* title) {
  c.text(kMargin, y, title, font_small);
  c.hLine(kMargin, y + 8, c.width() - 2 * kMargin);
  return y + 8 + font_body.lineHeight;
}

// "TR  EN" at the right end of the title line, active language underlined.
void drawLangSwitch(Canvas& c, int baseline, UiLang lang) {
  const char* codes[] = {"TR", "EN"};
  constexpr int kGap = 14;
  int x = c.width() - kMargin - c.textWidth(codes[0], font_body) - kGap - c.textWidth(codes[1], font_body);
  for (int i = 0; i < 2; i++) {
    const int w = c.textWidth(codes[i], font_body);
    c.text(x, baseline, codes[i], font_body);
    if (static_cast<int>(lang) == i) c.hLine(x, baseline + 6, w, 3);
    x += w + kGap;
  }
}
}  // namespace

void drawDemoScreen(Canvas& c, const DemoState& s) {
  const Strings& t = strings(s.lang);
  char buf[256];
  const int w = c.width() - 2 * kMargin;
  c.clear();

  int y = 64;
  c.text(kMargin, y, "GPU'cuk " GPUCUK_VERSION, font_title);
  drawLangSwitch(c, y, s.lang);
  y += 34;
  snprintf(buf, sizeof(buf), t.subtitle, (s.params + 500) / 1000);
  c.text(kMargin, y, buf, font_small);
  y += 18;
  c.hLine(kMargin, y, w, 3);

  y += 56;
  snprintf(buf, sizeof(buf), t.question, s.promptIndex + 1, s.promptCount);
  y = section(c, y, buf);
  snprintf(buf, sizeof(buf), "> %s", s.prompt);
  y = c.textWrapped(kMargin, y, w, buf, font_body);

  y += 30;
  y = section(c, y, s.overclock ? t.answerOverclock : t.answer);
  if (s.thinking) {
    c.text(kMargin, y, t.thinking, font_body);
  } else if (s.generating || s.answer[0]) {
    snprintf(buf, sizeof(buf), "%s%s", s.answer, s.generating ? "_" : "");
    c.textWrapped(kMargin, y, w, buf, font_body);
  } else {
    c.textWrapped(kMargin, y, w, t.idle, font_body);
  }

  y = 578;
  c.text(kMargin, y, t.status, font_small);
  c.hLine(kMargin, y + 8, w);
  y += 8 + font_small.lineHeight + 2;
  snprintf(buf, sizeof(buf), "seed: %lu · temp: %.1f · top-p: %.1f", static_cast<unsigned long>(s.seed),
           s.temperature, s.topp);
  c.text(kMargin, y, buf, font_small);
  y += font_small.lineHeight;
  if (s.chars > 0) {
    snprintf(buf, sizeof(buf), t.stats, s.chars, s.seconds, s.tokensPerSecond);
  } else {
    snprintf(buf, sizeof(buf), "%s", t.statsPending);
  }
  c.text(kMargin, y, buf, font_small);
  y += font_small.lineHeight;
  snprintf(buf, sizeof(buf), t.ram, static_cast<unsigned long>(s.freeHeapKb));
  c.text(kMargin, y, buf, font_small);

  const int footer = c.height() - 60;
  c.hLine(kMargin, footer - 34, w);
  c.text(kMargin, footer, t.footer1, font_small);
  c.text(kMargin, footer + font_small.lineHeight, t.footer2, font_small);
}

void drawSleepScreen(Canvas& c, UiLang lang) {
  const Strings& t = strings(lang);
  c.clear();
  const int mid = c.height() / 2;
  c.textCentered(mid - 20, t.sleepTitle, font_title);
  c.textCentered(mid + 30, "zZz", font_body);
  c.textCentered(mid + 90, t.sleepHint, font_small);
}

const char* unknownPromptText(UiLang lang) { return strings(lang).unknownPrompt; }
