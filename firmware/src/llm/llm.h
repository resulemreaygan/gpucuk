// SPDX-License-Identifier: MIT
/**
 * @file llm.h
 * @brief GPU'cuk inference: a character-level Llama-style transformer with no heap and weights in flash.
 *
 * The architecture must match tools/train.py, which also writes the weights (model_data.h).
 * The API is plain C so the same code runs on the device and on the computer (tools/host).
 *
 * @author Resul Emre Aygan
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of LlmSession::pending; must be at least the model's context length (checked in llm.c). */
#define LLM_MAX_PENDING 96

/** State of one answer being generated. Fill it with llmStart(), then call llmNext() until it returns NULL. */
typedef struct {
  int pos;                       /**< Next position to write into the KV cache. */
  int pending[LLM_MAX_PENDING];  /**< Tokens still to feed through the model before sampling. */
  int pendingCount;              /**< Number of valid entries in pending. */
  int pendingIndex;              /**< Next entry of pending to feed. */
  int done;                      /**< Non-zero once the answer has ended. */
  float temperature;             /**< Softmax temperature; 0 always picks the most likely character. */
  float topp;                    /**< Nucleus (top-p) cutoff; 1 or more disables it. */
  uint64_t rng;                  /**< xorshift64* state. */
} LlmSession;

/**
 * @brief Number of languages the model was trained on.
 * @return Language count.
 */
int llmLangCount(void);

/**
 * @brief Language code of a language index.
 * @param lang Language index, in tools/train.py --data order.
 * @return Code such as "tr" or "en", or "" for an invalid index.
 */
const char* llmLangCode(int lang);

/**
 * @brief Number of built-in questions per language.
 *
 * Index i is the same question in every language.
 *
 * @return Question count.
 */
int llmPromptCount(void);

/**
 * @brief Built-in question text.
 * @param lang Language index.
 * @param index Question index.
 * @return UTF-8 question, or "" for an invalid index.
 */
const char* llmPrompt(int lang, int index);

/**
 * @brief Number of model parameters.
 * @return Parameter count.
 */
int llmParamCount(void);

/**
 * @brief Starts a new answer.
 * @param s Session to (re)initialize.
 * @param prompt UTF-8 question; every character must be in the model's vocabulary.
 * @param seed Sampling seed; the same seed, prompt and settings always give the same answer.
 * @param temperature Softmax temperature; 0 always picks the most likely character.
 * @param topp Nucleus cutoff; 1 or more disables it.
 * @return 0 on success, -1 if the prompt is too long or has unknown characters.
 */
int llmStart(LlmSession* s, const char* prompt, uint32_t seed, float temperature, float topp);

/**
 * @brief Generates the next character of the answer.
 *
 * The first call also feeds the prompt through the model, so it takes longer.
 *
 * @param s Session started with llmStart().
 * @return The character as a UTF-8 string, or NULL when the answer is finished.
 */
const char* llmNext(LlmSession* s);

#ifdef __cplusplus
}
#endif
