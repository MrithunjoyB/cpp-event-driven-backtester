from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


def save(fig: plt.Figure, path: Path) -> None:
    fig.tight_layout()
    fig.savefig(path, dpi=160, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", default="results/public_synthetic/portfolio_equal_weight/attribution")
    args = parser.parse_args()
    root = Path(args.directory)
    figures = root / "figures"
    figures.mkdir(parents=True, exist_ok=True)
    daily = pd.read_csv(root / "daily_asset_attribution.csv")
    summary = pd.read_csv(root / "portfolio_attribution_summary.csv")
    costs = pd.read_csv(root / "transaction_cost_attribution.csv")
    risk = pd.read_csv(root / "risk_contribution.csv")
    drawdowns = pd.read_csv(root / "drawdown_episode_attribution.csv")
    regimes = pd.read_csv(root / "regime_attribution.csv")
    years = pd.read_csv(root / "calendar_year_attribution.csv")
    rebalances = pd.read_csv(root / "rebalance_attribution.csv")

    assets = sorted(daily["ticker"].dropna().unique())
    asset_daily = daily[daily["ticker"].isin(assets)].copy()
    asset_daily["end_date"] = pd.to_datetime(asset_daily["end_date"])
    cumulative = asset_daily.pivot_table(index="end_date", columns="ticker", values="net_contribution", aggfunc="sum").fillna(0).cumsum()
    fig, ax = plt.subplots(figsize=(11, 5.5))
    cumulative.plot(ax=ax, linewidth=1.7)
    ax.set(title="Cumulative Accounting Contribution by Asset", ylabel="Contribution (currency)", xlabel="")
    ax.axhline(0, color="black", linewidth=0.7)
    save(fig, figures / "cumulative_asset_contribution.png")

    totals = summary[summary["component"].isin(assets)].set_index("component")["contribution"].sort_values()
    fig, ax = plt.subplots(figsize=(8, 4.8))
    totals.plot.barh(ax=ax, color=["#b7423a" if value < 0 else "#287271" for value in totals])
    ax.set(title="Return Contribution Waterfall", xlabel="Contribution (currency)", ylabel="")
    save(fig, figures / "return_contribution_waterfall.png")

    fig, ax = plt.subplots(figsize=(8, 4.8))
    pd.Series({"Commission": costs["commission"].sum(), "Spread": costs["spread_cost"].sum(),
               "Slippage": costs["slippage_cost"].sum()}).plot.bar(ax=ax, color=["#7f3c8d", "#11a579", "#3969ac"])
    ax.set(title="Execution Cost Decomposition", ylabel="Cost (currency)", xlabel="")
    save(fig, figures / "transaction_cost_waterfall.png")

    fig, ax = plt.subplots(figsize=(8, 4.8))
    risk.set_index("ticker")["percentage_contribution"].sort_values().plot.barh(ax=ax, color="#3969ac")
    ax.set(title="Ex-post Volatility Contribution", xlabel="Share of portfolio volatility", ylabel="")
    save(fig, figures / "risk_contribution.png")

    if not drawdowns.empty:
        worst_id = drawdowns.groupby("episode_id")["drawdown_depth"].first().idxmin()
        episode = drawdowns[drawdowns["episode_id"] == worst_id].set_index("ticker")["contribution"].sort_values()
        fig, ax = plt.subplots(figsize=(8, 4.8))
        episode.plot.barh(ax=ax, color="#b7423a")
        ax.set(title="Worst Drawdown Accounting Contribution", xlabel="Peak-to-trough contribution", ylabel="")
        save(fig, figures / "worst_drawdown_contribution.png")

    regime_table = regimes[regimes["ticker"].isin(assets)].pivot_table(index="regime", columns="ticker", values="contribution", aggfunc="sum").fillna(0)
    if not regime_table.empty:
        fig, ax = plt.subplots(figsize=(10, 5))
        image = ax.imshow(regime_table.values, aspect="auto", cmap="RdYlGn")
        ax.set_xticks(range(len(regime_table.columns)), regime_table.columns, rotation=30, ha="right")
        ax.set_yticks(range(len(regime_table.index)), regime_table.index)
        ax.set_title("Contribution by Existing Causal Regime")
        fig.colorbar(image, ax=ax, label="Contribution")
        save(fig, figures / "regime_contribution_heatmap.png")

    year_table = years[years["ticker"].isin(assets)].pivot_table(index="calendar_year", columns="ticker", values="contribution", aggfunc="sum").fillna(0)
    fig, ax = plt.subplots(figsize=(10, 5))
    year_table.plot.bar(ax=ax)
    ax.set(title="Calendar-year Arithmetic Contribution", ylabel="Contribution", xlabel="Year")
    save(fig, figures / "calendar_year_contribution.png")

    fig, ax = plt.subplots(figsize=(10, 5))
    rebalances.set_index("execution_date")["holding_period_contribution"].plot(ax=ax, color="#287271")
    ax.set(title="Contribution by Rebalance Holding Period", ylabel="Contribution", xlabel="Execution date")
    ax.tick_params(axis="x", rotation=45)
    save(fig, figures / "rebalance_holding_period_contribution.png")

    asset_concentration = summary[summary["component"].isin(assets)].copy()
    asset_concentration["absolute_share"] = asset_concentration["percentage_of_net_profit"].abs()
    concentration = asset_concentration.nlargest(2, "absolute_share").set_index("component")["percentage_of_net_profit"]
    fig, ax = plt.subplots(figsize=(6.5, 4.5))
    concentration.plot.bar(ax=ax, color=["#e68310", "#008695"])
    ax.set(title="Largest Synthetic Asset Contributions", ylabel="Share of net profit", xlabel="")
    save(fig, figures / "largest_asset_concentration.png")
    print(f"Saved attribution figures to {figures.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
