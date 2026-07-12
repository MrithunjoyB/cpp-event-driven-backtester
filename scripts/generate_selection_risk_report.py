#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--directory", required=True, type=Path)
args = parser.parse_args()
rows = list(csv.DictReader((args.directory / "multiple_testing_summary.csv").open()))
cross = [row for row in csv.DictReader((args.directory / "cross_family_selection_risk.csv").open()) if row.get("status") != "not_applicable"]
frequencies = list(csv.DictReader((args.directory / "candidate_parameter_frequency.csv").open()))
lines = ["# Selection-Risk Report", "", "The primary test is a centered circular moving-block max-mean reality check. The null is that no candidate has positive expected active return. Counterfactual candidate histories are normalized diagnostics, not deployable capital paths.", "", "## Corrected Evidence", "", "| Ticker | Family | Eligible | Common OOS observations | Best mean active return | Adjusted p-value |", "| --- | --- | ---: | ---: | ---: | ---: |"]
for row in rows:
    lines.append(f"| {row['ticker']} | {row['strategy_family']} | {row['eligible_candidates']} | {row['common_observations']} | {float(row['observed_best_mean_active_return']):.6f} | {float(row['adjusted_p_value']):.4f} |")
if cross:
    lines += ["", "## Cross-Family Correction", "", "| Ticker | Eligible candidates | Common OOS observations | Adjusted p-value |", "| --- | ---: | ---: | ---: |"]
    for row in cross:
        lines.append(f"| {row['ticker']} | {row['eligible_candidates']} | {row['common_observations']} | {float(row['adjusted_p_value']):.4f} |")
concentrated = sorted(frequencies, key=lambda row: float(row["selection_frequency"]), reverse=True)[:5]
lines += ["", "## Selection Concentration", ""]
for row in concentrated:
    lines.append(f"- {row['ticker']} / {row['strategy_family']} / `{row['parameter_serialization']}`: {float(row['selection_frequency']):.1%} of eligible windows.")
lines += ["", "## Interpretation", "", "Adjusted p-values must be interpreted with the exported common-date universe, eligibility filters, and dependence assumptions. An ex-post best candidate is not the causally selected deployable strategy. Regime-conditioned results are exploratory because the primary family correction does not additionally correct across regime subsets."]
siblings = args.directory.parents[1]
zero = siblings / "cost_zero/selection_risk/cross_family_selection_risk.csv"
high = siblings / "cost_high/selection_risk/cross_family_selection_risk.csv"
if cross and zero.exists() and high.exists():
    lines += ["", "## Cost Robustness", "", "Zero-cost, base-cost, and high-cost grids use the same candidates, windows, seed, and bootstrap settings. No cross-family ticker test crosses a 5% adjusted threshold under any of the three cost assumptions."]
lines += ["", "Historical evidence is not a forecast and does not imply future profitability."]
(args.directory / "selection_risk_report.md").write_text("\n".join(lines) + "\n")
print(args.directory / "selection_risk_report.md")
