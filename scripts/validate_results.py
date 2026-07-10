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
    is_canonical_portfolio = "portfolio" in path.relative_to(RESULTS).parts[:1]
    required_by_suffix = {
        "equity_curve.csv": {"date", "portfolio_value", "cash", "holdings", "total_return", "drawdown"},
        "trades.csv": {"date", "ticker", "strategy", "action", "price", "quantity", "cost", "slippage", "portfolio_value"},
        "portfolio_equity_curve.csv": {"date", "portfolio_value", "cash", "total_holdings_value", "total_return", "drawdown", "gross_exposure"},
        "portfolio_positions.csv": {"date", "ticker", "quantity", "price", "market_value", "target_weight", "actual_weight", "rebalance_id"},
        "portfolio_fills.csv": {"rebalance_id", "date", "ticker", "side", "quantity", "price", "transaction_cost", "slippage_cost", "cash_after"},
        "portfolio_rebalances.csv": {"rebalance_id", "date", "policy_name", "frequency", "turnover"},
        "portfolio_allocation_weights.csv": {"rebalance_id", "policy_name", "ticker", "target_weight"},
        "portfolio_costs.csv": {"rebalance_id", "date", "ticker", "transaction_cost", "slippage_cost", "total_cost"},
    }
    header = set(rows[0].keys()) if rows else set()
    for suffix, required in required_by_suffix.items():
        if path.name.endswith(suffix):
            if path.name.startswith("portfolio_") and suffix in {"equity_curve.csv", "trades.csv"}:
                continue
            if path.name.startswith("portfolio_") and not is_canonical_portfolio:
                continue
            missing = required - header
            if missing:
                issues.append(f"{path}: missing required columns {sorted(missing)}")
    if path.name.endswith("performance_summary.csv") and path.name != "portfolio_performance_summary.csv":
        required = {"ticker", "strategy", "total_return", "benchmark_gross_return", "benchmark_net_return", "excess_return", "max_drawdown", "win_rate", "num_trades"}
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
    seen_rebalance_ids: set[int] = set()
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
        cash_after = as_float(row.get("cash_after", ""))
        if cash_after is not None and cash_after < -1e-4:
            issues.append(f"{path}:{line_number}: negative cash_after {cash_after}")

        drawdown = as_float(row.get("drawdown", ""))
        if drawdown is not None and drawdown > 1e-9:
            issues.append(f"{path}:{line_number}: drawdown greater than zero {drawdown}")

        quantity = as_float(row.get("quantity", ""))
        if quantity is not None and quantity < -1e-9:
            issues.append(f"{path}:{line_number}: negative quantity {quantity}")

        price = as_float(row.get("price", ""))
        if price is not None and price <= 0:
            issues.append(f"{path}:{line_number}: missing or invalid price {price}")

        for cost_col in ("transaction_cost", "slippage_cost", "total_cost", "cost", "slippage"):
            cost = as_float(row.get(cost_col, ""))
            if cost is not None and cost < -1e-9:
                issues.append(f"{path}:{line_number}: negative {cost_col} {cost}")

        for weight_col in ("target_weight", "actual_weight"):
            weight = as_float(row.get(weight_col, ""))
            if weight is not None and not (-1e-9 <= weight <= 1.000001):
                issues.append(f"{path}:{line_number}: {weight_col} outside [0,1] {weight}")

        gross_exposure = as_float(row.get("gross_exposure", ""))
        if gross_exposure is not None and gross_exposure > 1.000001:
            issues.append(f"{path}:{line_number}: gross exposure above no-leverage bound {gross_exposure}")

        turnover = as_float(row.get("turnover", ""))
        if path.name == "portfolio_rebalances.csv" and turnover is not None and not (0.0 <= turnover <= 10.0):
            issues.append(f"{path}:{line_number}: impossible turnover {turnover}")

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
            total_holdings_value = as_float(row.get("total_holdings_value", ""))
            if cash is not None and total_holdings_value is not None and abs((cash + total_holdings_value) - portfolio_value) > 1e-3:
                issues.append(f"{path}:{line_number}: cash+total_holdings_value does not equal portfolio value")
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

        if path.name == "portfolio_equity_curve.csv" and row.get("date"):
            key = (row["date"],)
            if key in seen_date_keys:
                issues.append(f"{path}:{line_number}: duplicate portfolio equity date {key}")
            seen_date_keys.add(key)

        rid = row.get("rebalance_id", "")
        if path.name == "portfolio_rebalances.csv" and rid != "":
            try:
                rid_int = int(float(rid))
            except ValueError:
                issues.append(f"{path}:{line_number}: invalid rebalance_id {rid}")
            else:
                if rid_int in seen_rebalance_ids:
                    issues.append(f"{path}:{line_number}: duplicate rebalance_id {rid_int}")
                seen_rebalance_ids.add(rid_int)

    if path.name == "portfolio_allocation_weights.csv":
        sums: dict[str, float] = {}
        for row in rows:
            rid = row.get("rebalance_id", "")
            sums[rid] = sums.get(rid, 0.0) + (as_float(row.get("target_weight", "")) or 0.0)
        for rid, total in sums.items():
            if total > 1.000001:
                issues.append(f"{path}: rebalance_id {rid} target weights sum above 1: {total}")

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
    global RESULTS
    if len(sys.argv) > 1:
        RESULTS = Path(sys.argv[1]).resolve()
    issues: list[str] = []
    if not RESULTS.exists():
        print(f"Missing results directory: {RESULTS}")
        return 1
    for path in sorted(RESULTS.rglob("*.csv")):
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
