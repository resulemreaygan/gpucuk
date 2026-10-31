#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Memorization experiment: can template data or looser training make GPU'cuk say new things?

Trains several variants (never touching the device model), samples them the way the device does
(temperature + top-p) and sorts every answer into one bucket:

    memorized  exactly an answer the model was trained on
    held-out   a template combination kept out of training (new, and known to be good)
    novel      in no data file, but every word is a real word of that language
    broken     contains an unknown word (typo, gibberish or the other language)

Writes ``runs/experiment/report.md`` and one checkpoint per variant.

``--eval`` scores an already trained checkpoint (for example the device model) instead. Every
template answer was trained on there, so the buckets become: handwritten, template, novel, broken.

Usage:
    memorization_experiment.py [--samples 40] [--only A,D]
    memorization_experiment.py --eval runs/gpucuk.pt [--samples 40]

Author: Resul Emre Aygan
"""
from __future__ import annotations

import argparse
import math
import random
import re
import sys
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

import torch
import torch.nn.functional as F

sys.path.insert(0, str(Path(__file__).resolve().parent))
from train import BOS, ROOT, Config, Model, Pair, Vocab, build_batch, expand_templates, load_dataset, sample_text  # noqa: E402

__author__ = "Resul Emre Aygan"

LANGS = ("tr", "en")
HOLDOUT = 0.25
OUT_DIR = ROOT / "runs/experiment"


@dataclass(frozen=True)
class Variant:
    """One training setup of the experiment."""

    name: str
    description: str
    templates: bool
    steps: int
    dropout: float
    weight_decay: float = 0.01


VARIANTS = [
    Variant("A", "hand-written data, 3000 steps (how v0.3 was trained)", False, 3000, 0.0),
    Variant("B", "hand-written data, 800 steps (undertrained)", False, 800, 0.0),
    Variant("C", "hand-written data, dropout 0.2", False, 3000, 0.2),
    Variant("D", "template data, 3000 steps", True, 3000, 0.0),
    Variant("E", "template data, dropout 0.1", True, 3000, 0.1),
    Variant("F", "template data, dropout 0.2", True, 3000, 0.2),
]
# (name, temperature, top-p)
SETTINGS = [("normal", 1.2, 0.9), ("overclock", 1.6, 1.0)]
DEVICE_SETTINGS = [("normal", 1.0, 0.9), ("overclock", 1.4, 0.9)]
BUCKETS = ("memorized", "held-out", "novel", "broken")

Classifier = Callable[[str, str], str]
Example = tuple[str, str, str]  # prompt, answer, language


@dataclass
class Corpus:
    """Everything the experiment needs to know about the data, per language."""

    prompts: dict[str, list[str]]
    original: dict[str, list[Pair]]
    extra: dict[str, list[Pair]]
    held: dict[str, list[Pair]]
    known_words: dict[str, set[str]]


@dataclass
class Score:
    """Sampling result for one model and one sampler setting."""

    percent: dict[str, float]
    distinct: float
    examples: dict[str, list[Example]]


def load_language(code: str, rng: random.Random) -> tuple[list[str], list[Pair], list[Pair], list[Pair]]:
    """Loads one language and splits its template answers into training and held-out parts.

    Uses the same random call order as train.py, so ``extra + held`` equals the template
    answers the device model was trained on.

    Args:
        code: Language code (``tr``, ``en``).
        rng: Random source seeded like train.py.

    Returns:
        Questions, hand-written pairs, template pairs for training and held-out template pairs.
    """
    prompts, pairs = load_dataset(ROOT / f"data/gpucuk_{code}.txt")
    expanded = expand_templates(ROOT / f"data/templates_{code}.txt", prompts, pairs, rng)
    extra: list[Pair] = []
    held: list[Pair] = []
    for prompt, answers in expanded.items():
        k = round(len(answers) * HOLDOUT)
        held += [(prompt, a) for a in answers[:k]]
        extra += [(prompt, a) for a in answers[k:]]
    return prompts, pairs, extra, held


def words(text: str, code: str) -> list[str]:
    """Splits an answer into lowercase words.

    Args:
        text: Answer text.
        code: Language code; Turkish needs its own dotted and dotless I.

    Returns:
        Words without punctuation.
    """
    if code == "tr":
        text = text.replace("I", "ı").replace("İ", "i")
    return [w for w in re.split(r"[\s.,!?:;]+", text.lower()) if w]


def load_corpus() -> Corpus:
    """Loads every language and collects the words that count as real.

    Returns:
        The corpus.
    """
    rng = random.Random(1337)
    prompts, original, extra, held = {}, {}, {}, {}
    for code in LANGS:
        prompts[code], original[code], extra[code], held[code] = load_language(code, rng)
    known: dict[str, set[str]] = {code: set() for code in LANGS}
    for code in LANGS:
        for _, a in original[code] + extra[code] + held[code]:
            known[code].update(words(a, code))
    return Corpus(prompts, original, extra, held, known)


def real_words(answer: str, code: str, corpus: Corpus) -> bool:
    """Tells whether every word of an answer appears somewhere in the data.

    Args:
        answer: Answer text.
        code: Language code.
        corpus: The corpus.

    Returns:
        True for a non-empty answer made only of known words.
    """
    ws = words(answer, code)
    return bool(ws) and all(w in corpus.known_words[code] for w in ws)


def train(pairs: Sequence[Pair], steps: int, dropout: float, weight_decay: float,
          seed: int = 1337, lr: float = 3e-3, batch: int = 32) -> tuple[Model, Vocab, float]:
    """Trains one variant with train.py's schedule.

    Args:
        pairs: Training (question, answer) pairs.
        steps: Optimizer steps.
        dropout: Dropout rate during training.
        weight_decay: AdamW weight decay.
        seed: Torch seed.
        lr: Peak learning rate.
        batch: Batch size.

    Returns:
        The model in eval mode, its vocabulary and the final loss over all pairs.
    """
    torch.manual_seed(seed)
    vocab = Vocab(pairs)
    cfg = Config(vocab_size=len(vocab.chars), dropout=dropout)
    model = Model(cfg)
    x_all, y_all = build_batch(pairs, vocab, cfg)
    opt = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=weight_decay, betas=(0.9, 0.95))
    warmup = max(1, steps // 20)
    model.train()
    for step in range(1, steps + 1):
        rate = lr * min(1.0, step / warmup)
        rate *= 0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * min(1.0, step / steps)))
        for group in opt.param_groups:
            group["lr"] = rate
        idx = torch.randint(0, len(pairs), (min(batch, len(pairs)),))
        loss = F.cross_entropy(model(x_all[idx]).view(-1, cfg.vocab_size), y_all[idx].view(-1), ignore_index=-100)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
    model.eval()
    with torch.no_grad():
        final = F.cross_entropy(model(x_all).view(-1, cfg.vocab_size), y_all.view(-1), ignore_index=-100).item()
    return model, vocab, final


@torch.no_grad()
def sample(model: Model, vocab: Vocab, prompt: str, n: int, temperature: float, topp: float,
           gen: torch.Generator) -> list[str]:
    """Batch version of the device sampler: BOS excluded, temperature, then nucleus (top-p).

    Args:
        model: Model in eval mode.
        vocab: Vocabulary.
        prompt: Question text.
        n: Number of answers.
        temperature: Softmax temperature.
        topp: Nucleus cutoff; 1.0 disables it.
        gen: Random generator.

    Returns:
        ``n`` answers without their closing newline.
    """
    ids = torch.tensor([[BOS] + vocab.encode(sample_text(prompt))] * n)
    newline = vocab.index["\n"]
    done = [False] * n
    outs: list[list[int]] = [[] for _ in range(n)]
    while ids.shape[1] < model.cfg.seq_len and not all(done):
        logits = model(ids)[:, -1] / temperature
        logits[:, BOS] = -float("inf")
        probs = F.softmax(logits, -1)
        if topp < 1.0:
            sorted_probs, order = probs.sort(-1, descending=True)
            sorted_probs[(sorted_probs.cumsum(-1) - sorted_probs) >= topp] = 0
            probs = torch.zeros_like(probs).scatter(1, order, sorted_probs)
        nxt = torch.multinomial(probs, 1, generator=gen).squeeze(1)
        for i, t in enumerate(nxt.tolist()):
            if not done[i]:
                if t == newline:
                    done[i] = True
                else:
                    outs[i].append(t)
        ids = torch.cat([ids, nxt[:, None]], 1)
    return [vocab.decode(o) for o in outs]


def score(model: Model, vocab: Vocab, corpus: Corpus, samples: int, temperature: float, topp: float,
          classify: Classifier) -> Score:
    """Samples every question in every language and buckets the answers.

    Args:
        model: Model in eval mode.
        vocab: Vocabulary.
        corpus: The corpus.
        samples: Answers per question and language.
        temperature: Softmax temperature.
        topp: Nucleus cutoff.
        classify: Maps (answer, language) to a bucket name.

    Returns:
        Bucket percentages, distinct answers per question and examples per bucket.
    """
    gen = torch.Generator().manual_seed(42)
    counts: dict[str, int] = {}
    distinct: list[int] = []
    examples: dict[str, list[Example]] = {}
    for code in LANGS:
        for prompt in corpus.prompts[code]:
            answers = sample(model, vocab, prompt, samples, temperature, topp, gen)
            distinct.append(len(set(answers)))
            for a in answers:
                bucket = classify(a, code)
                counts[bucket] = counts.get(bucket, 0) + 1
                picks = examples.setdefault(bucket, [])
                if (prompt, a, code) not in picks:
                    picks.append((prompt, a, code))
    total = sum(counts.values())
    return Score({b: 100 * n / total for b, n in counts.items()}, sum(distinct) / len(distinct), examples)


def load_checkpoint(path: str | Path) -> tuple[Model, Vocab]:
    """Loads a checkpoint written by train.py or by this experiment.

    Args:
        path: Checkpoint file.

    Returns:
        The model in eval mode and its vocabulary.
    """
    ckpt = torch.load(path)
    model = Model(Config(**{**ckpt["cfg"], "dropout": 0.0}))
    model.load_state_dict(ckpt["state"])
    model.eval()
    vocab = Vocab([])
    vocab.chars = ckpt["vocab"]
    vocab.index = {ch: i for i, ch in enumerate(vocab.chars)}
    return model, vocab


def evaluate_checkpoint(path: str, samples: int) -> None:
    """Prints how a trained model behaves with the device's sampler settings.

    Args:
        path: Checkpoint file.
        samples: Answers per question and language.
    """
    corpus = load_corpus()
    handwritten = {code: {a for _, a in corpus.original[code]} for code in LANGS}
    template = {code: {a for _, a in corpus.extra[code] + corpus.held[code]} for code in LANGS}

    def classify(answer: str, code: str) -> str:
        if answer in handwritten[code]:
            return "handwritten"
        if answer in template[code]:
            return "template"
        return "novel" if real_words(answer, code, corpus) else "broken"

    model, vocab = load_checkpoint(path)
    ex_rng = random.Random(7)
    print(f"{path}: 9 questions x 2 languages x {samples} answers")
    for setting, temperature, topp in DEVICE_SETTINGS:
        result = score(model, vocab, corpus, samples, temperature, topp, classify)
        buckets = ("handwritten", "template", "novel", "broken")
        print(f"\n{setting} (temp {temperature}, top-p {topp}): "
              + " · ".join(f"{b} {result.percent.get(b, 0):.0f}%" for b in buckets)
              + f" · {result.distinct:.1f} distinct answers per {samples} tries")
        for bucket in ("novel", "broken"):
            picks = result.examples.get(bucket, [])[:]
            ex_rng.shuffle(picks)
            for prompt, answer, code in picks[:8]:
                print(f"  {bucket} [{code}] > {prompt} → {answer}")


def memorization_classifier(trained: dict[str, set[str]], held: dict[str, set[str]], corpus: Corpus) -> Classifier:
    """Builds the classifier for one trained variant.

    Args:
        trained: Answers the variant was trained on, by language.
        held: Held-out template answers, by language.
        corpus: The corpus.

    Returns:
        A function mapping (answer, language) to memorized, held-out, novel or broken.
    """

    def classify(answer: str, code: str) -> str:
        if answer in trained[code]:
            return "memorized"
        if answer in held[code]:
            return "held-out"
        return "novel" if real_words(answer, code, corpus) else "broken"

    return classify


def run_experiment(samples: int, only: set[str] | None) -> None:
    """Trains and scores the variants, then writes the report.

    Args:
        samples: Answers per question and language.
        only: Variant names to run; None runs all of them.
    """
    corpus = load_corpus()
    for code in LANGS:
        print(f"{code}: {len(corpus.original[code])} hand-written answers, {len(corpus.extra[code])} template "
              f"answers for training + {len(corpus.held[code])} held out")
    held = {code: {a for _, a in corpus.held[code]} for code in LANGS}

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    results: list[tuple[Variant, float, dict[str, Score]]] = []
    for variant in VARIANTS:
        if only and variant.name not in only:
            continue
        if variant.templates:
            pairs = {code: corpus.original[code] * 2 + corpus.extra[code] for code in LANGS}
        else:
            pairs = {code: corpus.original[code] for code in LANGS}
        all_pairs = [p for code in LANGS for p in pairs[code]]
        t0 = time.time()
        model, vocab, loss = train(all_pairs, variant.steps, variant.dropout, variant.weight_decay)
        train_secs = time.time() - t0
        torch.save({"cfg": model.cfg.__dict__, "vocab": vocab.chars, "langs": list(LANGS), "prompts": corpus.prompts,
                    "state": model.state_dict(), "variant": variant.name, "description": variant.description},
                   OUT_DIR / f"{variant.name}.pt")

        trained = {code: {a for _, a in pairs[code]} for code in LANGS}
        classify = memorization_classifier(trained, held, corpus)
        scores = {setting: score(model, vocab, corpus, samples, temperature, topp, classify)
                  for setting, temperature, topp in SETTINGS}
        results.append((variant, loss, scores))
        n, o = scores["normal"].percent, scores["overclock"].percent
        print(f"{variant.name} ({variant.description}): loss {loss:.3f}, {train_secs:.0f} s | normal: memorized "
              f"{n.get('memorized', 0):.0f}% held-out {n.get('held-out', 0):.0f}% novel {n.get('novel', 0):.0f}% "
              f"broken {n.get('broken', 0):.0f}% | overclock broken {o.get('broken', 0):.0f}%", flush=True)

    write_report(results, samples, corpus)


def write_report(results: Sequence[tuple[Variant, float, dict[str, Score]]], samples: int, corpus: Corpus) -> None:
    """Writes ``runs/experiment/report.md``.

    Args:
        results: (variant, final loss, scores by setting) per trained variant.
        samples: Answers per question and language.
        corpus: The corpus.
    """
    ex_rng = random.Random(7)
    lines = [
        "# Memorization experiment",
        "",
        f"Generated by `tools/memorization_experiment.py`. Every variant answered every question in every "
        f"language {samples} times (9 questions × 2 languages × {samples} = {18 * samples} answers).",
        "",
        "- **memorized:** exactly an answer seen in training",
        "- **held-out:** a template answer kept out of training (new, and known to be good)",
        "- **novel:** in no data file, but every word is real",
        "- **broken:** contains an unknown word (typo, gibberish or the other language)",
        f"- **distinct:** average number of different answers per question in {samples} tries",
        "",
        "Template data: " + ", ".join(f"{c}: {len(corpus.extra[c])} for training + {len(corpus.held[c])} held out"
                                      for c in LANGS),
        "",
    ]
    for setting, temperature, topp in SETTINGS:
        lines += [f"## {setting} (temperature {temperature}, top-p {topp})", "",
                  "| variant | memorized | held-out | novel | broken | distinct | loss |",
                  "|---|---|---|---|---|---|---|"]
        for variant, loss, scores in results:
            s = scores[setting]
            cells = " | ".join(f"{s.percent.get(b, 0):.0f}%" for b in BUCKETS)
            lines.append(f"| {variant.name}: {variant.description} | {cells} | {s.distinct:.1f} | {loss:.3f} |")
        lines.append("")
    lines += ["## Examples (normal setting)", ""]
    for variant, _, scores in results:
        lines.append(f"### {variant.name}: {variant.description}")
        for bucket in ("held-out", "novel", "broken"):
            picks = scores["normal"].examples.get(bucket, [])[:]
            ex_rng.shuffle(picks)
            for prompt, answer, code in picks[:6]:
                lines.append(f"- {bucket} [{code}] > {prompt} → {answer}")
        lines.append("")
    (OUT_DIR / "report.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"\nreport: {OUT_DIR / 'report.md'}")


def main() -> None:
    """Parses the command line and runs the experiment or the evaluation."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--samples", type=int, default=40, help="answers per question and language")
    ap.add_argument("--only", default="", help="run only these variants, e.g. A,D")
    ap.add_argument("--eval", default="", help="score a trained checkpoint with the device's settings instead")
    args = ap.parse_args()
    if args.eval:
        evaluate_checkpoint(args.eval, args.samples)
    else:
        run_experiment(args.samples, set(args.only.split(",")) if args.only else None)


if __name__ == "__main__":
    main()
