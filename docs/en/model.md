# Model

[Türkçe](../tr/model.md)

GPU'cuk is a character-level transformer with the same building blocks as Llama, trained from scratch. It has 141,696 parameters.

## Why characters

- One token per character means no tokenizer file, and Turkish letters (ç, ğ, ı, İ, ö, ş, ü) just work.
- The vocabulary is every character that appears in the training data plus a BOS token: 83 tokens. A small vocabulary also keeps the classifier small.
- The price: one model step per character. A 40-character answer to a short question takes about 50 steps.

## Architecture

```
token embedding (83 × 64)
3 × block:
    RMSNorm → attention: 4 query heads, 2 key/value heads (grouped-query attention),
              head size 16, rotary positions (interleaved pairs, θ = 10000) → + residual
    RMSNorm → SwiGLU feed-forward, hidden size 172 → + residual
RMSNorm → classifier (shares its weights with the token embedding)
```

The context is 96 tokens: BOS, `> `, the question, a newline and the answer. The sizes live in `Config` in `tools/train.py`; the exported header carries them to the C code as `LLM_*` defines.

## Parameter budget

| Part | Parameters |
|---|---|
| Token embedding (also the classifier) | 83 × 64 = 5,312 |
| Attention per layer (wq, wk, wv, wo) | 64·64 + 2·(32·64) + 64·64 = 12,288 |
| Feed-forward per layer (w1, w2, w3) | 3 · 64·172 = 33,024 |
| Two norms per layer | 2 · 64 = 128 |
| 3 layers | 3 · 45,440 = 136,320 |
| Final norm | 64 |
| **Total** | **141,696** |

## Memory on the device

- **Weights:** 141,696 `float`s = 566,784 bytes (554 KB). They are a `const` array compiled into the firmware, so they stay in flash and are read through the flash cache. They cost no RAM.
- **KV cache:** keys and values for every layer and position, 2 × 3 × 96 × 32 × 4 bytes = 73,728 bytes (72 KB). With as many key/value heads as query heads it would be twice that.
- **Everything else** (activations, logits, sampler scratch): 4,000 bytes.

That is 77,728 bytes (76 KB) in total, all static: the inference code never calls `malloc`. For the whole firmware the linker reports 95,216 bytes of static RAM, and the device reports 146 KB of free heap while it answers.

## Inference code

`firmware/src/llm/llm.c` is about 300 lines of plain C. Its structure follows Andrej Karpathy's [llama2.c](https://github.com/karpathy/llama2.c) (`run.c`), written again for this model: no file loading, no heap, weights mapped straight from the array in `model_data.h`. `tools/train.py` exports the weights in exactly the order `llm.c` reads them.

The same file is compiled for the device and for the computer (`tools/host/llm_host`), which makes the model easy to test without hardware:

- At temperature 0, the C code gives the same answers as PyTorch for all 18 questions (9 per language).
- With sampling, the computer gives the same answers as the device for the same seed and settings: all 21 answers in the v0.4 device log were reproduced exactly.

## Sampling

- Temperature, then top-p (nucleus sampling). BOS is never sampled. An answer ends when the model emits a newline, or when the context is full.
- Random numbers come from xorshift64\*, started from `(seed << 1) | 1`. The same seed, question and settings give the same answer.
- A small shortcut: a character whose probability is below `(1 − top_p) / (vocab − 1)` can never be part of the nucleus, so only the characters above that line are sorted.

## Speed

On the device the model processes **3.8 to 4.0 tokens per second**. That counts every token that goes through the model (question and answer) and only the time spent in the model, not the time spent waiting for the display.

Time per answer grows a little faster than linearly, because attention looks back at every earlier position. The 14 distinct timings in the v0.4 serial log all match

```
seconds ≈ 0.233 · n + 0.00043 · n²      (n = tokens)
```

to within 0.05 s. For example "mdisec ne dedi?" with a 49-character answer is 68 tokens: 17.8 seconds.

Where the time goes: about 0.23 s per token at 160 MHz is roughly 37 million clock cycles, for about 142 thousand multiply-adds with the weights. That is around 260 cycles per multiply-add, the cost of floating point in software. Before the first run the estimate was about 3 tokens per second; the first measurement on the device was 3.9.

Not done (yet): int8 weights with integer arithmetic. That would make the weights four times smaller and should be much faster, but the demo did not need it.

## Limits

142 thousand parameters trained on 176 hand-written answers (about 6,900 characters) plus their template variations cannot understand questions. The model knows its 9 built-in questions, and the firmware only offers those. Most answers are lines from the data or new combinations of its phrases; how many are actually new, and how that was measured, is in [memorization.md](memorization.md).
