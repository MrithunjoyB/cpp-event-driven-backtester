# Result Schema

Research summaries and methodology outputs use schema version 2. Schema version is owned by export/metadata code, not simulation engines. `BacktestResult`, `PortfolioBacktestResult`, and `BootstrapResult` retain trades, equity, metrics, fills, positions, and sampled distributions in memory before export.

Single-asset summary rows identify strategy parameters, benchmark ticker and execution/cost policy, net and gross benchmark returns, net excess return, risk metrics, turnover, and costs. Walk-forward files explicitly separate in-sample candidates from frozen out-of-sample results and include interval boundaries, capital linkage, benchmark policy, and boundary liquidation costs. Regime rows include the information cutoff and causal volatility-threshold method. Portfolio files contain shared cash, synchronized positions, fills, rebalances, target weights, costs, and portfolio-level risk metrics.

CSV headers are centralized in `CsvResultExporter` for reusable simulation and portfolio results. Research experiment exports retain their established schema-v2 columns for numerical compatibility. JSON manifests include resolved methodology/configuration and run metadata. A future schema change must increment the version, document column semantics, and retain a migration note; existing columns must not silently change meaning.

## Schema V3

Schema v3 applies to union-calendar shared-cash portfolio results under `results/research_v3/`. It adds per-asset valuation marks, tradability and stale age, scheduled/decision/execution rebalance dates, corporate-action records, calendar and annualization metadata, and weekend/stale observation counts. Schema v2 remains the corrected legacy intersection methodology and is not overwritten.

Attribution extends schema v3 under each portfolio's `attribution/` directory. Daily asset rows, period reconciliation, cash, costs, corporate actions, rebalances, benchmark-relative return, drawdowns, covariance risk, causal regimes, and calendar years share experiment, policy, benchmark, adjustment, calendar, methodology, unit, and residual-tolerance metadata. Existing schema-v3 files are unchanged.

Statistical schema-v3 extensions live under each experiment's `statistics/` directory. CSV rows repeat method, seed, simulation count, block length, input series, benchmark, confidence, candidate and observation counts, and annualization metadata. `statistical_manifest.json` records assumptions, while `statistical_input_series.csv` preserves exact dated inputs.

Selection-risk schema-v3 extensions live under `results/research_v3/selection_risk/<experiment>/selection_risk/`. They retain candidate definitions and eligibility, exact dated normalized OOS and active returns, a separately rerun continuous-capital selected-strategy history, window metrics, causal selections, parameter frequencies, IS/OOS ranks and degradation, transitions, neighbourhood sensitivity, family/cross-family/regime reality checks, compact max-statistic distributions, warnings, and a manifest. Parent single-asset experiment configuration remains schema v2; the selection-risk output family is independently versioned as schema v3.
