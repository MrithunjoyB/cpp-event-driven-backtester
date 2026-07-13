#!/usr/bin/env python3
"""Validate user-supplied OHLCV files without assuming a data provider."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from datetime import date
from pathlib import Path


SCHEMAS = {
    ("Date", "Open", "High", "Low", "Close", "Volume"),
    ("Date", "Open", "High", "Low", "Close", "Volume", "AdjustedClose", "Dividends", "StockSplits"),
}


class DataValidationError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_file(path: Path) -> dict:
    if not path.is_file():
        raise DataValidationError(f"missing input: {path}")
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        fields = tuple(reader.fieldnames or ())
        if fields not in SCHEMAS:
            raise DataValidationError(f"unsupported schema in {path}: {','.join(fields)}")
        rows = list(reader)
    if not rows:
        raise DataValidationError(f"empty input: {path}")

    previous: date | None = None
    for line_number, row in enumerate(rows, start=2):
        try:
            current = date.fromisoformat(row["Date"])
            values = {name: float(row[name]) for name in fields[1:]}
        except (KeyError, ValueError) as error:
            raise DataValidationError(f"invalid value in {path}:{line_number}") from error
        if previous is not None and current <= previous:
            raise DataValidationError(f"dates must be unique and increasing in {path}:{line_number}")
        previous = current
        if any(not math.isfinite(value) for value in values.values()):
            raise DataValidationError(f"non-finite value in {path}:{line_number}")
        prices = [values[name] for name in ("Open", "High", "Low", "Close")]
        if min(prices) <= 0 or values["Volume"] < 0:
            raise DataValidationError(f"invalid price or volume in {path}:{line_number}")
        if values["High"] < max(values["Open"], values["Close"]) or values["Low"] > min(values["Open"], values["Close"]):
            raise DataValidationError(f"invalid OHLC relationship in {path}:{line_number}")
        if "AdjustedClose" in values and values["AdjustedClose"] <= 0:
            raise DataValidationError(f"invalid adjusted close in {path}:{line_number}")
        if "Dividends" in values and values["Dividends"] < 0:
            raise DataValidationError(f"invalid dividend in {path}:{line_number}")
        if "StockSplits" in values and values["StockSplits"] < 0:
            raise DataValidationError(f"invalid split in {path}:{line_number}")

    return {
        "classification": "user_supplied",
        "path": str(path),
        "ticker": path.stem,
        "schema": ",".join(fields),
        "row_count": len(rows),
        "date_range": {"start": rows[0]["Date"], "end": rows[-1]["Date"]},
        "sha256": sha256_file(path),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--json-output", type=Path)
    args = parser.parse_args()
    files: list[Path] = []
    for path in args.paths:
        files.extend(sorted(path.glob("*.csv")) if path.is_dir() else [path])
    try:
        records = [validate_file(path.resolve()) for path in files]
    except DataValidationError as error:
        print(f"Market-data validation failed: {error}")
        return 1
    if not records:
        print("Market-data validation failed: no CSV inputs found")
        return 1
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json.dumps({"inputs": records}, indent=2, sort_keys=True) + "\n")
    for record in records:
        print(f"{record['ticker']}: {record['row_count']} rows {record['sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
