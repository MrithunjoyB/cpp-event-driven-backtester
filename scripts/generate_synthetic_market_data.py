#!/usr/bin/env python3
"""Generate independent deterministic OHLCV fixtures for public reconstruction."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from datetime import date, timedelta
from pathlib import Path


GENERATOR_ID = "synthetic_market_data_v1"
GENERATOR_VERSION = 1
FIXTURE_SEED = 20260713
START_DATE = date(2019, 1, 1)
END_DATE = date(2025, 12, 31)
SCHEMA = ["Date", "Open", "High", "Low", "Close", "Volume", "AdjustedClose", "Dividends", "StockSplits"]

ASSETS = {
    "SYN_EQ_A": {"calendar": "synthetic_weekday", "start_micros": 100_000_000, "drift": 180, "vol": 12_000, "seed": 101},
    "SYN_EQ_B": {"calendar": "synthetic_weekday", "start_micros": 80_000_000, "drift": 90, "vol": 9_000, "seed": 211},
    "SYN_EQ_C": {"calendar": "synthetic_weekday", "start_micros": 45_000_000, "drift": 220, "vol": 24_000, "seed": 307},
    "SYN_BENCH": {"calendar": "synthetic_weekday", "start_micros": 200_000_000, "drift": 130, "vol": 7_000, "seed": 401},
    "SYN_CRYPTO": {"calendar": "synthetic_seven_day", "start_micros": 5_000_000_000, "drift": 240, "vol": 31_000, "seed": 503},
}

SYNTHETIC_CLOSURES = {
    date(2019, 1, 21), date(2019, 7, 4), date(2019, 12, 25),
    date(2020, 1, 20), date(2020, 7, 3), date(2020, 12, 25),
    date(2021, 1, 18), date(2021, 7, 5), date(2021, 12, 24),
    date(2022, 1, 17), date(2022, 7, 4), date(2022, 12, 26),
    date(2023, 1, 16), date(2023, 7, 4), date(2023, 12, 25),
    date(2024, 1, 15), date(2024, 7, 4), date(2024, 12, 25),
    date(2025, 1, 20), date(2025, 7, 4), date(2025, 12, 25),
}

MISSING_DATES = {
    "SYN_EQ_A": {date(2021, 3, 17), date(2024, 8, 14)},
    "SYN_EQ_B": {date(2020, 6, 11), date(2023, 10, 19)},
    "SYN_EQ_C": {date(2022, 2, 8), date(2025, 5, 6)},
    "SYN_BENCH": {date(2020, 9, 9)},
    "SYN_CRYPTO": {date(2022, 11, 13)},
}

SPLITS = {
    "SYN_EQ_A": {date(2022, 6, 15): 2.0},
    "SYN_EQ_B": {date(2024, 4, 1): 3.0},
    "SYN_EQ_C": {date(2023, 9, 12): 0.5},
    "SYN_BENCH": {},
    "SYN_CRYPTO": {},
}

DIVIDENDS = {
    "SYN_EQ_A": {date(2020, 3, 16): 0.18, date(2021, 6, 15): 0.22, date(2023, 3, 15): 0.14, date(2025, 6, 16): 0.17},
    "SYN_EQ_B": {date(2019, 9, 16): 0.24, date(2022, 9, 15): 0.28, date(2024, 9, 16): 0.12},
    "SYN_EQ_C": {date(2021, 12, 15): 0.08},
    "SYN_BENCH": {date(2020, 12, 15): 0.35, date(2022, 12, 15): 0.40, date(2024, 12, 16): 0.45},
    "SYN_CRYPTO": {},
}


def _lcg(value: int) -> int:
    return (1_664_525 * value + 1_013_904_223) & 0xFFFFFFFF


def _format_price(micros: int) -> str:
    return f"{micros // 1_000_000}.{micros % 1_000_000:06d}"


def _format_action(value: float) -> str:
    return f"{value:.6f}"


def _dates_for(asset: str) -> list[date]:
    calendar = ASSETS[asset]["calendar"]
    dates: list[date] = []
    current = START_DATE
    while current <= END_DATE:
        eligible = calendar == "synthetic_seven_day" or (current.weekday() < 5 and current not in SYNTHETIC_CLOSURES)
        if eligible and current not in MISSING_DATES[asset]:
            dates.append(current)
        current += timedelta(days=1)
    return dates


def _regime_drift(index: int, base_drift: int) -> tuple[int, int]:
    phase = (index // 280) % 6
    drift_adjustments = (350, -120, 0, 620, -760, 170)
    volatility_multipliers = (80, 105, 55, 140, 185, 95)
    return base_drift + drift_adjustments[phase], volatility_multipliers[phase]


def _generate_rows(asset: str) -> list[list[str]]:
    spec = ASSETS[asset]
    state = FIXTURE_SEED ^ int(spec["seed"])
    raw_close = int(spec["start_micros"])
    adjusted_close = raw_close
    anchor = raw_close
    rows: list[list[str]] = []

    for index, current in enumerate(_dates_for(asset)):
        split = SPLITS[asset].get(current, 0.0)
        ratio_micros = int(split * 1_000_000) if split else 1_000_000
        base_raw = max(1_000_000, raw_close * 1_000_000 // ratio_micros)

        state = _lcg(state)
        centered = ((state >> 8) % 2001) - 1000
        drift, vol_multiplier = _regime_drift(index, int(spec["drift"]))
        noise = centered * int(spec["vol"]) * vol_multiplier // 100_000
        mean_reversion = 0
        if (index // 280) % 6 == 2:
            mean_reversion = max(-5_000, min(5_000, (anchor - base_raw) * 14_000 // max(anchor, 1)))
        return_ppm = max(-180_000, min(180_000, drift + noise + mean_reversion))

        state = _lcg(state)
        gap_ppm = (((state >> 9) % 1201) - 600) * vol_multiplier // 100
        open_price = max(1_000_000, base_raw * (1_000_000 + gap_ppm) // 1_000_000)
        close_price = max(1_000_000, base_raw * (1_000_000 + return_ppm) // 1_000_000)

        state = _lcg(state)
        spread_ppm = 1_500 + ((state >> 10) % 8_001) * vol_multiplier // 100
        high = max(open_price, close_price) * (1_000_000 + spread_ppm) // 1_000_000
        low = max(1, min(open_price, close_price) * max(1, 1_000_000 - spread_ppm) // 1_000_000)

        dividend = DIVIDENDS[asset].get(current, 0.0)
        dividend_micros = int(dividend * 1_000_000)
        total_value_micros = ratio_micros * (close_price + dividend_micros) // 1_000_000
        adjusted_close = max(1, adjusted_close * total_value_micros // max(raw_close, 1))

        state = _lcg(state)
        base_volume = 2_000_000 if spec["calendar"] == "synthetic_weekday" else 250_000
        volume = base_volume + (state % (base_volume * 4)) + abs(return_ppm) * 31

        rows.append([
            current.isoformat(), _format_price(open_price), _format_price(high), _format_price(low),
            _format_price(close_price), str(volume), _format_price(adjusted_close),
            _format_action(dividend), _format_action(split),
        ])
        raw_close = close_price
        if index % 280 == 279:
            anchor = raw_close

    return rows


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(output_directory: Path) -> dict:
    output_directory.mkdir(parents=True, exist_ok=True)
    metadata_assets = []
    for asset in ASSETS:
        rows = _generate_rows(asset)
        path = output_directory / f"{asset}.csv"
        with path.open("w", newline="") as stream:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(SCHEMA)
            writer.writerows(rows)
        metadata_assets.append({
            "asset": asset,
            "calendar": ASSETS[asset]["calendar"],
            "classification": "synthetic",
            "corporate_actions": {
                "dividends": {key.isoformat(): value for key, value in sorted(DIVIDENDS[asset].items())},
                "splits": {key.isoformat(): value for key, value in sorted(SPLITS[asset].items())},
            },
            "date_range": {"start": rows[0][0], "end": rows[-1][0]},
            "filename": path.name,
            "missing_dates": sorted(value.isoformat() for value in MISSING_DATES[asset]),
            "row_count": len(rows),
            "sha256": _sha256(path),
        })

    metadata = {
        "classification": "synthetic",
        "copyright": "Copyright 2026 Mrithunjoy Basumatary",
        "description": "Independent deterministic fixtures; no provider data or real return path is used.",
        "fixture_seed": FIXTURE_SEED,
        "generator_id": GENERATOR_ID,
        "generator_version": GENERATOR_VERSION,
        "license": "Apache-2.0",
        "network_required": False,
        "schema": SCHEMA,
        "synthetic_closures": sorted(value.isoformat() for value in SYNTHETIC_CLOSURES),
        "assets": metadata_assets,
    }
    (output_directory / "metadata.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-directory", type=Path, default=Path("data/synthetic"))
    args = parser.parse_args()
    metadata = generate(args.output_directory)
    for asset in metadata["assets"]:
        print(f"{asset['asset']}: {asset['row_count']} rows {asset['sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
