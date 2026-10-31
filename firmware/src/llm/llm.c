// SPDX-License-Identifier: MIT
/**
 * @file llm.c
 * @brief Forward pass and sampler for the model in model_data.h.
 *
 * Token embedding, then per layer: RMSNorm, grouped-query attention with interleaved RoPE and a
 * KV cache, RMSNorm, SwiGLU feed-forward; final RMSNorm and a classifier tied to the embedding.
 * All buffers are static (about 76 KB of RAM, 72 KB of it the KV cache); the weights are read
 * straight from flash.
 *
 * @author Resul Emre Aygan
 */
#include "llm.h"

#include <math.h>
#include <string.h>

#include "model_data.h"

#define HEAD_SIZE (LLM_DIM / LLM_N_HEADS)
#define KV_DIM (LLM_N_KV_HEADS * HEAD_SIZE)
#define KV_MUL (LLM_N_HEADS / LLM_N_KV_HEADS)

_Static_assert(LLM_SEQ_LEN <= LLM_MAX_PENDING, "LlmSession.pending too small for the model's context");
_Static_assert(LLM_N_HEADS % LLM_N_KV_HEADS == 0, "heads must be a multiple of kv heads");

typedef struct {
  const float* rmsAtt;
  const float* wq;
  const float* wk;
  const float* wv;
  const float* wo;
  const float* rmsFfn;
  const float* w1;
  const float* w2;
  const float* w3;
} Layer;

static Layer layers[LLM_N_LAYERS];
static const float* tokEmb;
static const float* rmsFinal;
static int mapped;
static int newlineToken = -1;
static int32_t vocabCodepoint[LLM_VOCAB_SIZE];
static float ropeFreq[HEAD_SIZE / 2];

static float x[LLM_DIM], xb[LLM_DIM], xb2[LLM_DIM], q[LLM_DIM];
static float hb[LLM_HIDDEN_DIM], hb2[LLM_HIDDEN_DIM];
static float att[LLM_SEQ_LEN];
static float logits[LLM_VOCAB_SIZE];
static float ropeCos[HEAD_SIZE / 2], ropeSin[HEAD_SIZE / 2];
static float keyCache[LLM_N_LAYERS][LLM_SEQ_LEN][KV_DIM];
static float valueCache[LLM_N_LAYERS][LLM_SEQ_LEN][KV_DIM];

// Decodes one UTF-8 sequence; returns bytes consumed (0 at end of string).
static int utf8Decode(const char* s, int32_t* cp) {
  const unsigned char* u = (const unsigned char*)s;
  if (u[0] == 0) return 0;
  if (u[0] < 0x80) { *cp = u[0]; return 1; }
  if ((u[0] & 0xE0) == 0xC0 && u[1]) { *cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); return 2; }
  if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) {
    *cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
    return 3;
  }
  *cp = -1;
  return 1;
}

// Points the layer structs into the flat weight array, in the order tools/train.py exports them.
static void mapWeights(void) {
  const float* p = kLlmWeights;
  tokEmb = p;
  p += LLM_VOCAB_SIZE * LLM_DIM;
  for (int l = 0; l < LLM_N_LAYERS; l++) {
    Layer* L = &layers[l];
    L->rmsAtt = p; p += LLM_DIM;
    L->wq = p; p += LLM_DIM * LLM_DIM;
    L->wk = p; p += KV_DIM * LLM_DIM;
    L->wv = p; p += KV_DIM * LLM_DIM;
    L->wo = p; p += LLM_DIM * LLM_DIM;
    L->rmsFfn = p; p += LLM_DIM;
    L->w1 = p; p += LLM_HIDDEN_DIM * LLM_DIM;
    L->w2 = p; p += LLM_DIM * LLM_HIDDEN_DIM;
    L->w3 = p; p += LLM_HIDDEN_DIM * LLM_DIM;
  }
  rmsFinal = p;

  for (int i = 0; i < HEAD_SIZE / 2; i++) {
    ropeFreq[i] = powf(LLM_ROPE_THETA, -(2.0f * i) / HEAD_SIZE);
  }
  vocabCodepoint[0] = -1;
  for (int i = 1; i < LLM_VOCAB_SIZE; i++) {
    utf8Decode(kLlmVocab[i], &vocabCodepoint[i]);
    if (vocabCodepoint[i] == '\n') newlineToken = i;
  }
  mapped = 1;
}

