from __future__ import annotations
import argparse,csv
from pathlib import Path
def main():
 p=argparse.ArgumentParser();p.add_argument("--directory",default="results/research_v3/portfolio_equal_weight/statistics");a=p.parse_args();root=Path(a.directory)
 s={r["metric"]:r for r in csv.DictReader((root/"bootstrap_summary.csv").open())};sh=list(csv.DictReader((root/"sharpe_inference.csv").open()))[0];mt=list(csv.DictReader((root/"multiple_testing_summary.csv").open()))[0]
 text=f"""# Statistical Robustness Report

Statistical evidence is conditional on historical data and assumptions; it does not guarantee future profitability.

- Default method: {sh['method']} with block length {sh['block_length']}, seed {sh['seed']}, and {sh['simulation_count']} simulations.
- Cumulative-return {float(sh['confidence_level']):.0%} interval: [{float(s['cumulative_return']['lower_bound']):.4f}, {float(s['cumulative_return']['upper_bound']):.4f}].
- Sharpe interval: [{float(sh['sharpe_lower']):.4f}, {float(sh['sharpe_upper']):.4f}].
- Probability Sharpe above zero: {float(sh['probability_sharpe_positive']):.2%}.
- Probability Sharpe exceeds benchmark: {float(sh['probability_sharpe_exceeds_benchmark']):.2%}.
- Centered moving-block reality-check p-value: {float(mt['p_value']):.4f}. With one portfolio candidate this is diagnostic, not a grid-wide selection correction.

The block-length cube-root heuristic is transparent but not claimed optimal. Portfolio-policy output does not substitute for continuous-OOS strategy-grid inference.
"""
 (root/"statistical_report.md").write_text(text);print(f"Wrote {(root/'statistical_report.md').resolve()}");return 0
if __name__=="__main__":raise SystemExit(main())
