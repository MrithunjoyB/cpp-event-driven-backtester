from pathlib import Path
import os

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / ".matplotlib-cache"))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


RESULTS = ROOT / "results"
PLOTS = RESULTS / "plots"
PORTFOLIO = RESULTS / "portfolio"
PORTFOLIO_FIGURES = PORTFOLIO / "figures"


def plot_latest_equity() -> None:
    equity_path = RESULTS / "equity_curve.csv"
    if not equity_path.exists():
        print("No equity_curve.csv found. Run the C++ backtester first.")
        return

    equity = pd.read_csv(equity_path, parse_dates=["date"])
    fig, axes = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
    axes[0].plot(equity["date"], equity["portfolio_value"], label="Portfolio Value", color="#1f77b4")
    axes[0].set_title("Equity Curve")
    axes[0].set_ylabel("Portfolio Value")
    axes[0].grid(True, alpha=0.3)
    axes[0].legend()

    axes[1].fill_between(equity["date"], equity["drawdown"], 0, color="#d62728", alpha=0.35)
    axes[1].set_title("Drawdown")
    axes[1].set_ylabel("Drawdown")
    axes[1].grid(True, alpha=0.3)

    PLOTS.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(PLOTS / "equity_drawdown.png", dpi=150)
    plt.close(fig)


def plot_strategy_comparison() -> None:
    comparison_path = RESULTS / "cross_asset_comparison.csv"
    if not comparison_path.exists():
        comparison_path = RESULTS / "strategy_comparison.csv"
    if not comparison_path.exists():
        print("No comparison CSV found. Run the C++ backtester first.")
        return

    summary = pd.read_csv(comparison_path)
    summary["label"] = summary["ticker"] + " | " + summary["strategy"]

    fig, axes = plt.subplots(3, 1, figsize=(12, 10))
    axes[0].bar(summary["label"], summary["total_return"], color="#2ca02c")
    axes[0].set_title("Total Return by Strategy")
    axes[0].tick_params(axis="x", rotation=45)
    axes[0].grid(True, axis="y", alpha=0.3)

    axes[1].bar(summary["label"], summary["sharpe"], color="#9467bd")
    axes[1].set_title("Sharpe Ratio by Strategy")
    axes[1].tick_params(axis="x", rotation=45)
    axes[1].grid(True, axis="y", alpha=0.3)

    axes[2].bar(summary["label"], summary["max_drawdown"], color="#d62728")
    axes[2].set_title("Maximum Drawdown by Strategy")
    axes[2].tick_params(axis="x", rotation=45)
    axes[2].grid(True, axis="y", alpha=0.3)

    PLOTS.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(PLOTS / "strategy_comparison.png", dpi=150)
    plt.close(fig)


def plot_benchmark_timings() -> None:
    timings_path = RESULTS / "benchmark_timings.csv"
    if not timings_path.exists():
        return
    timings = pd.read_csv(timings_path)
    fig, ax = plt.subplots(figsize=(9, 5))
    ax.bar(timings["benchmark"], timings["milliseconds"], color="#17becf")
    ax.set_title("C++ Runtime Benchmarks")
    ax.set_ylabel("Milliseconds")
    ax.tick_params(axis="x", rotation=25)
    ax.grid(True, axis="y", alpha=0.3)
    PLOTS.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(PLOTS / "benchmark_timings.png", dpi=150)
    plt.close(fig)


def portfolio_dirs() -> list[Path]:
    if not PORTFOLIO.exists():
        return []
    dirs = [p for p in PORTFOLIO.iterdir() if p.is_dir() and (p / "portfolio_equity_curve.csv").exists()]
    if (PORTFOLIO / "portfolio_equity_curve.csv").exists():
        dirs.append(PORTFOLIO)
    return sorted(dirs)