static int tokenFor(int32_t cp) {
  for (int i = 1; i < LLM_VOCAB_SIZE; i++) {
    if (vocabCodepoint[i] == cp) return i;
  }
  return -1;
}

static void rmsnorm(float* out, const float* in, const float* w) {
  float ss = 0.0f;
  for (int i = 0; i < LLM_DIM; i++) ss += in[i] * in[i];
  ss = 1.0f / sqrtf(ss / LLM_DIM + LLM_NORM_EPS);
  for (int i = 0; i < LLM_DIM; i++) out[i] = w[i] * (ss * in[i]);
}

// out[d] = W[d][n] · in[n]
static void matmul(float* out, const float* in, const float* w, int n, int d) {
  for (int i = 0; i < d; i++) {
    const float* row = w + i * n;
    float sum = 0.0f;
    for (int j = 0; j < n; j++) sum += row[j] * in[j];
    out[i] = sum;
  }
}

static void softmax(float* v, int n) {
  float maxVal = v[0];
  for (int i = 1; i < n; i++) if (v[i] > maxVal) maxVal = v[i];
  float sum = 0.0f;
  for (int i = 0; i < n; i++) { v[i] = expf(v[i] - maxVal); sum += v[i]; }
  for (int i = 0; i < n; i++) v[i] /= sum;
}

// Rotates (even, odd) pairs of every head by the angles of the current position.
static void rope(float* v, int heads) {
  for (int h = 0; h < heads; h++) {
    float* vh = v + h * HEAD_SIZE;
    for (int j = 0; j < HEAD_SIZE / 2; j++) {
      const float v0 = vh[2 * j], v1 = vh[2 * j + 1];
      vh[2 * j] = v0 * ropeCos[j] - v1 * ropeSin[j];
      vh[2 * j + 1] = v0 * ropeSin[j] + v1 * ropeCos[j];
    }
  }
}

// Runs one token through the model, appends its keys and values to the cache, fills logits.
static void forward(int token, int pos) {
  memcpy(x, tokEmb + token * LLM_DIM, sizeof(x));
  for (int j = 0; j < HEAD_SIZE / 2; j++) {
    const float angle = pos * ropeFreq[j];
    ropeCos[j] = cosf(angle);
    ropeSin[j] = sinf(angle);
  }
  const float scale = 1.0f / sqrtf((float)HEAD_SIZE);

  for (int l = 0; l < LLM_N_LAYERS; l++) {
    const Layer* L = &layers[l];
    float* k = keyCache[l][pos];
    float* v = valueCache[l][pos];

    rmsnorm(xb, x, L->rmsAtt);
    matmul(q, xb, L->wq, LLM_DIM, LLM_DIM);
    matmul(k, xb, L->wk, LLM_DIM, KV_DIM);
    matmul(v, xb, L->wv, LLM_DIM, KV_DIM);
    rope(q, LLM_N_HEADS);
    rope(k, LLM_N_KV_HEADS);

    for (int h = 0; h < LLM_N_HEADS; h++) {
      const float* qh = q + h * HEAD_SIZE;
      const int kvOff = (h / KV_MUL) * HEAD_SIZE;
      for (int t = 0; t <= pos; t++) {
        const float* kt = keyCache[l][t] + kvOff;
        float score = 0.0f;
        for (int i = 0; i < HEAD_SIZE; i++) score += qh[i] * kt[i];
        att[t] = score * scale;
      }
      softmax(att, pos + 1);
      float* out = xb + h * HEAD_SIZE;
      memset(out, 0, HEAD_SIZE * sizeof(float));
      for (int t = 0; t <= pos; t++) {
        const float* vt = valueCache[l][t] + kvOff;
        for (int i = 0; i < HEAD_SIZE; i++) out[i] += att[t] * vt[i];
      }
    }
    matmul(xb2, xb, L->wo, LLM_DIM, LLM_DIM);
    for (int i = 0; i < LLM_DIM; i++) x[i] += xb2[i];

    rmsnorm(xb, x, L->rmsFfn);
    matmul(hb, xb, L->w1, LLM_DIM, LLM_HIDDEN_DIM);
    matmul(hb2, xb, L->w3, LLM_DIM, LLM_HIDDEN_DIM);
    for (int i = 0; i < LLM_HIDDEN_DIM; i++) {
      const float a = hb[i];
      hb[i] = a / (1.0f + expf(-a)) * hb2[i];
    }
    matmul(xb, hb, L->w2, LLM_HIDDEN_DIM, LLM_DIM);
    for (int i = 0; i < LLM_DIM; i++) x[i] += xb[i];
  }

  rmsnorm(x, x, rmsFinal);
  matmul(logits, x, tokEmb, LLM_DIM, LLM_VOCAB_SIZE);
}

