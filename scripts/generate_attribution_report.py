from __future__ import annotations

import argparse
import csv
from pathlib import Path


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", default="results/public_synthetic/portfolio_equal_weight/attribution")
    args = parser.parse_args()
    root = Path(args.directory)
    summary = {row["component"]: row for row in rows(root / "portfolio_attribution_summary.csv")}
    risk = sorted(rows(root / "risk_contribution.csv"), key=lambda row: float(row["percentage_contribution"]), reverse=True)
    costs = rows(root / "transaction_cost_attribution.csv")
    cash = rows(root / "cash_attribution.csv")
    drawdowns = rows(root / "drawdown_episode_attribution.csv")
    worst = min(drawdowns, key=lambda row: float(row["drawdown_depth"])) if drawdowns else None
    commission = sum(float(row["commission"]) for row in costs)
    slippage = sum(float(row["slippage_cost"]) for row in costs)
    cash_drag = sum(float(row["uninvested_cash_drag"]) for row in cash)
    assets = sorted((row for key, row in summary.items() if key not in {"CASH", "TRANSACTION_COSTS", "CORPORATE_ACTIONS", "BENCHMARK_RETURN", "ACTIVE_RETURN", "RESIDUAL", "TOTAL"}),
                    key=lambda row: abs(float(row.get("percentage_of_net_profit", 0))), reverse=True)
    leaders = assets[:2]
    text = ["# Public Synthetic Portfolio Attribution Report", "", "These synthetic-fixture results validate accounting and reproducibility; they are not empirical market evidence or profitability claims.", "",
            "Attribution is accounting description, not economic causality.", "",
            "## Return Attribution", "",
            *[f"- {row['component']} contribution: {float(row.get('contribution_return', 0)):.2%} of starting capital; {float(row.get('percentage_of_net_profit', 0)):.2%} of net profit." for row in leaders],
            f"- Active return versus configured benchmark: {float(summary.get('ACTIVE_RETURN', {}).get('contribution_return', 0)):.2%}.",
            f"- Commission: {commission:.2f}; slippage: {slippage:.2f}; modelled spread cost: 0.00.",
            f"- Descriptive uninvested-cash drag: {cash_drag:.2f}; modelled cash interest: 0.00.",
            f"- Total residual: {float(summary.get('RESIDUAL', {}).get('contribution', 0)):.8f}.", "",
            "## Risk and Drawdown", "",
            f"- Largest ex-post volatility contributor: {risk[0]['ticker']} ({float(risk[0]['percentage_contribution']):.2%})." if risk else "- Risk contribution unavailable.",
            f"- Worst drawdown: {float(worst['drawdown_depth']):.2%}, from {worst['peak_date']} to {worst['trough_date']}." if worst else "- No drawdown episode.", "",
            "Arithmetic currency contributions reconcile wealth changes period by period. They must not be added or stacked as geometrically compounded strategy returns."]
    output = root / "attribution_report.md"
    output.write_text("\n".join(text) + "\n")
    print(f"Wrote {output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
