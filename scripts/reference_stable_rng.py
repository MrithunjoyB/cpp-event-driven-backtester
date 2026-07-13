#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path

MASK32 = 0xFFFFFFFF


class Mt19937:
    def __init__(self, seed: int) -> None:
        self.state = [0] * 624
        self.state[0] = seed & MASK32
        for index in range(1, 624):
            previous = self.state[index - 1]
            self.state[index] = (1812433253 * (previous ^ (previous >> 30)) + index) & MASK32
        self.index = 624

    def _twist(self) -> None:
        for index in range(624):
            value = (self.state[index] & 0x80000000) | (self.state[(index + 1) % 624] & 0x7FFFFFFF)
            shifted = value >> 1
            if value & 1:
                shifted ^= 0x9908B0DF
            self.state[index] = self.state[(index + 397) % 624] ^ shifted
        self.index = 0

    def word(self) -> int:
        if self.index >= 624:
            self._twist()
        value = self.state[self.index]
        self.index += 1
        value ^= value >> 11
        value ^= (value << 7) & 0x9D2C5680
        value ^= (value << 15) & 0xEFC60000
        value ^= value >> 18
        return value & MASK32


def portable_bounded_v1(engine: Mt19937, bound: int) -> tuple[int, int]:
    if not 0 < bound <= MASK32:
        raise ValueError("bound must be in [1, 2^32-1]")
    threshold = ((-bound) & MASK32) % bound
    consumed = 0
    while True:
        word = engine.word()
        consumed += 1
        product = word * bound
        if (product & MASK32) >= threshold:
            return (product >> 32) & MASK32, consumed


BOUNDS = [1, 2, 3, 5, 7, 10, 31, 32, 33, 255, 256, 257, 1000, 2190, 3653, 0x80000001, MASK32]
SEEDS = [0, 1, 42, 5489]
OUTPUTS_PER_VECTOR = 128


def generate(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["fixture_version", "seed", "bound", "ordinal", "value", "words_consumed", "cumulative_words"])
        for seed in SEEDS:
            for bound in BOUNDS:
                engine = Mt19937(seed)
                cumulative = 0
                for ordinal in range(OUTPUTS_PER_VECTOR):
                    value, consumed = portable_bounded_v1(engine, bound)
                    cumulative += consumed
                    writer.writerow(["portable_bounded_v1", seed, bound, ordinal, value, consumed, cumulative])


def validate(path: Path) -> None:
    rows = list(csv.DictReader(path.open()))
    expected_rows = len(SEEDS) * len(BOUNDS) * OUTPUTS_PER_VECTOR
    if len(rows) != expected_rows:
        raise AssertionError(f"expected {expected_rows} vectors, found {len(rows)}")
    engines: dict[tuple[int, int], Mt19937] = {}
    cumulative: dict[tuple[int, int], int] = {}
    rejection_seen = False
    for row in rows:
        key = int(row["seed"]), int(row["bound"])
        engine = engines.setdefault(key, Mt19937(key[0]))
        value, consumed = portable_bounded_v1(engine, key[1])
        cumulative[key] = cumulative.get(key, 0) + consumed
        if value != int(row["value"]) or consumed != int(row["words_consumed"]):
            raise AssertionError(f"reference mismatch at {key}, ordinal {row['ordinal']}")
        if cumulative[key] != int(row["cumulative_words"]):
            raise AssertionError("engine consumption mismatch")
        rejection_seen |= consumed > 1
    if not rejection_seen:
        raise AssertionError("fixture does not exercise rejection")
    print(f"Stable RNG Python reference passed: {len(rows)} outputs")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", type=Path, default=Path("tests/fixtures/rng/portable_bounded_v1.csv"))
    parser.add_argument("--generate", action="store_true")
    args = parser.parse_args()
    if args.generate:
        generate(args.fixture)
    validate(args.fixture)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
