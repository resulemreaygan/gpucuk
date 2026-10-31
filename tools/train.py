#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Trains the GPU'cuk character-level transformer and exports it as a C header.

One model learns every language; the question text alone selects the answer language.
Data files are named ``gpucuk_<lang>.txt`` and must list the same questions in the same order.
A ``templates_<lang>.txt`` next to a data file adds template answers; together with dropout this
makes the model combine phrases instead of reciting whole answers.

The architecture must stay in lockstep with ``firmware/src/llm/llm.c``:
Llama-style (RMSNorm, interleaved RoPE, SwiGLU, grouped-query attention), with the output
classifier tied to the token embedding.

Usage:
    train.py [--data data/gpucuk_tr.txt data/gpucuk_en.txt] [--out firmware/src/llm/model_data.h]
             [--steps 3000] [--no-templates] [--dropout 0.1]

Author: Resul Emre Aygan
"""
from __future__ import annotations

import argparse
import itertools
import math
import random
import re
import time
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import cast

import torch
import torch.nn as nn
import torch.nn.functional as F

__author__ = "Resul Emre Aygan"

ROOT = Path(__file__).resolve().parent.parent
BOS = 0
MAX_ANSWER = 64
PER_TEMPLATE = 40
SLOT = re.compile(r"\{(\w+)\}")

Pair = tuple[str, str]


@dataclass
class Config:
    """Model hyperparameters; mirrored by the ``LLM_*`` defines in the exported header."""

    dim: int = 64
    n_layers: int = 3
    n_heads: int = 4
    n_kv_heads: int = 2
    hidden_dim: int = 172
    seq_len: int = 96
    vocab_size: int = 0
    norm_eps: float = 1e-5
    rope_theta: float = 10000.0
    dropout: float = 0.0  # training only; not exported


def load_dataset(path: str | Path) -> tuple[list[str], list[Pair]]:
    """Reads a ``gpucuk_<lang>.txt`` data file.

    Args:
        path: Data file with ``> question`` lines followed by one answer per line.

    Returns:
        The questions in file order and every (question, answer) pair.

    Raises:
        SystemExit: If an answer appears before any question.
    """
    prompts: list[str] = []
    pairs: list[Pair] = []
    prompt: str | None = None
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("#"):
            continue
        if not line:
            prompt = None
            continue
        if line.startswith(">"):
            prompt = line[1:].strip()
            if prompt not in prompts:
                prompts.append(prompt)
            continue
        if prompt is None:
            raise SystemExit(f"{path}: answer outside a question group: {raw!r}")
        pairs.append((prompt, line))
    return prompts, pairs


def load_templates(path: str | Path) -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    """Reads a ``templates_<lang>.txt`` file.

    Lines ``@name: a | b`` define slots; lines under ``> question`` are templates using ``{name}``.

    Args:
        path: Template file.

    Returns:
        Slot values by slot name, and template lines by question.

    Raises:
        SystemExit: If a template appears before any question.
    """
    slots: dict[str, list[str]] = {}
    groups: dict[str, list[str]] = {}
    prompt: str | None = None
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            prompt = None
            continue
        if line.startswith("#"):
            continue
        if line.startswith("@"):
            name, values = line[1:].split(":", 1)
            slots[name.strip()] = [v.strip() for v in values.split("|")]
        elif line.startswith(">"):
            prompt = line[1:].strip()
            groups.setdefault(prompt, [])
        elif prompt is None:
            raise SystemExit(f"{path}: template outside a question group: {raw!r}")
        else:
            groups[prompt].append(line)
    return slots, groups


def fill_slots(template: str, values: Sequence[str]) -> str:
    """Replaces the ``{name}`` placeholders of a template, left to right.

    Args:
        template: Text with ``{name}`` placeholders.
        values: One value per placeholder.

    Returns:
        The filled text.
    """
    it = iter(values)
    return SLOT.sub(lambda _: next(it), template)


def expand_template(template: str, slots: dict[str, list[str]], rng: random.Random, limit: int) -> list[str]:
    """Fills a template with slot values.

    Args:
        template: Text with ``{name}`` placeholders.
        slots: Slot values by slot name.
        rng: Shuffles the combinations, so a capped template still covers every slot.
        limit: Longest answer allowed, in characters.

    Returns:
        Up to ``PER_TEMPLATE`` answers that use no slot value twice.

    Raises:
        SystemExit: If the template uses an undefined slot.
    """
    names = SLOT.findall(template)
    missing = [n for n in names if n not in slots]
    if missing:
        raise SystemExit(f"undefined slot {missing}: {template!r}")
    combos = list(itertools.product(*(slots[n] for n in names)))
    rng.shuffle(combos)
    out: list[str] = []
    for combo in combos:
        if len({c.lower() for c in combo}) < len(combo):
            continue
        text = fill_slots(template, combo)
        if len(text) <= limit:
            out.append(text)
        if len(out) >= PER_TEMPLATE:
            break
    return out


def expand_templates(
    path: str | Path, prompts: Sequence[str], pairs: Sequence[Pair], rng: random.Random
) -> dict[str, list[str]]:
    """Expands every template in a file.

    Args:
        path: Template file.
        prompts: Questions of the matching data file.
        pairs: Hand-written (question, answer) pairs; these are not repeated.
        rng: Random source; train.py and memorization_experiment.py use the same seed and call
            order, so both see the same answers.

    Returns:
        Shuffled template answers by question.

    Raises:
        SystemExit: If the file has a question the data file does not.
    """
    slots, groups = load_templates(path)
    out: dict[str, list[str]] = {}
    for prompt, templates in groups.items():
        if prompt not in prompts:
            raise SystemExit(f"{path}: question not in the data file: {prompt!r}")
        seen = {a for p, a in pairs if p == prompt}
        # BOS + "> " + prompt + "\n" + answer + "\n" must fit in the context
        limit = min(MAX_ANSWER, Config.seq_len - len(sample_text(prompt)) - 2)
        answers: list[str] = []
        for t in templates:
            for a in expand_template(t, slots, rng, limit):
                if a not in seen:
                    seen.add(a)
                    answers.append(a)
        rng.shuffle(answers)
        out[prompt] = answers
    return out


def sample_text(prompt: str, answer: str = "") -> str:
    """Formats a training sample the way the device feeds it (after BOS).

    Args:
        prompt: Question text.
        answer: Answer text; empty for the prompt prefix alone.

    Returns:
        ``"> "``, the question, a newline, then the answer.
    """
    return f"> {prompt}\n{answer}"


class Vocab:
    """Character vocabulary; token 0 is BOS, the rest are single characters in sorted order."""

    def __init__(self, pairs: Sequence[Pair]) -> None:
        """Builds the vocabulary from every character in the samples.

        Args:
            pairs: (question, answer) pairs.
        """
        chars = set("\n")
        for p, a in pairs:
            chars.update(sample_text(p, a))
        self.chars: list[str] = [""] + sorted(chars)
        self.index: dict[str, int] = {c: i for i, c in enumerate(self.chars)}

    def encode(self, text: str) -> list[int]:
        """Converts text to token ids.

        Args:
            text: Text made only of vocabulary characters.

        Returns:
            One token id per character.
        """
        return [self.index[c] for c in text]

    def decode(self, ids: Sequence[int]) -> str:
        """Converts token ids back to text.

        Args:
            ids: Token ids.

        Returns:
            The joined characters (BOS decodes to an empty string).
        """
        return "".join(self.chars[i] for i in ids)


class RMSNorm(nn.Module):
    """Root-mean-square layer norm with a learned scale."""

    def __init__(self, dim: int, eps: float) -> None:
        """Creates the norm.

        Args:
            dim: Feature size.
            eps: Added to the mean square for numerical stability.
        """
        super().__init__()
        self.eps = eps
        self.weight = nn.Parameter(torch.ones(dim))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """Normalizes the last dimension.

        Args:
            x: Input of shape (..., dim).

        Returns:
            Tensor of the same shape.
        """
        return x * torch.rsqrt(x.pow(2).mean(-1, keepdim=True) + self.eps) * self.weight


def rope_tables(cfg: Config) -> tuple[torch.Tensor, torch.Tensor]:
    """Precomputes rotary embedding angles.

    Args:
        cfg: Model config.

    Returns:
        Cosine and sine tables of shape (seq_len, head_size / 2).
    """
    hd = cfg.dim // cfg.n_heads
    freqs = cfg.rope_theta ** (-torch.arange(0, hd, 2).float() / hd)
    angles = torch.outer(torch.arange(cfg.seq_len).float(), freqs)
    return torch.cos(angles), torch.sin(angles)


def apply_rope(x: torch.Tensor, cos: torch.Tensor, sin: torch.Tensor) -> torch.Tensor:
    """Rotates adjacent feature pairs (x0, x1), (x2, x3), ... like llm.c does.

    Args:
        x: Queries or keys of shape (batch, time, heads, head_size).
        cos: Cosine table from ``rope_tables``.
        sin: Sine table from ``rope_tables``.

    Returns:
        Rotated tensor of the same shape.
    """
    T = x.shape[1]
    xr = x.float().reshape(*x.shape[:-1], -1, 2)
    x0, x1 = xr[..., 0], xr[..., 1]
    c = cos[:T].view(1, T, 1, -1)
    s = sin[:T].view(1, T, 1, -1)
    out = torch.stack((x0 * c - x1 * s, x0 * s + x1 * c), dim=-1)
    return out.flatten(-2).type_as(x)


class Attention(nn.Module):
    """Causal self-attention with grouped-query heads and rotary positions."""

    def __init__(self, cfg: Config) -> None:
        """Creates the projections.

        Args:
            cfg: Model config.
        """
        super().__init__()
        self.n_heads, self.n_kv = cfg.n_heads, cfg.n_kv_heads
        self.hd = cfg.dim // cfg.n_heads
        self.wq = nn.Linear(cfg.dim, cfg.n_heads * self.hd, bias=False)
        self.wk = nn.Linear(cfg.dim, cfg.n_kv_heads * self.hd, bias=False)
        self.wv = nn.Linear(cfg.dim, cfg.n_kv_heads * self.hd, bias=False)
        self.wo = nn.Linear(cfg.n_heads * self.hd, cfg.dim, bias=False)

    def forward(self, x: torch.Tensor, cos: torch.Tensor, sin: torch.Tensor) -> torch.Tensor:
        """Attends over the sequence.

        Args:
            x: Input of shape (batch, time, dim).
            cos: Cosine table from ``rope_tables``.
            sin: Sine table from ``rope_tables``.

        Returns:
            Output of shape (batch, time, dim).
        """
        B, T, _ = x.shape
        q = self.wq(x).view(B, T, self.n_heads, self.hd)
        k = self.wk(x).view(B, T, self.n_kv, self.hd)
        v = self.wv(x).view(B, T, self.n_kv, self.hd)
        q, k = apply_rope(q, cos, sin), apply_rope(k, cos, sin)
        rep = self.n_heads // self.n_kv
        k = k.repeat_interleave(rep, dim=2)
        v = v.repeat_interleave(rep, dim=2)
        out = F.scaled_dot_product_attention(
            q.transpose(1, 2), k.transpose(1, 2), v.transpose(1, 2), is_causal=True
        )
        return self.wo(out.transpose(1, 2).reshape(B, T, -1))


class FeedForward(nn.Module):
    """SwiGLU feed-forward block."""

    def __init__(self, cfg: Config) -> None:
        """Creates the projections.

        Args:
            cfg: Model config.
        """
        super().__init__()
        self.w1 = nn.Linear(cfg.dim, cfg.hidden_dim, bias=False)
        self.w2 = nn.Linear(cfg.hidden_dim, cfg.dim, bias=False)
        self.w3 = nn.Linear(cfg.dim, cfg.hidden_dim, bias=False)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """Applies ``w2(silu(w1 x) * w3 x)``.

        Args:
            x: Input of shape (..., dim).

        Returns:
            Tensor of the same shape.
        """
        return self.w2(F.silu(self.w1(x)) * self.w3(x))


class Block(nn.Module):
    """Pre-norm transformer block: attention then feed-forward, each with a residual."""

    def __init__(self, cfg: Config) -> None:
        """Creates the sublayers.

        Args:
            cfg: Model config.
        """
        super().__init__()
        self.attention_norm = RMSNorm(cfg.dim, cfg.norm_eps)
        self.attention = Attention(cfg)
        self.ffn_norm = RMSNorm(cfg.dim, cfg.norm_eps)
        self.feed_forward = FeedForward(cfg)
        self.dropout = nn.Dropout(cfg.dropout)

    def forward(self, x: torch.Tensor, cos: torch.Tensor, sin: torch.Tensor) -> torch.Tensor:
        """Runs the block.

        Args:
            x: Input of shape (batch, time, dim).
            cos: Cosine table from ``rope_tables``.
            sin: Sine table from ``rope_tables``.

        Returns:
            Output of shape (batch, time, dim).
        """
        x = x + self.dropout(self.attention(self.attention_norm(x), cos, sin))
        return x + self.dropout(self.feed_forward(self.ffn_norm(x)))


class Model(nn.Module):
    """The GPU'cuk transformer; ``export_header`` writes its weights in llm.c's order."""

    def __init__(self, cfg: Config) -> None:
        """Creates and initializes the model.

        The order in which modules are created is part of the result: it decides how the
        random generator is consumed, so changing it changes the trained weights.

        Args:
            cfg: Model config; ``vocab_size`` must be set.
        """
        super().__init__()
        self.cfg = cfg
        self.tok_embeddings = nn.Embedding(cfg.vocab_size, cfg.dim)
        self.dropout = nn.Dropout(cfg.dropout)
        self.layers = nn.ModuleList(Block(cfg) for _ in range(cfg.n_layers))
        self.norm = RMSNorm(cfg.dim, cfg.norm_eps)
        cos, sin = rope_tables(cfg)
        self.register_buffer("cos", cos, persistent=False)
        self.register_buffer("sin", sin, persistent=False)
        self.apply(self._init)
        for name, p in self.named_parameters():
            if name.endswith("wo.weight") or name.endswith("w2.weight"):
                nn.init.normal_(p, std=0.02 / math.sqrt(2 * cfg.n_layers))

    @staticmethod
    def _init(m: nn.Module) -> None:
        """Initializes linear and embedding weights from N(0, 0.02)."""
        if isinstance(m, (nn.Linear, nn.Embedding)):
            nn.init.normal_(m.weight, std=0.02)

    def forward(self, idx: torch.Tensor) -> torch.Tensor:
        """Computes next-token logits.

        Args:
            idx: Token ids of shape (batch, time).

        Returns:
            Logits of shape (batch, time, vocab_size).
        """
        h = self.dropout(self.tok_embeddings(idx))
        for layer in self.layers:
            h = layer(h, self.cos, self.sin)
        return self.norm(h) @ self.tok_embeddings.weight.T


def build_batch(pairs: Sequence[Pair], vocab: Vocab, cfg: Config) -> tuple[torch.Tensor, torch.Tensor]:
    """Encodes every pair as its own sequence.

    The loss only covers the answer and its closing newline; the question is context.

    Args:
        pairs: (question, answer) pairs.
        vocab: Vocabulary.
        cfg: Model config (for ``seq_len``).

    Returns:
        Inputs and targets of shape (len(pairs), seq_len); ignored targets are -100.

    Raises:
        SystemExit: If a sample does not fit in the context.
    """
    n = len(pairs)
    x = torch.zeros(n, cfg.seq_len, dtype=torch.long)
    y = torch.full((n, cfg.seq_len), -100, dtype=torch.long)
    for i, (p, a) in enumerate(pairs):
        prefix = [BOS] + vocab.encode(sample_text(p))
        ids = prefix + vocab.encode(a + "\n")
        if len(ids) > cfg.seq_len:
            raise SystemExit(f"sample too long ({len(ids)} > {cfg.seq_len}): {p} / {a}")
        x[i, : len(ids) - 1] = torch.tensor(ids[:-1])
        tgt = ids[1:]
        start = len(prefix) - 1
        y[i, start : len(tgt)] = torch.tensor(tgt[start:])
    return x, y


@torch.no_grad()
def generate(model: Model, vocab: Vocab, prompt: str, temperature: float, seed: int) -> str:
    """Samples one answer without top-p (used for the training log).

    Args:
        model: Trained model in eval mode.
        vocab: Vocabulary.
        prompt: Question text.
        temperature: 0 picks the most likely character every time.
        seed: Seed for the sampler.

    Returns:
        The answer without its closing newline.
    """
    g = torch.Generator().manual_seed(seed)
    ids = [BOS] + vocab.encode(sample_text(prompt))
    newline = vocab.index["\n"]
    out: list[int] = []
    while len(ids) < model.cfg.seq_len:
        logits = model(torch.tensor([ids]))[0, -1]
        if temperature == 0:
            nxt = int(logits.argmax())
        else:
            nxt = int(torch.multinomial(F.softmax(logits / temperature, -1), 1, generator=g))
        if nxt == newline:
            break
        ids.append(nxt)
        out.append(nxt)
    return vocab.decode(out)


def c_string(s: str) -> str:
    """Quotes text as a C string literal.

    Args:
        s: Text.

    Returns:
        The literal, including quotes.
    """
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def export_header(
    model: Model, vocab: Vocab, langs: Sequence[str], prompts: dict[str, list[str]], path: str | Path, meta: str
) -> int:
    """Writes config, vocabulary, questions and weights as ``model_data.h``.

    Weight order: token embedding; per layer attention norm, wq, wk, wv, wo, ffn norm, w1, w2, w3;
    final norm. ``mapWeights()`` in llm.c reads them back in the same order.

    Args:
        model: Trained model.
        vocab: Vocabulary.
        langs: Language codes in training order (the device's language order).
        prompts: Questions by language code.
        path: Output header.
        meta: One-line summary written as a comment.

    Returns:
        Number of exported floats.
    """
    cfg = model.cfg
    tensors = [model.tok_embeddings.weight]
    for layer in cast(Iterable[Block], model.layers):
        a, f = layer.attention, layer.feed_forward
        tensors += [layer.attention_norm.weight, a.wq.weight, a.wk.weight, a.wv.weight, a.wo.weight,
                    layer.ffn_norm.weight, f.w1.weight, f.w2.weight, f.w3.weight]
    tensors.append(model.norm.weight)
    flat = torch.cat([t.detach().float().flatten() for t in tensors]).tolist()

    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Generated by tools/train.py — do not edit.",
        f"// {meta}",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"#define LLM_DIM {cfg.dim}",
        f"#define LLM_N_LAYERS {cfg.n_layers}",
        f"#define LLM_N_HEADS {cfg.n_heads}",
        f"#define LLM_N_KV_HEADS {cfg.n_kv_heads}",
        f"#define LLM_HIDDEN_DIM {cfg.hidden_dim}",
        f"#define LLM_SEQ_LEN {cfg.seq_len}",
        f"#define LLM_VOCAB_SIZE {cfg.vocab_size}",
        f"#define LLM_NORM_EPS {cfg.norm_eps}f",
        f"#define LLM_ROPE_THETA {cfg.rope_theta}f",
        f"#define LLM_N_PARAMS {len(flat)}",
        f"#define LLM_N_LANGS {len(langs)}",
        f"#define LLM_N_PROMPTS {len(prompts[langs[0]])}",
        "",
        "// Token 0 is BOS (empty string); the rest are single UTF-8 characters.",
        "static const char* const kLlmVocab[LLM_VOCAB_SIZE] = {",
        "    " + ", ".join(c_string(c) for c in vocab.chars) + "};",
        "",
        "static const char* const kLlmLangCodes[LLM_N_LANGS] = {",
        "    " + ", ".join(c_string(code) for code in langs) + "};",
        "",
        "// kLlmPrompts[lang][i] is the same question in each language.",
        "static const char* const kLlmPrompts[LLM_N_LANGS][LLM_N_PROMPTS] = {",
        *["    {" + ", ".join(c_string(p) for p in prompts[code]) + "}," for code in langs],
        "};",
        "",
        "static const float kLlmWeights[LLM_N_PARAMS] = {",
    ]
    for i in range(0, len(flat), 8):
        lines.append("    " + ", ".join(f"{v:.8e}f" for v in flat[i : i + 8]) + ",")
    lines.append("};")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return len(flat)


def parse_args() -> argparse.Namespace:
    """Parses the command line.

    Returns:
        Parsed arguments.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--data", nargs="+", default=[str(ROOT / "data/gpucuk_tr.txt"), str(ROOT / "data/gpucuk_en.txt")],
                    help="data files; their order is the device's language order")
    ap.add_argument("--out", default=str(ROOT / "firmware/src/llm/model_data.h"))
    ap.add_argument("--ckpt", default=str(ROOT / "runs/gpucuk.pt"))
    ap.add_argument("--steps", type=int, default=3000)
    ap.add_argument("--lr", type=float, default=3e-3)
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--seed", type=int, default=1337)
    ap.add_argument("--dim", type=int, default=Config.dim)
    ap.add_argument("--layers", type=int, default=Config.n_layers)
    ap.add_argument("--templates", action=argparse.BooleanOptionalAction, default=True,
                    help="also train on templates_<lang>.txt next to each data file")
    ap.add_argument("--original-repeat", type=int, default=2,
                    help="how many times hand-written answers repeat when templates are used")
    ap.add_argument("--dropout", type=float, default=0.1)
    return ap.parse_args()


