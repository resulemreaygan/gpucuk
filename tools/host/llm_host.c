// SPDX-License-Identifier: MIT
/**
 * @file llm_host.c
 * @brief Runs firmware/src/llm on the computer with the same weights as the device.
 *
 * With -t 0 the answers must match tools/train.py's "[t=0]" samples character for character.
 *
 * Usage: llm_host [-l tr|en] [-t temp] [-p topp] [-s seed] [-n runs] [prompt_index]
 *        (no -l = every language, no index = every prompt)
 *        Defaults match the device's normal mode: -t 1.0 -p 0.9, seeds 1 to 3.
 *
 * @author Resul Emre Aygan
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "llm.h"

static double nowSeconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void run(int lang, int index, uint32_t seed, float temp, float topp) {
  LlmSession s;
  if (llmStart(&s, llmPrompt(lang, index), seed, temp, topp) != 0) {
    printf("  [s=%u] (prompt does not fit the model)\n", seed);
    return;
  }
  char answer[512] = {0};
  int chars = 0;
  const double t0 = nowSeconds();
  const char* c;
  while ((c = llmNext(&s)) != NULL) {
    strncat(answer, c, sizeof(answer) - strlen(answer) - 1);
    chars++;
  }
  const double dt = nowSeconds() - t0;
  printf("  [s=%u] %s   (%d chars, %.2f ms)\n", seed, answer, chars, dt * 1000);
}

int main(int argc, char** argv) {
  float temp = 1.0f, topp = 0.9f;
  uint32_t seed = 1;
  int runs = 3, only = -1;
  const char* langCode = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-l") && i + 1 < argc) langCode = argv[++i];
    else if (!strcmp(argv[i], "-t") && i + 1 < argc) temp = atof(argv[++i]);
    else if (!strcmp(argv[i], "-p") && i + 1 < argc) topp = atof(argv[++i]);
    else if (!strcmp(argv[i], "-s") && i + 1 < argc) seed = strtoul(argv[++i], NULL, 10);
    else if (!strcmp(argv[i], "-n") && i + 1 < argc) runs = atoi(argv[++i]);
    else only = atoi(argv[i]);
  }
  printf("%d parameters, temp %.2f, top-p %.2f\n", llmParamCount(), temp, topp);
  for (int lang = 0; lang < llmLangCount(); lang++) {
    if (langCode && strcmp(langCode, llmLangCode(lang))) continue;
    for (int p = 0; p < llmPromptCount(); p++) {
      if (only >= 0 && p != only) continue;
      printf("[%s] > %s\n", llmLangCode(lang), llmPrompt(lang, p));
      for (int r = 0; r < runs; r++) run(lang, p, seed + r, temp, topp);
    }
  }
  return 0;
}
