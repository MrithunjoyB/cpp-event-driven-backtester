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
│   └── Metrics.h
├── src/
│   ├── main.cpp
│   ├── MarketData.cpp
│   ├── Strategy.cpp
│   ├── Portfolio.cpp
│   ├── ExecutionHandler.cpp
│   ├── Backtester.cpp
│   └── Metrics.cpp
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

## Run

Run the default comparison across AAPL, MSFT, and SPY using all three strategies:

```bash
./backtester
```

Run a single strategy:

```bash
./backtester --ticker AAPL --strategy ma_cross --capital 100000
./backtester --ticker MSFT --strategy rsi --capital 100000
./backtester --ticker SPY --strategy macd --capital 100000
```

Optional cost controls:

```bash
./backtester --ticker AAPL --strategy ma_cross --transaction-cost 0.001 --slippage 0.0005
```

## Output Files

The engine writes:

- `results/trades.csv`
- `results/equity_curve.csv`
- `results/performance_summary.csv`
- `results/strategy_comparison.csv`
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
ticker,strategy,total_return,annualized_return,volatility,sharpe,max_drawdown,win_rate,profit_factor,num_trades,average_trade_return,transaction_cost_adjusted_return
```

## Visualize Results

```bash
python3 scripts/visualize_results.py
```

The script plots:

- Equity curve
- Drawdown curve
- Strategy comparison bars for return, Sharpe ratio, and max drawdown

## Metrics Explained

- Total return: final portfolio value divided by starting capital minus one.
- Annualized return: total return converted to a 252-trading-day annual rate.
- Volatility: standard deviation of daily portfolio returns annualized by sqrt(252).
- Sharpe ratio: annualized return proxy divided by annualized volatility.
- Maximum drawdown: largest peak-to-trough portfolio decline.
- Win rate: winning closed trades divided by total closed trades.
- Profit factor: gross profit divided by gross loss.
- Average trade return: average return across closed sell trades.
- Transaction-cost-adjusted return: ending performance adjusted for recorded costs and slippage.

## Example Strategy Comparison

Generated from the default yFinance date range in `scripts/download_data.py`.

| Ticker | Strategy | Total Return | Sharpe | Max Drawdown | Win Rate | Trades |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| AAPL | MA Cross | 92.79% | 0.66 | -29.14% | 43.75% | 33 |
| AAPL | RSI | 36.74% | 0.34 | -28.83% | 64.71% | 35 |
| AAPL | MACD | 97.74% | 0.71 | -21.47% | 45.45% | 110 |
| MSFT | MA Cross | 27.86% | 0.32 | -41.73% | 41.18% | 34 |
| MSFT | RSI | 82.74% | 0.58 | -20.65% | 83.33% | 37 |
| MSFT | MACD | -12.31% | -0.04 | -39.84% | 35.00% | 121 |
| SPY | MA Cross | 44.55% | 0.59 | -30.03% | 50.00% | 25 |
| SPY | RSI | 22.08% | 0.29 | -27.37% | 73.33% | 30 |
| SPY | MACD | 19.49% | 0.32 | -17.04% | 48.44% | 129 |

## Design Notes

The implementation is intentionally long-only. Signals become orders only when they are feasible: buy orders require available cash, and sell orders require an existing position. The execution handler applies transaction costs and slippage before the portfolio is updated, so reported results are cost-aware.

## Future Improvements

- More realistic order book simulation.
- Portfolio-level multi-asset backtesting.
- Walk-forward optimization.
- Parameter tuning and grid search.
- Options strategy backtesting.
- Machine learning signal generation.
- Live paper-trading integration.
- Benchmark comparison against buy-and-hold.
- More robust corporate-action handling.