def main() -> None:
    """Loads the data, trains, prints sample answers and exports the header and checkpoint."""
    args = parse_args()

    random.seed(args.seed)
    torch.manual_seed(args.seed)
    template_rng = random.Random(args.seed)

    langs: list[str] = []
    prompts: dict[str, list[str]] = {}
    pairs: list[Pair] = []
    n_original = n_template = 0
    for path in args.data:
        code = Path(path).stem.rsplit("_", 1)[-1]
        langs.append(code)
        prompts[code], lang_pairs = load_dataset(path)
        n_original += len(lang_pairs)
        template_path = Path(path).with_name(f"templates_{code}.txt")
        if args.templates and template_path.exists():
            expanded = expand_templates(template_path, prompts[code], lang_pairs, template_rng)
            extra = [(p, a) for p, answers in expanded.items() for a in answers]
            pairs += lang_pairs * args.original_repeat + extra
            n_template += len(extra)
            print(f"{code}: {len(prompts[code])} questions, {len(lang_pairs)} answers + {len(extra)} from templates")
        else:
            pairs += lang_pairs
            print(f"{code}: {len(prompts[code])} questions, {len(lang_pairs)} answers")
    counts = {code: len(p) for code, p in prompts.items()}
    if len(set(counts.values())) != 1:
        raise SystemExit(f"every language needs the same number of questions: {counts}")
    vocab = Vocab(pairs)
    cfg = Config(dim=args.dim, n_layers=args.layers, vocab_size=len(vocab.chars), dropout=args.dropout)
    model = Model(cfg)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"{len(pairs)} training samples, vocabulary {cfg.vocab_size}, {n_params:,} parameters, "
          f"dropout {args.dropout}")

    x_all, y_all = build_batch(pairs, vocab, cfg)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01, betas=(0.9, 0.95))
    warmup = max(1, args.steps // 20)
    t0 = time.time()
    for step in range(1, args.steps + 1):
        lr = args.lr * min(1.0, step / warmup)
        lr *= 0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * min(1.0, step / args.steps)))
        for group in opt.param_groups:
            group["lr"] = lr
        idx = torch.randint(0, len(pairs), (min(args.batch, len(pairs)),))
        logits = model(x_all[idx])
        loss = F.cross_entropy(logits.view(-1, cfg.vocab_size), y_all[idx].view(-1), ignore_index=-100)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
        if step % 200 == 0 or step == 1:
            model.eval()
            with torch.no_grad():
                full = F.cross_entropy(model(x_all).view(-1, cfg.vocab_size), y_all.view(-1), ignore_index=-100)
            model.train()
            print(f"step {step:5d}  loss {full.item():.4f}  lr {lr:.2e}  {time.time() - t0:.0f} s")

    model.eval()
    print("\n--- samples (t=0: most likely answer, s=1/s=2: temperature 0.8 with two seeds) ---")
    for code in langs:
        for p in prompts[code]:
            print(f"[{code}] > {p}")
            print(f"  [t=0]   {generate(model, vocab, p, 0, 0)}")
            for s in (1, 2):
                print(f"  [s={s}]   {generate(model, vocab, p, 0.8, s)}")

    Path(args.ckpt).parent.mkdir(parents=True, exist_ok=True)
    torch.save({"cfg": cfg.__dict__, "vocab": vocab.chars, "langs": langs, "prompts": prompts,
                "state": model.state_dict()}, args.ckpt)
    meta = (f"{'/'.join(langs)}: {counts[langs[0]]} prompts each, {n_original} answers + {n_template} from templates, "
            f"dropout {args.dropout}, {args.steps} steps, final loss {full.item():.4f}")
    n = export_header(model, vocab, langs, prompts, args.out, meta)
    print(f"\n{args.out}: {n:,} floats ({n * 4 / 1024:.0f} KB flash)")


if __name__ == "__main__":
    main()
