# C++ Event-Driven Backtesting Engine for Algorithmic Trading Strategies

A modular C++17 backtesting engine for evaluating algorithmic trading strategies on historical OHLCV data. The project demonstrates event-driven trading logic, market data handling, technical indicators, portfolio accounting, execution costs, risk metrics, and Python-based data download/visualization.

## Why This Project Exists

This project was built as an honest, interview-ready quantitative development project. It avoids fake complexity while still implementing the core pieces expected in a real backtesting workflow: data ingestion, strategy signals, order generation, fills, portfolio updates, transaction costs, equity curves, trade logs, and performance analytics.

## Features

- Loads historical OHLCV CSV data.
- Uses an event-driven flow: market data -> signal -> order -> fill -> portfolio update -> metrics.
- Implements three C++ trading strategies:
  - Moving Average Crossover
  - RSI Mean Reversion
  - MACD Momentum
- Implements technical indicators in C++:
  - Simple moving average
  - RSI
  - MACD through exponential moving averages
  - Daily returns
  - Rolling volatility helper
- Tracks starting capital, cash, position, holdings, trade history, realized P&L, unrealized holdings, and portfolio value.
- Applies transaction costs and slippage on every fill.
- Uses next-bar open execution: signals are generated from the current bar and filled on the next bar open.
- Compares strategy returns against buy-and-hold benchmark returns for the same ticker and date range.
- Supports parameter grid search, walk-forward validation, cross-asset evaluation, transaction-cost sensitivity, regime evaluation, and runtime benchmarking.
- Prevents buying beyond available cash and prevents long-only portfolios from selling more shares than held.
- Exports trades, equity curves, performance summaries, and strategy comparison files.
- Includes Python scripts for yFinance data download and Matplotlib visualization.

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
├── data/
├── include/
│   ├── MarketData.h
│   ├── Event.h
│   ├── Strategy.h
│   ├── Portfolio.h
│   ├── ExecutionHandler.h
│   ├── Backtester.h
│   ├── Analysis.h
│   └── Metrics.h
├── src/
│   ├── main.cpp
│   ├── MarketData.cpp
│   ├── Strategy.cpp
│   ├── Portfolio.cpp
│   ├── ExecutionHandler.cpp
│   ├── Backtester.cpp
│   ├── Analysis.cpp
│   └── Metrics.cpp
├── tests/
│   └── test_engine.cpp
├── scripts/
│   ├── download_data.py
│   └── visualize_results.py
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
mkdir build
cd build
cmake ..
cmake --build .
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

## Run

From the project root after building:

```bash
./build/backtester --mode compare
```

Run a single strategy:

```bash
./build/backtester --mode single --ticker AAPL --strategy ma_cross --capital 100000
./build/backtester --mode single --ticker MSFT --strategy rsi --capital 100000
./build/backtester --mode single --ticker SPY --strategy macd --capital 100000
```

Optional cost controls:

```bash
./build/backtester --mode single --ticker AAPL --strategy ma_cross --transaction-cost 0.001 --slippage 0.0005
```

Reproducible date windows:

```bash
./build/backtester --mode grid --ticker AAPL --start 2020-01-02 --end 2025-12-30
```

Analysis modes:

```bash
./build/backtester --mode cross-asset
./build/backtester --mode grid
./build/backtester --mode walk-forward
./build/backtester --mode cost
./build/backtester --mode regime
./build/backtester --mode benchmark
./build/backtester --mode all
```

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
- `results/regime_evaluation.csv`
- `results/benchmark_timings.csv`
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
ticker,strategy,parameter_set,total_return,benchmark_gross_return,benchmark_net_return,excess_return,annualized_return,volatility,sharpe,max_drawdown,win_rate,profit_factor,num_trades,turnover,total_transaction_costs,cost_drag,average_trade_return
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

## Metrics Explained

- All returns are decimal returns: `0.10` means `10%`.
- Total return: final net portfolio value divided by starting capital minus one. Trading costs are already reflected in the portfolio value.
- Benchmark gross return: buy-and-hold close-to-close return over the same date window, before commission and slippage.
- Benchmark net return: buy-and-hold over the same date window after applying the same commission and slippage assumptions at entry and exit.
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

## Correctness Guarantees and Assumptions

