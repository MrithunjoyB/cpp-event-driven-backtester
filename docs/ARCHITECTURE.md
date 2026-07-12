# Architecture

## Dependency Direction

`quant_cli` parses commands and delegates to `quant::app::Application`. The application validates typed configuration, invokes experiment or portfolio services from `quant_core`, then calls exporters. Simulation code has no CLI dependency. `Backtester` and `PortfolioBacktester` return in-memory results and do not write files.

```text
quant_cli -> application -> experiments/portfolio -> strategy + execution + market data
                              |                       |
                              +-> analytics <--------+
                              +-> in-memory results -> io exporters
```

`quant_core` is the single reusable static library. Production sources are compiled once and linked into the CLI and every test target.

## Modules

- `domain`: validated calendar dates and typed error categories.
- `market_data`: OHLCV CSV parsing, validation, ordering, and ticker access.
- `strategies`: strategy interface, indicators, MA, RSI, MACD, and breakout signals.
- `execution`: next-bar fills, commission, slippage, and order validation.
- `portfolio`: long-only single-asset accounting and true shared-cash multi-asset accounting.
- `allocation`: equal-weight, inverse-volatility, and momentum top-N policies.
- `analytics`: returns, drawdown, risk, and trade metrics.
- `analytics/PortfolioAttribution`: trade-aware period accounting, cash/cost/corporate-action contribution, drawdown episodes, and ex-post covariance risk contribution.
- `methodology`: causal regimes and calendar-duration windows.
- `experiments`: backtests, parameter search, walk-forward, regime/cost analysis, and deterministic bootstrap analysis.
- `config`: strict typed JSON loading, validation, and resolved configuration.
- `io`: schema-v2 CSV and JSON manifest output with checked writes.
- `performance`: bounded deterministic indexed execution for independent candidate simulations.

Selection-risk experiments load each required dataset once into immutable shared ownership. Training and test benchmark paths are constructed once per ticker/window and reused only within the exact matching capital, date, cost, and liquidation context. No global mutable cache is used.

Candidate training and counterfactual OOS simulations within one fixed window are independent and may use the bounded executor. Task indices define result slots, so completion order cannot affect ranking or export order. Event chronology, linked selected-strategy OOS capital, shared-cash portfolio updates, bootstrap reductions, output assembly, and file writing remain serial.

The shared-cash portfolio owns one cash balance and synchronized positions. It is intentionally separate from cross-asset evaluation, where each ticker is an independent backtest. The removed legacy composite path averaged independent account curves and was not a portfolio accounting model.

## Research Timing

Signals use bar `t` close information and execute at bar `t+1` open. Walk-forward training ends before testing begins; selected parameters are frozen through each test interval. Continuous OOS mode links each test window's ending capital to the next window's starting capital. Regime labels used at an execution open and return-interval start are based on information available through the previous close.
