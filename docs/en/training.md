# Data and training

[Türkçe](../tr/training.md)

The model is trained on a computer's CPU with PyTorch; no GPU is involved at any point. On the Apple Silicon Mac it was developed on, training takes about two minutes.

## The dataset

`data/gpucuk_tr.txt` and `data/gpucuk_en.txt` hold hand-written questions and answers:

```
> GPU ver
Önce kitapları bitir.
GPU benim. Sen kitapları oku.
...
```

- A `> question` line, then one answer per line. A blank line ends the group, and `#` starts a comment.
- 9 questions and 88 answers per language. The English answers are separate jokes in the same tone, not translations.
- Both files list the questions in the same order, so switching the language on the device keeps the question.
- Tone: a tiny device that thinks it is a giant GPU. Cocky but cute.
- Answers should stay within 64 characters (template answers longer than that are dropped). Question plus answer must fit the 96-token context; `train.py` stops with an error if one does not.

There is no language tag. One model learns both languages, and the language of the question decides the language of the answer.

## Templates

176 answers are not much: a model trained on them alone recites them word for word. `data/templates_tr.txt` and `data/templates_en.txt` describe the same jokes with interchangeable parts:

```
@missing: CUDA | fan | FPU | ekran kartı | soğutucu | tensor çekirdeği
@plenty: kitap | karizma | sabır | özgüven | stil | hayal gücü

> GPU ver
Bende {missing} yok ama {plenty} çok.
```

- `@name: a | b | c` defines a slot. Slots are shared by every question in the file.
- Lines under a `> question` are templates; `{name}` is replaced by each value of the slot.
- `train.py` shuffles all combinations (seed 1337), skips those that use the same value twice or get too long, and keeps at most 40 per template. Answers that already exist in the hand-written file are not added again.

This gives 529 extra Turkish and 536 extra English answers. Why templates, and what they changed, is measured in [memorization.md](memorization.md).

## What the model sees

Every training sample is one sequence: `BOS`, `> `, the question, a newline, the answer and a closing newline. The loss only covers the answer and its newline; the question is context. On the device, `llm.c` feeds the question in exactly the same form and samples until the model writes a newline.

The training set is every hand-written answer twice plus every template answer: 1,417 samples.

## Recipe

| Setting | Value |
|---|---|
| Steps | 3,000, batches of 32 samples drawn at random |
| Optimizer | AdamW, peak learning rate 3e-3, betas (0.9, 0.95), weight decay 0.01 |
| Schedule | Linear warm-up for 150 steps, then cosine decay to 10% of the peak |
| Regularization | Dropout 0.1, gradient clipping at 1.0 |
| Seed | 1337 for everything (PyTorch, Python, template expansion) |
| Final loss | 0.128 (over all training samples) |

## Running it

```sh
.venv/bin/python tools/train.py
```

This writes:

- `firmware/src/llm/model_data.h`: the model sizes as `LLM_*` defines, the vocabulary, the questions and all weights as a `const float` array (about 2.5 MB of C source, 554 KB once compiled).
- `runs/gpucuk.pt`: a PyTorch checkpoint for the evaluation tools.

It also prints the loss every 200 steps and, for every question, the most likely answer (`[t=0]`) and two sampled ones.

Useful options: `--steps`, `--dropout`, `--no-templates`, `--original-repeat` (how often hand-written answers repeat; default 2), `--dim` and `--layers`. The C code reads every size from `model_data.h`, so a differently sized model needs no code changes; the context is fixed at 96, and RAM use grows with `--dim` and `--layers` (see [model.md](model.md)).

## Checking a new model

```sh
tools/host/build.sh                       # compile llm.c for the computer
tools/host/llm_host -t 0 -n 1             # must equal the "[t=0]" lines of the training log
tools/host/llm_host -t 1.0 -p 0.9 -s 42 -n 1 2
.venv/bin/python tools/memorization_experiment.py --eval runs/gpucuk.pt
```

The third line answers question 3 ("GPU ver" / "Give me a GPU") the way the device does in normal mode with seed 42. For the published model:

```
141696 parameters, temp 1.00, top-p 0.90
[tr] > GPU ver
  [s=42] Önce kitapları bitir. Sonra belki H100.   (39 chars, 2.21 ms)
[en] > Give me a GPU
  [s=42] You're already holding me. Turn the page first.   (47 chars, 2.83 ms)
```

The times depend on the computer. On the development Mac the Turkish answer took about 2 milliseconds; the e-reader needs 12.7 seconds for the same answer. Leave out the `2` to get every question, and change `-s` and `-n` for other seeds.

The last line samples 40 answers per question and language with the device's settings and sorts them into hand-written, template, novel and broken (see [memorization.md](memorization.md)).

## Reproducibility

`model_data.h` in this repository is the model running on the device. On the machine it was developed on, with the package versions in `requirements.txt` (torch 2.14.1), `tools/train.py` reproduces it bit for bit. Another torch version or another CPU may give slightly different weights.

## Characters and the font

The vocabulary is built from the data, so new characters in the data simply become new tokens. The device font, however, only has printable ASCII, the Turkish letters and the symbols `·`, `…`, `—`, `–`, `’`, `“`, `”`, `°` and `×`; anything else is drawn as `?`. To use other characters, add them to `CHARSET` in `tools/make_font.py` and run `tools/make_fonts.sh`.
