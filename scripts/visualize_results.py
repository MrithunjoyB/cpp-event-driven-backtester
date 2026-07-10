from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"


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

    fig.tight_layout()
    plt.show()


def plot_strategy_comparison() -> None:
    comparison_path = RESULTS / "strategy_comparison.csv"
    if not comparison_path.exists():
        print("No strategy_comparison.csv found. Run the default C++ comparison first.")
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

    fig.tight_layout()
    plt.show()


def main() -> None:
    plot_latest_equity()
    plot_strategy_comparison()


if __name__ == "__main__":
    main()

