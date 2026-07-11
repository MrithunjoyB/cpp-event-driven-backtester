from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

import pandas as pd
import yfinance as yf


TICKERS = ["AAPL", "MSFT", "SPY", "TSLA", "BTC-USD"]
START = "2020-01-01"
END = "2025-12-31"
SCHEMA = ["Date", "Open", "High", "Low", "Close", "Volume", "AdjustedClose", "Dividends", "StockSplits"]


def normalize_download(frame: pd.DataFrame, ticker: str) -> pd.DataFrame:
    if frame.empty:
        raise ValueError(f"No data returned for {ticker}")
    data = frame.copy()
    if isinstance(data.columns, pd.MultiIndex):
        if ticker in data.columns.get_level_values(-1):
            data = data.xs(ticker, axis=1, level=-1)
        else:
            data.columns = data.columns.get_level_values(0)
    data = data.reset_index()
    date_column = "Date" if "Date" in data.columns else data.columns[0]
    data = data.rename(columns={date_column: "Date", "Adj Close": "AdjustedClose", "Stock Splits": "StockSplits"})
    for name in ("Dividends", "StockSplits"):
        if name not in data.columns:
            data[name] = 0.0
    if "AdjustedClose" not in data.columns:
        raise ValueError(f"AdjustedClose is unavailable for {ticker}; adjustment provenance would be ambiguous")
    missing = set(SCHEMA) - set(data.columns)
    if missing:
        raise ValueError(f"Missing columns for {ticker}: {sorted(missing)}")
    data = data[SCHEMA]
    dates = pd.to_datetime(data["Date"], utc=True, errors="raise").dt.tz_convert(None).dt.normalize()
    data["Date"] = dates.dt.strftime("%Y-%m-%d")
    numeric = SCHEMA[1:]
    data[numeric] = data[numeric].apply(pd.to_numeric, errors="raise")
    if data[numeric].isna().any().any():
        raise ValueError(f"Non-finite numeric data for {ticker}")
    if data["Date"].duplicated().any():
        raise ValueError(f"Duplicate dates returned for {ticker}")
    data = data.sort_values("Date", kind="stable").reset_index(drop=True)
    if (data[["Open", "High", "Low", "Close", "AdjustedClose"]] <= 0).any().any():
        raise ValueError(f"Non-positive price returned for {ticker}")
    if (data[["Volume", "Dividends", "StockSplits"]] < 0).any().any():
        raise ValueError(f"Negative volume or corporate action returned for {ticker}")
    return data


def write_dataset(data: pd.DataFrame, ticker: str, output: Path, start: str, end: str, force: bool) -> None:
    expected_header = ",".join(SCHEMA)
    if output.exists() and not force:
        existing_header = output.open().readline().strip()
        if existing_header != expected_header:
            raise FileExistsError(f"Refusing to overwrite incompatible {output}; rerun with --force")
    data.to_csv(output, index=False)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    metadata = {
        "source_ticker": ticker,
        "data_source": "yfinance",
        "requested_start": start,
        "requested_end_exclusive": end,
        "actual_start": data["Date"].iloc[0],
        "actual_end": data["Date"].iloc[-1],
        "download_timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "timezone_normalization": "UTC input normalized to timezone-naive civil date",
        "adjustment_provenance": "Yahoo Finance AdjustedClose; Dividends and StockSplits requested with actions=True",
        "csv_sha256": digest,
        "schema_version": 3,
    }
    output.with_suffix(".metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--start", default=START)
    parser.add_argument("--end", default=END)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    data_dir = Path(__file__).resolve().parents[1] / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    for ticker in TICKERS:
        print(f"Downloading {ticker}...")
        frame = yf.download(ticker, start=args.start, end=args.end, auto_adjust=False, actions=True, progress=False)
        normalized = normalize_download(frame, ticker)
        output = data_dir / f"{ticker}.csv"
        write_dataset(normalized, ticker, output, args.start, args.end, args.force)
        print(f"  Saved {output}")


if __name__ == "__main__":
    main()
