# C++ Event-Driven Quantitative Backtesting and Risk Analytics Engine

A reproducible quantitative research platform for evaluating systematic strategies under out-of-sample validation, realistic execution costs, regime shifts, and portfolio constraints.

## Why This Project Exists

This project was built as an honest, interview-ready quantitative development project. It avoids fake complexity while still implementing the core pieces expected in a real backtesting workflow: data ingestion, strategy signals, order generation, fills, portfolio updates, transaction costs, equity curves, trade logs, and performance analytics.

## Features

- Loads historical OHLCV CSV data.
- Uses an event-driven flow: market data -> signal -> order -> fill -> portfolio update -> metrics.
- Implements four C++ trading strategies:
  - Moving Average Crossover
  - RSI Mean Reversion
  - MACD Momentum
  - Volatility Breakout
- Implements technical indicators in C++:
  - Simple moving average
  - RSI
  - MACD through exponential moving averages
  - Daily returns
  - Rolling volatility helper
- Tracks starting capital, cash, position, holdings, trade history, realized P&L, unrealized holdings, and portfolio value.
- Applies transaction costs and slippage on every fill.
- Uses next-bar open execution: signals are generated from the current bar and filled on the next bar open.
- Compares strategy returns against a configured same-asset or external buy-and-hold benchmark using comparable execution assumptions.
- Supports JSON experiment configs, parameter grid search, walk-forward validation, cross-asset evaluation, transaction-cost sensitivity, regime evaluation, bootstrap uncertainty, true shared-cash multi-asset portfolio backtesting, and runtime benchmarking.
- Supports schema-v3 union-calendar valuation for mixed equity/crypto portfolios, stale closed-market marks, civil-calendar rebalancing, and explicit split/dividend accounting.
- Produces trade-aware portfolio return, cash, execution-cost, corporate-action, rebalance, benchmark-relative, drawdown, risk, regime, and calendar-year attribution with checked residuals.
- Provides deterministic IID comparison and dependence-preserving moving-block bootstrap, bootstrap Sharpe inference, and a centered moving-block selection-risk diagnostic.
- Prevents buying beyond available cash and prevents long-only portfolios from selling more shares than held.
- Exports trades, equity curves, performance summaries, and strategy comparison files.
- Includes Python scripts for yFinance data download and Matplotlib visualization.
- Generates a Markdown research report under `results/report/research_report.md`.

## Tech Stack

- C++17
- CMake 3.16+
- Python 3
- yFinance
- Pandas
- Matplotlib

## Folder Structure

```text
cpp-event-driven-backtester/
├── CMakeLists.txt
├── README.md
├── configs/
├── data/
├── apps/
│   └── quant_cli.cpp
├── include/
│   ├── quant/{app,config,domain,experiments,io}/
│   └── compatibility public headers
├── src/
│   ├── allocation/ analytics/ execution/ market_data/
│   ├── methodology/ portfolio/ strategies/
│   └── app/ config/ domain/ experiments/ io/
├── tests/
│   ├── fixtures/
│   ├── refactor/
│   └── test_engine.cpp
├── docs/
│   ├── ARCHITECTURE.md
│   ├── CONFIGURATION.md
│   ├── RESULT_SCHEMA.md
│   └── TESTING.md
├── scripts/
│   ├── download_data.py
│   ├── validate_results.py
│   ├── visualize_results.py
│   └── generate_research_report.py
└── results/
```

## Download Market Data

Install Python dependencies:

```bash
pip install yfinance pandas matplotlib
```

Download data for AAPL, MSFT, SPY, TSLA, and BTC-USD:

```bash
cd cpp-event-driven-backtester
python3 scripts/download_data.py
```

The downloader saves CSV files to `data/` with this format:

```csv
Date,Open,High,Low,Close,Volume
2024-01-01,100,105,99,103,1000000
```

## Build