// xorshift64*: small, fast and identical on every platform.
static uint32_t randomU32(uint64_t* state) {
  *state ^= *state >> 12;
  *state ^= *state << 25;
  *state ^= *state >> 27;
  return (uint32_t)((*state * 0x2545F4914F6CDD1DULL) >> 32);
}

static float randomF32(uint64_t* state) { return (randomU32(state) >> 8) / 16777216.0f; }

static int sample(LlmSession* s) {
  logits[0] = -1e30f;  // never emit BOS
  if (s->temperature <= 0.0f) {
    int best = 1;
    for (int i = 2; i < LLM_VOCAB_SIZE; i++) if (logits[i] > logits[best]) best = i;
    return best;
  }
  for (int i = 0; i < LLM_VOCAB_SIZE; i++) logits[i] /= s->temperature;
  softmax(logits, LLM_VOCAB_SIZE);

  static int order[LLM_VOCAB_SIZE];
  int n = 0;
  if (s->topp < 1.0f) {
    // Characters below the cutoff can never be part of the nucleus, so they are not sorted.
    const float cutoff = (1.0f - s->topp) / (LLM_VOCAB_SIZE - 1);
    for (int i = 0; i < LLM_VOCAB_SIZE; i++) {
      if (logits[i] < cutoff) continue;
      int j = n++;
      while (j > 0 && logits[order[j - 1]] < logits[i]) { order[j] = order[j - 1]; j--; }
      order[j] = i;
    }
    float cum = 0.0f;
    int last = n - 1;
    for (int i = 0; i < n; i++) {
      cum += logits[order[i]];
      if (cum > s->topp) { last = i; break; }
    }
    n = last + 1;
  } else {
    for (int i = 0; i < LLM_VOCAB_SIZE; i++) order[n++] = i;
  }

  float total = 0.0f;
  for (int i = 0; i < n; i++) total += logits[order[i]];
  const float r = randomF32(&s->rng) * total;
  float cum = 0.0f;
  for (int i = 0; i < n; i++) {
    cum += logits[order[i]];
    if (r < cum) return order[i];
  }
  return order[n - 1];
}

int llmLangCount(void) { return LLM_N_LANGS; }

const char* llmLangCode(int lang) { return (lang >= 0 && lang < LLM_N_LANGS) ? kLlmLangCodes[lang] : ""; }

int llmPromptCount(void) { return LLM_N_PROMPTS; }

const char* llmPrompt(int lang, int index) {
  if (lang < 0 || lang >= LLM_N_LANGS || index < 0 || index >= LLM_N_PROMPTS) return "";
  return kLlmPrompts[lang][index];
}

int llmParamCount(void) { return LLM_N_PARAMS; }

int llmStart(LlmSession* s, const char* prompt, uint32_t seed, float temperature, float topp) {
  if (!mapped) mapWeights();
  memset(s, 0, sizeof(*s));
  s->temperature = temperature;
  s->topp = topp;
  s->rng = ((uint64_t)seed << 1) | 1u;

  // Same layout as training: BOS, "> ", the question, newline; the answer follows.
  int n = 0;
  s->pending[n++] = 0;
  s->pending[n++] = tokenFor('>');
  s->pending[n++] = tokenFor(' ');
  int32_t cp;
  int used;
  while ((used = utf8Decode(prompt, &cp)) > 0) {
    prompt += used;
    const int t = tokenFor(cp);
    if (t < 0 || n >= LLM_SEQ_LEN - 2) return -1;
    s->pending[n++] = t;
  }
  s->pending[n++] = newlineToken;
  for (int i = 1; i < n; i++) if (s->pending[i] < 0) return -1;
  s->pendingCount = n;
  return 0;
}

const char* llmNext(LlmSession* s) {
  if (s->done) return 0;
  while (s->pendingIndex < s->pendingCount) {
    if (s->pos >= LLM_SEQ_LEN) { s->done = 1; return 0; }
    forward(s->pending[s->pendingIndex++], s->pos++);
  }
  const int next = sample(s);
  if (next == newlineToken) { s->done = 1; return 0; }
  s->pending[0] = next;
  s->pendingCount = 1;
  s->pendingIndex = 0;
  return kLlmVocab[next];
}
