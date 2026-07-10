from pathlib import Path
import os

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / ".matplotlib-cache"))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


RESULTS = ROOT / "results"
REPORT = RESULTS / "report"
FIGURES = REPORT / "figures"


def save_bar(df: pd.DataFrame, x: str, y: str, title: str, filename: str) -> None:
    if df.empty or x not in df or y not in df:
        return
    fig, ax = plt.subplots(figsize=(11, 5))
    ax.bar(df[x].astype(str), df[y])
    ax.set_title(title)
    ax.tick_params(axis="x", rotation=45)
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(FIGURES / filename, dpi=150)
    plt.close(fig)


def main() -> None:
    FIGURES.mkdir(parents=True, exist_ok=True)
    cross = pd.read_csv(RESULTS / "cross_asset_comparison.csv") if (RESULTS / "cross_asset_comparison.csv").exists() else pd.DataFrame()
    cost = pd.read_csv(RESULTS / "transaction_cost_sensitivity.csv") if (RESULTS / "transaction_cost_sensitivity.csv").exists() else pd.DataFrame()
    timings = pd.read_csv(RESULTS / "benchmark_timings.csv") if (RESULTS / "benchmark_timings.csv").exists() else pd.DataFrame()
    regime = pd.read_csv(RESULTS / "regime_evaluation.csv") if (RESULTS / "regime_evaluation.csv").exists() else pd.DataFrame()

    if not cross.empty:
        cross["label"] = cross["ticker"] + " " + cross["strategy"]
        save_bar(cross, "label", "excess_return", "Strategy Excess Return vs Net Benchmark", "strategy_excess_return.png")
    if not timings.empty:
        save_bar(timings, "benchmark", "milliseconds", "C++ Benchmark Timings", "benchmark_timings.png")
    if not regime.empty:
        save_bar(regime.head(30), "regime", "return", "Regime Returns Sample", "regime_returns.png")
    if not cost.empty:
        pivot = cost.pivot_table(index="commission_bps", columns="slippage_bps", values="net_return", aggfunc="mean")
        fig, ax = plt.subplots(figsize=(7, 5))
        image = ax.imshow(pivot.values, aspect="auto")
        ax.set_xticks(range(len(pivot.columns)), pivot.columns)
        ax.set_yticks(range(len(pivot.index)), pivot.index)
        ax.set_xlabel("Slippage bps")
        ax.set_ylabel("Commission bps")
        ax.set_title("Transaction Cost Surface: Mean Net Return")
        fig.colorbar(image, ax=ax)
        fig.tight_layout()
        fig.savefig(FIGURES / "cost_surface_heatmap.png", dpi=150)
        plt.close(fig)

    report = REPORT / "research_report.md"
    with report.open("w") as f:
        f.write("# Research Report\n\n")
        f.write("## Research Question\nEvaluate whether systematic strategies remain robust out of sample, across assets, regimes, costs, and simple portfolio allocations.\n\n")
        f.write("## Methodology\nSignals use audited next-bar execution. Costs and slippage are included in fills. Results are generated from project CSV outputs only.\n\n")
        f.write("## Key Generated Figures\n")
        for path in sorted(FIGURES.glob("*.png")):
            f.write(f"- `{path.relative_to(REPORT)}`\n")
        f.write("\n## Findings\n")
        if not cross.empty:
            best = cross.sort_values("sharpe", ascending=False).iloc[0]
            f.write(f"- Highest Sharpe in cross-asset output: {best['ticker']} {best['strategy']} ({best['sharpe']:.3f}).\n")
            under = (cross["excess_return"] < 0).sum()
            f.write(f"- {under} of {len(cross)} strategy/asset rows underperformed the net buy-and-hold benchmark.\n")
        f.write("\n## Limitations\nDaily bars, long-only strategy tests, simplified portfolio allocation, no order book, and historical simulation uncertainty.\n")
    print(f"Wrote {report}")


if __name__ == "__main__":
    main()
