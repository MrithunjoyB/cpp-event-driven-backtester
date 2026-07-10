# Research Report

## Research Question
Evaluate whether systematic strategies remain robust out of sample, across assets, regimes, costs, and simple portfolio allocations.

## Methodology
Signals use audited next-bar execution. Costs and slippage are included in fills. Results are generated from project CSV outputs only.

## Key Generated Figures
- `figures/benchmark_timings.png`
- `figures/cost_surface_heatmap.png`
- `figures/regime_returns.png`
- `figures/strategy_excess_return.png`

## Findings
- Highest Sharpe in cross-asset output: TSLA MACD_Momentum (1.085).
- 15 of 15 strategy/asset rows underperformed the net buy-and-hold benchmark.

## Limitations
Daily bars, long-only strategy tests, simplified portfolio allocation, no order book, and historical simulation uncertainty.