```bash
cd cpp-event-driven-backtester
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

## Run

From the project root after building:

```bash
./build/quant_cli --mode compare
```

Run a single strategy:

```bash
./build/quant_cli --mode single --ticker AAPL --strategy ma_cross --capital 100000
./build/quant_cli --mode single --ticker MSFT --strategy rsi --capital 100000
./build/quant_cli --mode single --ticker SPY --strategy macd --capital 100000
```

Optional cost controls:

```bash
./build/quant_cli --mode single --ticker AAPL --strategy ma_cross --transaction-cost 0.001 --slippage 0.0005
```

Reproducible date windows:

```bash
./build/quant_cli --mode grid --ticker AAPL --start 2020-01-02 --end 2025-12-30
```

Analysis modes:

```bash
./build/quant_cli --mode cross-asset
./build/quant_cli --mode grid
./build/quant_cli --mode walk-forward
./build/quant_cli --mode cost
./build/quant_cli --mode regime
./build/quant_cli --mode benchmark
./build/quant_cli --mode all
```

Run a reproducible configured research experiment:

```bash
./build/quant_cli run --config configs/ma_walk_forward.json
./build/quant_cli run --config configs/rsi_walk_forward.json
./build/quant_cli run --config configs/macd_walk_forward.json
./build/quant_cli run --config configs/portfolio_equal_weight.json
./build/quant_cli run --config configs/portfolio_inverse_volatility.json
./build/quant_cli run --config configs/portfolio_momentum_top_n.json
```

Each config records the experiment name, ticker universe, strategy or allocation policy, starting capital, commission/slippage basis points, walk-forward or rebalance schedule, benchmark, objective, minimum trade requirement, regime method, random seed, and output directory. Portfolio configs write shared-cash portfolio outputs under `results/portfolio/`.

## Output Files

The engine writes:

- `results/trades.csv`
- `results/equity_curve.csv`
- `results/performance_summary.csv`
- `results/strategy_comparison.csv`
- `results/cross_asset_comparison.csv`
- `results/parameter_grid_results.csv`
- `results/walk_forward_windows.csv`
- `results/walk_forward_equity_curve.csv`
- `results/parameter_selection_history.csv`
- `results/transaction_cost_sensitivity.csv`
- `results/transaction_cost_surface.csv`
- `results/break_even_costs.csv`
- `results/regime_evaluation.csv`
- `results/strategy_regime_performance.csv`
- `results/regime_assignments.csv`
- `results/benchmark_timings.csv`
- `results/research_v2/<experiment>/walk_forward/windows.csv`
- `results/research_v2/<experiment>/walk_forward/in_sample_results.csv`
- `results/research_v2/<experiment>/walk_forward/out_of_sample_results.csv`
- `results/research_v2/<experiment>/walk_forward/oos_equity_curve.csv`
- `results/research_v2/<experiment>/bootstrap/bootstrap_summary.csv`
- `results/portfolio/portfolio_equity_curve.csv`
- `results/portfolio/portfolio_positions.csv`
- `results/portfolio/portfolio_orders.csv`
- `results/portfolio/portfolio_fills.csv`
- `results/portfolio/portfolio_rebalances.csv`
- `results/portfolio/portfolio_performance_summary.csv`
- `results/portfolio/portfolio_allocation_weights.csv`
- `results/portfolio/portfolio_costs.csv`
- `results/research_v3/<portfolio>/portfolio_valuations.csv`
- `results/research_v3/<portfolio>/portfolio_corporate_actions.csv`
- `results/research_v3/<portfolio>/portfolio_rebalances.csv`
- `results/research_v3/<portfolio>/attribution/*.csv`
- `results/research_v3/<portfolio>/attribution/figures/*.png`
- `results/research_v3/<portfolio>/attribution/attribution_report.md`
- `results/research_v3/<portfolio>/statistics/*.{csv,json}`
- `results/research_v3/<portfolio>/statistics/figures/*.png`
- `results/portfolio/inverse_volatility/*.csv`
- `results/portfolio/momentum_top_n/*.csv`
- `results/report/research_report.md`
- Per-run files such as `results/AAPL_MA_Cross_trades.csv`

Trade log schema:

```csv
date,ticker,strategy,action,price,quantity,cost,slippage,portfolio_value,realized_pnl,trade_return
```

Equity curve schema:

```csv
date,portfolio_value,cash,holdings,total_return,drawdown
```

Performance summary schema:

```csv
schema_version,ticker,strategy,parameter_set,benchmark_ticker,benchmark_execution_policy,benchmark_cost_policy,excess_return_basis,total_return,benchmark_gross_return,benchmark_net_return,excess_return,annualized_return,volatility,sharpe,max_drawdown,win_rate,profit_factor,num_trades,turnover,total_transaction_costs,cost_drag,average_trade_return
```

Portfolio equity schema:

```csv
date,portfolio_value,cash,total_holdings_value,total_return,drawdown,gross_exposure
```

Portfolio summary schema:

```csv
policy_name,total_return,equal_weight_benchmark_return,spy_benchmark_return,excess_return,annualized_return,volatility,sharpe,sortino,max_drawdown,calmar,var_95,expected_shortfall_95,beta,alpha,information_ratio,turnover,total_transaction_costs,number_of_rebalances,number_of_fills,average_cash_allocation,average_gross_exposure
```

## Visualize Results

```bash
python3 scripts/visualize_results.py
```

The script saves PNG plots under `results/plots/`:

- Equity curve
- Drawdown curve
- Strategy comparison bars for return, Sharpe ratio, and max drawdown
- Runtime benchmark timings

For portfolio runs, the script also saves figures under `results/portfolio/figures/`:

- Portfolio equity curve versus policy folder
- Portfolio drawdown
- Allocation weights over time
- Turnover by rebalance
- Transaction-cost contribution
- Policy comparison chart
- Portfolio risk summary table

Generate the research report:

```bash
python3 scripts/generate_research_report.py
```

## Metrics Explained

- All returns are decimal returns: `0.10` means `10%`.
- Total return: final net portfolio value divided by starting capital minus one. Trading costs are already reflected in the portfolio value.
- Benchmark gross return: the comparable buy-and-hold policy using the same decision timing, next-open execution, integer sizing, and 5% cash reserve, with zero costs.
- Benchmark net return: the same comparable policy after applying the strategy's commission and slippage assumptions.
- Excess return: strategy total return minus benchmark net return.
- Annualized return: total return converted to a 252-trading-day annual rate.
- Volatility: sample standard deviation of daily portfolio returns annualized by `sqrt(252)`.
- Sharpe ratio: annualized average daily return divided by annualized volatility, with no risk-free-rate adjustment.
- Maximum drawdown: largest peak-to-trough portfolio decline. Values are zero or negative.
- Win rate: winning completed exits divided by completed exits. In this long-only engine, a completed exit is a sell fill.
- Profit factor: gross winning realized P&L divided by absolute gross losing realized P&L. If there are wins and no losses, the export uses a large finite sentinel instead of infinity.
- Number of trades: number of fills, not number of round trips.
- Turnover: gross traded notional divided by starting capital.
- Total transaction costs: explicit commission plus measured slippage cost. Slippage is applied through the fill price and reported as cost attribution.
- Cost drag: total transaction costs divided by starting capital.
- Average trade return: average realized return on completed exits.
- Sortino ratio in research outputs: annualized average daily return divided by annualized downside deviation.
- Calmar ratio in research outputs: annualized return divided by absolute maximum drawdown.
- Bootstrap probability of loss: fraction of resampled paths ending below starting capital. It is descriptive and does not predict extreme losses.

## Correctness Guarantees and Assumptions

The engine is long-only and uses daily OHLCV bars. Strategy indicators are computed using data available through the current bar close. If a signal is generated, the order is queued and executed at the next bar open with configured slippage and transaction costs. This avoids filling on the same close used to generate the signal. If the next open gaps enough that a buy is no longer affordable, the portfolio rejects the fill rather than allowing negative cash.

The default transaction cost is 10 basis points and default slippage is 5 basis points. Order sizing deploys 95% of available cash on new long entries.

Trade accounting uses slippage-adjusted fill prices. Buy cash outflow is fill notional plus commission. Sell cash inflow is fill notional minus commission. Slippage is not double-counted in cash flows; it is reported separately as measured cost attribution. Realized P&L is recorded only on sells and uses commission-inclusive average entry cost basis.

Open positions at the final bar remain open and are marked to market in the final equity value. They are not force-liquidated unless a strategy generates a sell signal before the dataset ends.

Continuous walk-forward tests are the exception: each test window uses a scheduled liquidation at its final close. Commission and slippage are charged, the resulting cash becomes the next test window's starting capital, and newly selected parameters begin without inheriting an incompatible position.

Single-asset benchmarks use the same first-close decision, next-open execution, integer-share sizing, cash reserve, valuation interval, and cost model as an equivalent buy-and-hold strategy. `benchmark="same_asset"` resolves to the traded ticker. A configured external ticker such as `SPY` is loaded independently and fails clearly when its data is unavailable.

The project does not claim production-grade execution realism, high-frequency behavior, complete bias elimination, or order-book simulation.

## Shared-Cash Portfolio Backtesting

The portfolio engine in `PortfolioBacktester` is separate from the audited single-asset `Backtester`. It uses one starting capital value and one shared cash balance across the full ticker universe. It tracks simultaneous per-ticker positions, last marked prices, total holdings value, cash, portfolio value, transaction costs, slippage costs, allocation weights, fills, and rebalances.

Portfolio rebalancing uses daily bars. Target weights are decided from information available before the rebalance execution date. Rebalance orders execute at that date's open with the same transaction-cost and slippage model used by the single-asset engine. The rebalance flow is:

1. Mark the portfolio using available open prices.
2. Compute target weights from the selected allocation policy.
3. Generate sell orders first.
4. Apply slippage-adjusted fill prices and commission.
5. Update shared cash.
6. Generate buy orders.
7. Scale buys deterministically if available shared cash is insufficient.
8. Reject orders that would create negative cash or long-only negative holdings.
9. Mark end-of-day equity using closes.

Supported allocation policies:

- Equal weight: allocates equally across available assets.
- Inverse volatility: allocates more weight to lower-volatility assets using a 60-trading-day historical volatility lookback by default.
- Momentum top-N: ranks assets by trailing return using a 126-trading-day lookback by default and allocates equally to the top `N`.

Every policy enforces non-negative weights, max weight per asset, optional cash buffer, and a minimum trade threshold. Weekly and monthly rebalance frequencies are supported. The benchmark fields compare the portfolio against equal-weight buy-and-hold for the same universe and SPY where SPY is part of the data universe.

## Data Validation Rules

The CSV loader requires exactly:

```csv
Date,Open,High,Low,Close,Volume
```

It rejects malformed rows, missing values, duplicate dates, non-chronological dates, non-finite prices, zero or negative prices, negative volume, `High < Low`, and open/close prices outside the high-low range. It does not silently sort data.

## Indicator Warm-Up

SMA and rolling volatility return zero until enough observations are available. RSI returns a neutral value during warm-up and is bounded to finite values. MACD waits for enough slow EMA and signal observations before producing actionable crossover signals.

## Testing Methodology

The CTest target contains deterministic unit-style checks using small fixed inputs and CSV fixtures under `tests/fixtures/`. It covers indicators, costs, accounting, next-bar execution, benchmark parity and propagation, causal regime prefix invariance, execution and return timestamps, equity and BTC calendar windows, leap years, missing dates, OOS non-overlap and capital reconciliation, shared-cash constraints, portfolio risk metrics, and result-integrity fixtures.

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
python3 scripts/validate_results.py
```

The result validator fails on NaN, infinity, missing required columns, negative holdings/cash, positive drawdown, invalid win rates, negative trade counts, duplicate equity dates, basic equity reconciliation errors, invalid portfolio weights, no-leverage violations, negative transaction costs, missing fill prices, and inconsistent rebalance IDs.

## Reproducibility

Every run writes `run_metadata.json` with schema version 2, the mode, CLI ticker/strategy arguments, starting capital, commission/slippage in basis points, date arguments, execution convention, benchmark ticker and methodology, timestamp, and Git commit hash where available. Config-driven experiments also write their resolved calendar, continuity, boundary, benchmark, and schema settings.

## Experiment Configuration And Parameter Search

Grid search evaluates every candidate and exports every result to `results/parameter_grid_results.csv`; it does not only report the best row.

- Moving Average short windows: 5, 10, 20, 30.
- Moving Average long windows: 50, 100, 150, 200.
- RSI periods: 7, 14, 21.
- RSI threshold pairs: 20/80, 25/75, 30/70, 35/65.
- MACD combinations: 8/21/5, 12/26/9, 16/32/9, 20/40/10.
- Volatility breakout lookbacks: 10, 20, 40 with multipliers 1.0, 1.5, 2.0.

Invalid combinations, such as short moving averages greater than or equal to long moving averages, are rejected.

## Walk-Forward Validation

Walk-forward validation defaults to true civil-calendar durations:

- Training interval: `[anchor, anchor + 3 calendar years)`.
- Testing interval: `[anchor + 3 years, anchor + 3 years + 6 calendar months)`.
- Step: advance the anchor by 6 calendar months.

Equities and seven-day assets therefore share elapsed boundaries but can have different observation counts. Missing dates do not shift boundaries, leap days and month ends are clamped deterministically, training and testing never overlap, and test dates are counted once. Observation-count windows remain available only through the explicit legacy `window_mode="observation_count"` setting.

For each window, candidate parameters are evaluated only in-sample. The default config objective is:

```text
objective = training Sharpe, after rejecting candidates below the configured minimum trade count
```

Other implemented objectives include Calmar ratio, excess return, and a Sharpe objective with a maximum-drawdown guard.

The selected parameters are frozen and tested on the immediately following out-of-sample period. The default `continuous_capital` policy links ending cash into the next window after the cost-bearing boundary liquidation. `normalized_window` is a separate diagnostic mode and must not be presented as deployable stitched performance. Schema-v2 outputs record observation counts, starting and ending capital, liquidation costs, linked and cumulative returns, continuity policy, benchmark methodology, and test boundaries.

## Bootstrap Uncertainty

Config-driven experiments generate IID bootstrap outputs with a fixed seed:

- `bootstrap_summary.csv`
- `bootstrap_paths_sample.csv`
- `bootstrap_metric_distributions.csv`

The bootstrap estimates uncertainty in terminal wealth and loss probability using resampled historical strategy returns. It does not assume normally distributed returns.

## Regime Evaluation

Regimes are assigned mechanically, not manually:

- Bull: price above 200-day SMA and 60-day return positive.
- Bear: price below 200-day SMA and 60-day return negative.
- Sideways: all other classified observations.
- Volatility state: 20-day annualized rolling volatility above or below the expanding median of volatility observations strictly before the classification date, after a 60-observation threshold warm-up.

Each regime point represents information known at that date's close. An open fill on date `t` uses a regime cutoff no later than close `t-1`. A close-to-close return from `t-1` to `t` is attributed to the start-of-period regime known at close `t-1`. Warm-up dates are labelled `unavailable`, and exports include trend state, volatility state, volatility value, causal threshold, information cutoff, and threshold method.

Regime assignments are exported to `results/regime_assignments.csv`; strategy metrics are exported to `results/regime_evaluation.csv` and `results/strategy_regime_performance.csv`. Historical schema-v1 research outputs under `results/research/` are retained only as labelled regression artifacts; current config-driven outputs are written under `results/research_v2/`.

## C++ Performance Benchmarks

Runtime benchmarks are exported to `results/benchmark_timings.csv`. On the verified local run:

| Benchmark | Time |
| --- | ---: |
| Naive rolling SMA | 4.877557 ms |
| Optimized rolling SMA | 0.771395 ms |
| Single AAPL backtest | 38.884989 ms |
| Full AAPL parameter sweep | 220.501466 ms |

## Example Strategy Comparison

Generated from the default yFinance date range in `scripts/download_data.py`.

| Ticker | Strategy | Total Return | Sharpe | Max Drawdown | Win Rate | Trades |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| AAPL | MA Cross | 109.94% | 0.74 | -27.27% | 37.50% | 33 |
| AAPL | RSI | 23.06% | 0.26 | -30.29% | 64.71% | 35 |
| AAPL | MACD | 88.48% | 0.67 | -25.70% | 47.27% | 110 |
| MSFT | MA Cross | 42.94% | 0.42 | -38.85% | 41.18% | 34 |
| MSFT | RSI | 97.02% | 0.64 | -20.40% | 83.33% | 37 |
| MSFT | MACD | -14.75% | -0.06 | -41.39% | 36.67% | 121 |
| SPY | MA Cross | 50.12% | 0.64 | -29.50% | 50.00% | 25 |
| SPY | RSI | 23.43% | 0.30 | -27.79% | 73.33% | 30 |
| SPY | MACD | 22.91% | 0.36 | -16.04% | 43.75% | 129 |

## Design Notes

The implementation is intentionally long-only. Signals become orders only when they are feasible: buy orders require available cash, and sell orders require an existing position. The execution handler applies transaction costs and slippage before the portfolio is updated, so reported results are cost-aware.

## Future Improvements

- More realistic order book simulation.
- Walk-forward optimization for allocation-policy parameters.
- Options strategy backtesting.
- Machine learning signal generation.
- Live paper-trading integration.
- More robust corporate-action handling.

## Current Limitations

- Uses daily OHLCV bars only.
- Long-only by default.
- No order book, intraday queue model, or partial fills.
- No taxes, borrow costs, financing rates, or corporate-action adjustment beyond the downloaded price data.
- Schema-v3 shared portfolios use union-calendar valuation, stale closed-market marks with a configured expiry, civil weekly/monthly schedules, and aligned mixed-calendar annualization. Schema-v2 intersection results remain available explicitly for regression comparison.
- Exchange closures are inferred from bar absence; complete exchange-holiday accuracy is not claimed. Corporate actions support splits and ex-date cash dividends, but not tax, payable-date settlement, delistings, or symbol changes.
- Price series are not a documented dividend-adjusted total-return dataset.
- Bootstrap output remains IID, and no block bootstrap or multiple-testing correction is claimed.
- Statistical outputs are descriptive and are not claimed as research-grade inference.
- Not a live trading system.
- Intended for education, research, and interview discussion, not guaranteed profitability.
