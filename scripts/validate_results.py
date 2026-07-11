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
    relative_parts = path.relative_to(RESULTS).parts
    is_canonical_portfolio = not (relative_parts and relative_parts[0] == "research" and "portfolio" in relative_parts)
    required_by_suffix = {
        "equity_curve.csv": {"date", "portfolio_value", "cash", "holdings", "total_return", "drawdown"},
        "trades.csv": {"date", "ticker", "strategy", "action", "price", "quantity", "cost", "slippage", "portfolio_value"},
        "portfolio_equity_curve.csv": {"date", "portfolio_value", "cash", "total_holdings_value", "total_return", "drawdown", "gross_exposure"},
        "portfolio_positions.csv": {"date", "ticker", "quantity", "price", "market_value", "target_weight", "actual_weight", "rebalance_id"},
        "portfolio_fills.csv": {"rebalance_id", "date", "ticker", "side", "quantity", "price", "transaction_cost", "slippage_cost", "cash_after"},
        "portfolio_allocation_weights.csv": {"rebalance_id", "policy_name", "ticker", "target_weight"},
        "portfolio_costs.csv": {"rebalance_id", "date", "ticker", "transaction_cost", "slippage_cost", "total_cost"},
        "portfolio_valuations.csv": {"date", "ticker", "tradable", "has_bar", "mark_price", "mark_source", "stale_age_days", "position_quantity", "marked_value", "actual_weight"},
        "portfolio_corporate_actions.csv": {"action_date", "ticker", "action_type", "value", "quantity_before", "quantity_after", "cash_effect", "portfolio_value_before", "portfolio_value_after", "adjustment_policy", "source"},
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
        required = {"schema_version", "ticker", "strategy", "total_return", "benchmark_ticker", "benchmark_execution_policy", "benchmark_cost_policy", "excess_return_basis", "benchmark_gross_return", "benchmark_net_return", "excess_return", "max_drawdown", "win_rate", "num_trades"}
        missing = required - header
        if missing:
            issues.append(f"{path}: missing required columns {sorted(missing)}")


def validate_file(path: Path, issues: list[str]) -> None:
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    if not rows:
        if path.name == "portfolio_corporate_actions.csv" and reader.fieldnames and {
            "action_date", "ticker", "action_type", "value", "adjustment_policy", "source"
        }.issubset(reader.fieldnames):
            return
        issues.append(f"{path}: empty CSV")
        return
    relative_parts = path.relative_to(RESULTS).parts
    if relative_parts and relative_parts[0] == "research" and "portfolio" in relative_parts:
        return
    check_required_columns(path, rows, issues)

    if path.name == "portfolio_performance_summary.csv" and rows[0].get("schema_version") == "3":
        required = {"calendar_mode", "valuation_frequency", "observations_per_year", "annualization_method",
                    "total_valuation_observations", "weekend_observations", "stale_mark_observations",
                    "stale_mark_policy", "max_stale_calendar_days"}
        missing = required - set(rows[0])
        if missing:
            issues.append(f"{path}: missing schema-v3 summary columns {sorted(missing)}")
        if rows[0].get("calendar_mode") != "union":
            issues.append(f"{path}: schema-v3 portfolio must identify union calendar mode")
        observations = as_float(rows[0].get("observations_per_year", ""))
        if observations is None or observations <= 0:
            issues.append(f"{path}: invalid observations_per_year")

    if path.name == "portfolio_rebalances.csv":
        if "scheduled_rebalance_date" in rows[0]:
            required = {"rebalance_id", "scheduled_rebalance_date", "decision_date", "execution_date",
                        "deferred_asset_count", "skipped_asset_count", "partial_rebalance", "closed_asset_policy", "turnover"}
        else:
            required = {"rebalance_id", "date", "policy_name", "frequency", "turnover"}
        missing = required - set(rows[0])
        if missing:
            issues.append(f"{path}: missing required rebalance columns {sorted(missing)}")

    if path.name == "portfolio_valuations.csv":
        seen: set[tuple[str, str]] = set()
        for line_number, row in enumerate(rows, start=2):
            key = (row.get("date", ""), row.get("ticker", ""))
            if key in seen:
                issues.append(f"{path}:{line_number}: duplicate union-calendar valuation {key}")
            seen.add(key)
            stale_age = as_float(row.get("stale_age_days", ""))
            if stale_age is None or stale_age < 0:
                issues.append(f"{path}:{line_number}: impossible stale age")
            source = row.get("mark_source", "")
            if not source:
                issues.append(f"{path}:{line_number}: missing mark source")
            tradable = row.get("tradable") == "1"
            has_bar = row.get("has_bar") == "1"
            if tradable and not has_bar:
                issues.append(f"{path}:{line_number}: tradable asset has no bar")

    if path.name == "portfolio_fills.csv" and "execution_date" in rows[0]:
        valuation_path = path.with_name("portfolio_valuations.csv")
        if valuation_path.exists():
            with valuation_path.open(newline="") as stream:
                tradable = {(row["date"], row["ticker"]): row["tradable"] == "1" for row in csv.DictReader(stream)}
            for line_number, row in enumerate(rows, start=2):
                key = (row.get("execution_date", ""), row.get("ticker", ""))
                if not tradable.get(key, False):
                    issues.append(f"{path}:{line_number}: fill occurred on non-tradable date {key}")

    if path.name == "portfolio_corporate_actions.csv":
        dividend_keys: set[tuple[str, str]] = set()
        for line_number, row in enumerate(rows, start=2):
            value = as_float(row.get("value", ""))
            if value is None or value <= 0:
                issues.append(f"{path}:{line_number}: invalid corporate-action value")
            if row.get("action_type") == "cash_dividend" and as_float(row.get("cash_effect", "")) not in (None, 0.0):
                key = (row.get("action_date", ""), row.get("ticker", ""))
                if key in dividend_keys:
                    issues.append(f"{path}:{line_number}: duplicate dividend credit {key}")
                dividend_keys.add(key)

    if path.name in {"windows.csv", "walk_forward_windows.csv"} and "schema_version" in rows[0]:
        required = {
            "window_mode", "ticker", "strategy", "window_id", "train_start", "train_end",
            "test_start", "test_end", "train_observations", "test_observations",
            "starting_capital", "ending_capital", "continuity_policy",
            "boundary_position_policy", "boundary_liquidation_costs", "linked_return",
            "cumulative_oos_return", "benchmark_ticker", "benchmark_execution_policy",
            "benchmark_cost_policy", "excess_return_basis",
        }
        missing = required - set(rows[0].keys())
        if missing:
            issues.append(f"{path}: missing schema-v2 walk-forward columns {sorted(missing)}")
        previous: dict[tuple[str, str], tuple[str, float]] = {}
        initial: dict[tuple[str, str], float] = {}
        for line_number, row in enumerate(rows, start=2):
            key = (row.get("ticker", ""), row.get("strategy", ""))
            start = as_float(row.get("starting_capital", ""))
            end = as_float(row.get("ending_capital", ""))
            if start is None or end is None:
                issues.append(f"{path}:{line_number}: invalid window capital")
                continue
            if row.get("train_end", "") >= row.get("test_start", ""):
                issues.append(f"{path}:{line_number}: training and testing periods overlap")
            if key in previous:
                prior_test_end, prior_ending = previous[key]
                if prior_test_end >= row.get("test_start", ""):
                    issues.append(f"{path}:{line_number}: test windows overlap")
                if row.get("continuity_policy") == "continuous_capital" and abs(prior_ending - start) > 1e-3:
                    issues.append(f"{path}:{line_number}: previous ending capital does not equal next starting capital")
            initial.setdefault(key, start)
            cumulative = as_float(row.get("cumulative_oos_return", ""))
            expected = end / initial[key] - 1.0 if initial[key] > 0 else 0.0
            if row.get("continuity_policy") == "continuous_capital" and cumulative is not None and abs(cumulative - expected) > 1e-4:
                issues.append(f"{path}:{line_number}: cumulative OOS return does not reconcile")
            if not row.get("benchmark_ticker") or not row.get("benchmark_execution_policy"):
                issues.append(f"{path}:{line_number}: missing benchmark metadata")
            previous[key] = (row.get("test_end", ""), end)

    if path.name == "regime_assignments.csv" and "schema_version" in rows[0]:
        required = {"trend_regime", "volatility_regime", "volatility_value", "volatility_threshold", "information_cutoff", "volatility_threshold_method"}
        missing = required - set(rows[0].keys())
        if missing:
            issues.append(f"{path}: missing causal regime columns {sorted(missing)}")
        for line_number, row in enumerate(rows, start=2):
            if row.get("information_cutoff", "") > row.get("date", ""):
                issues.append(f"{path}:{line_number}: regime cutoff is in the future")
            if row.get("volatility_threshold_method") != "expanding_median_strictly_prior_volatility":
                issues.append(f"{path}:{line_number}: unexpected volatility threshold method")

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
        if price is not None and price <= 0 and (quantity is None or quantity > 0):
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
