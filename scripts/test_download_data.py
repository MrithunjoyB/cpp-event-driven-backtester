from __future__ import annotations

import sys
from pathlib import Path

import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent))
from download_data import SCHEMA, normalize_download


def main() -> int:
    columns = pd.MultiIndex.from_tuples([
        ("Open", "TEST"), ("High", "TEST"), ("Low", "TEST"), ("Close", "TEST"),
        ("Adj Close", "TEST"), ("Volume", "TEST"), ("Dividends", "TEST"), ("Stock Splits", "TEST")])
    frame = pd.DataFrame([
        [101, 102, 100, 101, 101, 1000, 0, 0],
        [100, 101, 99, 100, 100, 900, 1, 2],
    ], index=pd.to_datetime(["2024-01-02", "2024-01-01"]), columns=columns)
    frame.index.name = "Date"
    first = normalize_download(frame, "TEST")
    second = normalize_download(frame, "TEST")
    assert list(first.columns) == SCHEMA
    assert first["Date"].tolist() == ["2024-01-01", "2024-01-02"]
    assert first.to_csv(index=False) == second.to_csv(index=False)
    duplicate = pd.concat([frame, frame.iloc[:1]])
    try:
        normalize_download(duplicate, "TEST")
    except ValueError as error:
        assert "Duplicate" in str(error)
    else:
        raise AssertionError("duplicate dates were accepted")
    print("download_data fixture tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