The engine is long-only and uses daily OHLCV bars. Strategy indicators are computed using data available through the current bar close. If a signal is generated, the order is queued and executed at the next bar open with configured slippage and transaction costs. This avoids filling on the same close used to generate the signal. If the next open gaps enough that a buy is no longer affordable, the portfolio rejects the fill rather than allowing negative cash.

The default transaction cost is 10 basis points and default slippage is 5 basis points. Order sizing deploys 95% of available cash on new long entries.

Trade accounting uses slippage-adjusted fill prices. Buy cash outflow is fill notional plus commission. Sell cash inflow is fill notional minus commission. Slippage is not double-counted in cash flows; it is reported separately as measured cost attribution. Realized P&L is recorded only on sells and uses commission-inclusive average entry cost basis.

Open positions at the final bar remain open and are marked to market in the final equity value. They are not force-liquidated unless a strategy generates a sell signal before the dataset ends.

The project does not claim production-grade execution realism, high-frequency behavior, complete bias elimination, or order-book simulation.

## Data Validation Rules

The CSV loader requires exactly:

```csv
Date,Open,High,Low,Close,Volume
```

It rejects malformed rows, missing values, duplicate dates, non-chronological dates, non-finite prices, zero or negative prices, negative volume, `High < Low`, and open/close prices outside the high-low range. It does not silently sort data.

## Indicator Warm-Up

SMA and rolling volatility return zero until enough observations are available. RSI returns a neutral value during warm-up and is bounded to finite values. MACD waits for enough slow EMA and signal observations before producing actionable crossover signals.

## Testing Methodology

The CTest target contains deterministic unit-style checks using small fixed inputs and CSV fixtures under `tests/fixtures/`. It covers indicators, transaction cost and slippage, portfolio accounting, invalid orders, next-bar execution, benchmark windows, metric formulas, and data-quality rejection cases.

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
python3 scripts/validate_results.py
```

The result validator fails on NaN, infinity, missing required columns, negative holdings/cash, positive drawdown, invalid win rates, negative trade counts, duplicate equity dates, and basic equity reconciliation errors.

## Reproducibility

Every run writes `results/run_metadata.json` with the mode, CLI ticker/strategy arguments, starting capital, commission/slippage in basis points, date arguments, execution convention, timestamp, and Git commit hash where available.

## Parameter Search

Grid search evaluates every candidate and exports every result to `results/parameter_grid_results.csv`; it does not only report the best row.

- Moving Average short windows: 5, 10, 20.
- Moving Average long windows: 50, 100, 200.
- RSI periods: 7, 14, 21.
- RSI threshold pairs: 20/80, 25/75, 30/70.
- MACD combinations: 8/17/9, 12/26/9, 19/39/9, 5/35/5.

Invalid combinations, such as short moving averages greater than or equal to long moving averages, are rejected.

## Walk-Forward Validation

Walk-forward validation uses:

- Training window: 3 years, approximated as 756 trading days.
- Testing window: 6 months, approximated as 126 trading days.
- Step size: 6 months.

For each window, candidate parameters are evaluated only in-sample. The objective is:

```text
objective = training Sharpe - 0.25 * abs(training max drawdown)
```

The selected parameters are frozen and then tested on the immediately following out-of-sample period. In-sample selection history and out-of-sample results are written separately.

## Regime Evaluation

Regimes are assigned mechanically, not manually:

- Bull: price above 200-day SMA and 60-day return positive.
- Bear: price below 200-day SMA and 60-day return negative.
- Sideways: all other classified observations.
- High volatility: 60-day annualized rolling volatility above its historical median.

Regime metrics are exported to `results/regime_evaluation.csv`.

## C++ Performance Benchmarks

Runtime benchmarks are exported to `results/benchmark_timings.csv`. On the verified local run:

| Benchmark | Time |
| --- | ---: |
| Naive rolling SMA | 7.247708 ms |
| Optimized rolling SMA | 0.826958 ms |
| Single AAPL backtest | 28.799875 ms |
| Full AAPL parameter sweep | 92.758042 ms |

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
- Portfolio-level multi-asset backtesting.
- Options strategy backtesting.
- Machine learning signal generation.
- Live paper-trading integration.
- Benchmark comparison against buy-and-hold.
- More robust corporate-action handling.
