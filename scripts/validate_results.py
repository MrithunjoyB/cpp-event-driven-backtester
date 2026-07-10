from __future__ import annotations

import csv
import math
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"


def as_float(value: str) -> float | None:
    if value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def check_required_columns(path: Path, rows: list[dict[str, str]], issues: list[str]) -> None:
    required_by_suffix = {
        "equity_curve.csv": {"date", "portfolio_value", "cash", "holdings", "total_return", "drawdown"},
        "trades.csv": {"date", "ticker", "strategy", "action", "price", "quantity", "cost", "slippage", "portfolio_value"},
        "performance_summary.csv": {"ticker", "strategy", "total_return", "benchmark_gross_return", "benchmark_net_return", "excess_return", "max_drawdown", "win_rate", "num_trades"},
    }
    header = set(rows[0].keys()) if rows else set()
    for suffix, required in required_by_suffix.items():
        if path.name.endswith(suffix):
            missing = required - header
            if missing:
                issues.append(f"{path}: missing required columns {sorted(missing)}")


def validate_file(path: Path, issues: list[str]) -> None:
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    if not rows:
        issues.append(f"{path}: empty CSV")
        return
    check_required_columns(path, rows, issues)

    seen_date_keys: set[tuple[str, ...]] = set()
    last_equity_value = None
    for line_number, row in enumerate(rows, start=2):
        for key, value in row.items():
            numeric = as_float(value or "")
            if numeric is not None and not math.isfinite(numeric):
                issues.append(f"{path}:{line_number}: non-finite {key}={value}")

        holdings = as_float(row.get("holdings", ""))
        if holdings is not None and holdings < -1e-6:
            issues.append(f"{path}:{line_number}: negative holdings {holdings}")

        cash = as_float(row.get("cash", ""))
        if cash is not None and cash < -1e-4:
            issues.append(f"{path}:{line_number}: negative cash {cash}")

        drawdown = as_float(row.get("drawdown", ""))
        if drawdown is not None and drawdown > 1e-9:
            issues.append(f"{path}:{line_number}: drawdown greater than zero {drawdown}")

        win_rate = as_float(row.get("win_rate", ""))
        if win_rate is not None and not (0.0 <= win_rate <= 1.0):
            issues.append(f"{path}:{line_number}: win rate outside [0,1] {win_rate}")

        trade_count = as_float(row.get("num_trades", row.get("trade_count", "")))
        if trade_count is not None and trade_count < 0:
            issues.append(f"{path}:{line_number}: negative trade count {trade_count}")

        portfolio_value = as_float(row.get("portfolio_value", ""))
        if portfolio_value is not None:
            if portfolio_value < -1e-6:
                issues.append(f"{path}:{line_number}: negative portfolio value {portfolio_value}")
            if cash is not None and holdings is not None and abs((cash + holdings) - portfolio_value) > 1e-3:
                issues.append(f"{path}:{line_number}: cash+holdings does not equal portfolio value")
            last_equity_value = portfolio_value

        if path.name.endswith("equity_curve.csv") and row.get("date"):
            key = (
                row.get("ticker", ""),
                row.get("strategy", ""),
                row.get("parameter_set", ""),
                row.get("window_id", ""),
                row["date"],
            )
            if key in seen_date_keys:
                issues.append(f"{path}:{line_number}: duplicate date key {key}")
            seen_date_keys.add(key)

    if path.name == "equity_curve.csv":
        summary_path = path.with_name("performance_summary.csv")
        if summary_path.exists() and last_equity_value is not None:
            with summary_path.open(newline="") as f:
                summary = next(csv.DictReader(f), None)
            if summary:
                reported_return = as_float(summary.get("total_return", ""))
                if reported_return is not None:
                    inferred = (last_equity_value / 100000.0) - 1.0
                    if abs(inferred - reported_return) > 1e-4:
                        issues.append(f"{path}: ending value inconsistent with performance_summary.csv")


def main() -> int:
    issues: list[str] = []
    if not RESULTS.exists():
        print(f"Missing results directory: {RESULTS}")
        return 1
    for path in sorted(RESULTS.glob("*.csv")):
        validate_file(path, issues)
    if issues:
        print("Result validation failed:")
        for issue in issues:
            print(f"- {issue}")
        return 1
    print("Result validation passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

