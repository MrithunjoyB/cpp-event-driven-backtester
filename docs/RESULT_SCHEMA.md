# Result Schema

Research summaries and methodology outputs use schema version 2. Schema version is owned by export/metadata code, not simulation engines. `BacktestResult`, `PortfolioBacktestResult`, and `BootstrapResult` retain trades, equity, metrics, fills, positions, and sampled distributions in memory before export.

Single-asset summary rows identify strategy parameters, benchmark ticker and execution/cost policy, net and gross benchmark returns, net excess return, risk metrics, turnover, and costs. Walk-forward files explicitly separate in-sample candidates from frozen out-of-sample results and include interval boundaries, capital linkage, benchmark policy, and boundary liquidation costs. Regime rows include the information cutoff and causal volatility-threshold method. Portfolio files contain shared cash, synchronized positions, fills, rebalances, target weights, costs, and portfolio-level risk metrics.

CSV headers are centralized in `CsvResultExporter` for reusable simulation and portfolio results. Research experiment exports retain their established schema-v2 columns for numerical compatibility. JSON manifests include resolved methodology/configuration and run metadata. A future schema change must increment the version, document column semantics, and retain a migration note; existing columns must not silently change meaning.

## Schema V3

Schema v3 applies to union-calendar shared-cash portfolio results under `results/research_v3/`. It adds per-asset valuation marks, tradability and stale age, scheduled/decision/execution rebalance dates, corporate-action records, calendar and annualization metadata, and weekend/stale observation counts. Schema v2 remains the corrected legacy intersection methodology and is not overwritten.