def plot_portfolio_results() -> None:
    dirs = portfolio_dirs()
    if not dirs:
        return
    PORTFOLIO_FIGURES.mkdir(parents=True, exist_ok=True)
    summaries = []
    for directory in dirs:
        label = directory.name
        equity = pd.read_csv(directory / "portfolio_equity_curve.csv", parse_dates=["date"])
        summary_path = directory / "portfolio_performance_summary.csv"
        if summary_path.exists():
            summary = pd.read_csv(summary_path)
            summary["directory"] = label
            summaries.append(summary)

        fig, axes = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
        axes[0].plot(equity["date"], equity["portfolio_value"], label=label)
        axes[0].set_title(f"Portfolio Equity: {label}")
        axes[0].set_ylabel("Portfolio Value")
        axes[0].grid(True, alpha=0.3)
        axes[0].legend()
        axes[1].fill_between(equity["date"], equity["drawdown"], 0, alpha=0.35)
        axes[1].set_title("Portfolio Drawdown")
        axes[1].set_ylabel("Drawdown")
        axes[1].grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(PORTFOLIO_FIGURES / f"{label}_equity_drawdown.png", dpi=150)
        plt.close(fig)

        weights_path = directory / "portfolio_allocation_weights.csv"
        if weights_path.exists():
            weights = pd.read_csv(weights_path)
            pivot = weights.pivot_table(index="rebalance_id", columns="ticker", values="target_weight", aggfunc="last").fillna(0)
            fig, ax = plt.subplots(figsize=(11, 5))
            pivot.plot.area(ax=ax)
            ax.set_title(f"Allocation Weights: {label}")
            ax.set_ylabel("Target Weight")
            ax.grid(True, axis="y", alpha=0.3)
            fig.tight_layout()
            fig.savefig(PORTFOLIO_FIGURES / f"{label}_allocation_weights.png", dpi=150)
            plt.close(fig)

        rebalances_path = directory / "portfolio_rebalances.csv"
        if rebalances_path.exists():
            rebalances = pd.read_csv(rebalances_path)
            fig, ax = plt.subplots(figsize=(10, 4))
            ax.bar(rebalances["rebalance_id"], rebalances["turnover"])
            ax.set_title(f"Turnover by Rebalance: {label}")
            ax.set_xlabel("Rebalance ID")
            ax.set_ylabel("Turnover")
            ax.grid(True, axis="y", alpha=0.3)
            fig.tight_layout()
            fig.savefig(PORTFOLIO_FIGURES / f"{label}_turnover.png", dpi=150)
            plt.close(fig)

        costs_path = directory / "portfolio_costs.csv"
        if costs_path.exists():
            costs = pd.read_csv(costs_path)
            by_rebalance = costs.groupby("rebalance_id")[["transaction_cost", "slippage_cost"]].sum()
            fig, ax = plt.subplots(figsize=(10, 4))
            by_rebalance.plot(kind="bar", stacked=True, ax=ax)
            ax.set_title(f"Transaction Cost Contribution: {label}")
            ax.set_xlabel("Rebalance ID")
            ax.set_ylabel("Cost")
            ax.grid(True, axis="y", alpha=0.3)
            fig.tight_layout()
            fig.savefig(PORTFOLIO_FIGURES / f"{label}_costs.png", dpi=150)
            plt.close(fig)

    if summaries:
        combined = pd.concat(summaries, ignore_index=True)
        fig, ax = plt.subplots(figsize=(9, 5))
        ax.bar(combined["policy_name"], combined["total_return"])
        ax.set_title("Portfolio Policy Comparison")
        ax.set_ylabel("Total Return")
        ax.grid(True, axis="y", alpha=0.3)
        fig.tight_layout()
        fig.savefig(PORTFOLIO_FIGURES / "policy_comparison.png", dpi=150)
        plt.close(fig)

        fig, ax = plt.subplots(figsize=(12, max(2.5, 0.45 * len(combined) + 1.5)))
        ax.axis("off")
        cols = ["policy_name", "total_return", "sharpe", "max_drawdown", "var_95", "expected_shortfall_95", "turnover"]
        table = ax.table(
            cellText=combined[cols].round(4).astype(str).values,
            colLabels=cols,
            loc="center",
        )
        table.auto_set_font_size(False)
        table.set_fontsize(8)
        table.scale(1, 1.25)
        ax.set_title("Portfolio Risk Summary")
        fig.tight_layout()
        fig.savefig(PORTFOLIO_FIGURES / "portfolio_risk_summary.png", dpi=150)
        plt.close(fig)


def main() -> None:
    plot_latest_equity()
    plot_strategy_comparison()
    plot_benchmark_timings()
    plot_portfolio_results()
    print(f"Saved plots to {PLOTS}")


if __name__ == "__main__":
    main()
