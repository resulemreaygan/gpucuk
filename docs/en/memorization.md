# Memorization experiment

[Türkçe](../tr/memorization.md)

v0.3 worked, but it only recited: in normal mode, every one of 720 sampled answers was a line from the dataset, word for word. The question was whether a 142K-parameter model can be made to say something new **without changing the model**: same architecture, same C code, same speed and RAM. Only the data and the training could change.

## Why it memorizes

176 hand-written answers are about 6,900 characters, against 141 thousand parameters. The model learns every answer by heart. Measured on variant A below: it gives the first character of an answer 29% probability on average, so that is where the choice happens; for 97% of the remaining characters it is more than 99% sure. A higher temperature does not help: the model leaves its memorized path, but it knows no language beyond those lines, so it produces gibberish.

## Setup

`tools/memorization_experiment.py` trains six variants with `train.py`'s model and schedule (3,000 steps unless noted, weight decay 0.01, seed 1337). The device model is never touched.

| Variant | Data | Training |
|---|---|---|
| A | hand-written answers only | no dropout (how v0.3 was trained) |
| B | hand-written answers only | 800 steps (undertrained) |
| C | hand-written answers only | dropout 0.2 |
| D | hand-written + templates | no dropout |
| E | hand-written + templates | dropout 0.1 |
| F | hand-written + templates | dropout 0.2 |

The template variants train on the hand-written answers twice plus 75% of the [template](training.md#templates) answers. The other 25% (132 Turkish, 134 English) are held out: new to the model, and known to be good sentences.

Every variant answers every question in both languages 40 times, so 720 answers per setting. Sampling works like the device's (temperature, then top-p, never BOS), but with PyTorch's random numbers, so a given seed does not give the device's answer. Two settings: v0.3's normal (temperature 1.2, top-p 0.9) and v0.3's overclock (1.6, top-p off).

Each answer goes into one bucket:

- **memorized:** exactly an answer the variant was trained on;
- **held-out:** a held-out template answer;
- **novel:** in no data file, but every word appears somewhere in the data of that language;
- **broken:** at least one word that does not (a typo, gibberish or the other language).

The word check is only an approximation. "Zero. But FPU of books." counts as novel, while the device's "mdisec haklıydı. Şimdi saniyede birkaç karakter yazıyor." counts as broken, because the data has "yazıyorum" but not "yazıyor".

"Distinct" is the average number of different answers per question in 40 tries.

## Results

Normal (temperature 1.2, top-p 0.9):

| Variant | memorized | held-out | novel | broken | distinct | final loss |
|---|---|---|---|---|---|---|
| A: hand-written | 100% | 0% | 0% | 0% | 9.5 | 0.059 |
| B: hand-written, 800 steps | 96% | 0% | 0% | 4% | 10.3 | 0.068 |
| C: hand-written, dropout 0.2 | 98% | 0% | 1% | 1% | 9.7 | 0.066 |
| D: templates | 97% | 1% | 2% | 0% | 23.6 | 0.104 |
| **E: templates, dropout 0.1** | 72% | **3%** | **18%** | 7% | 27.4 | 0.120 |
| F: templates, dropout 0.2 | 33% | 4% | 27% | 36% | 35.3 | 0.168 |

Overclock (temperature 1.6, top-p off):

| Variant | memorized | held-out | novel | broken | distinct |
|---|---|---|---|---|---|
| A: hand-written | 66% | 0% | 0% | 34% | 22.2 |
| B: hand-written, 800 steps | 9% | 0% | 1% | 90% | 39.2 |
| C: hand-written, dropout 0.2 | 37% | 0% | 4% | 59% | 31.6 |
| D: templates | 31% | 1% | 5% | 63% | 37.9 |
| E: templates, dropout 0.1 | 21% | 1% | 13% | 66% | 38.6 |
| F: templates, dropout 0.2 | 4% | 1% | 9% | 86% | 39.9 |

## What it shows

- **Loosening the training alone does nothing useful.** With less training (B) or more dropout (C), the model still recites, and what it does not recite is mostly broken. The problem is too little data, not too much training.
- **Templates alone more than double the variety** (9.5 → 23.6 different answers per 40 tries), but the model simply memorizes the bigger set: 97% memorized.
- **Templates plus a little dropout (E) is the first variant that builds new sentences:** 18% novel, plus 3% held-out template answers it never saw, at 7% broken. For example:
  - "A single-core, fanless RISC-V legend. The rest is talent."
  - "E-ink ekranlı, pilli bir süper bilgisayar. Gerisi yetenek."
  - "Hayatım değişti. Orada çıktım."
- **More dropout (F)** gives more novel answers, but a third of all answers are broken.
- v0.3's overclock setting is far too hot for E: two thirds broken.

None of this changes the device side: dropout only exists during training, and the model has the same size and speed.

## The device model (v0.4)

E's recipe became `train.py`'s default, with two differences: no answers are held out, and the sampler settings were lowered to suit the new model. Measured with `--eval`, 720 answers per setting:

| Setting | hand-written | template | novel | broken | distinct |
|---|---|---|---|---|---|
| Normal (1.0, top-p 0.9) | 16% | 63% | 17% | 5% | 26.9 |
| Overclock (1.4, top-p 0.9) | 11% | 48% | 22% | 19% | 32.7 |

Here "template" means one of the template answers the model was trained on, and "novel" a sentence that is in neither file.

In the device's serial log of v0.4 (about half an hour of use), 5 of the 17 different answers were novel:

- "mdisec haklıydı. Şimdi o izliyor, ben üretiyorum." (seed 4813)
- "Adım GPU'cuk. Yani cebe sığan, RISC-V bir efsane." (seed 4238)
- "Hoş geldin! Bende fan yok ama kitap çok." (seed 812)
- "Selam! Bende FPU yok ama karizmam var." (seed 46)
- "Çok iyiydi. Teşekkürler latentshift.ai." (seed 515)

The price: the hand-written jokes are now only 16% of the answers, and about one answer in twenty is broken.

## Running it

```sh
.venv/bin/python tools/memorization_experiment.py                  # all six variants: 16 minutes on the development Mac
.venv/bin/python tools/memorization_experiment.py --only A,E       # some of them
.venv/bin/python tools/memorization_experiment.py --eval runs/gpucuk.pt
```

The full run writes `runs/experiment/report.md` (the tables above plus example answers from every bucket) and one checkpoint per variant. `--eval` prints the device-settings table and examples for an already trained model.
