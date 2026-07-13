# Statistical Methodology

The production engine defaults to a circular moving-block bootstrap over consecutive union-calendar portfolio returns or explicitly labelled continuous-OOS returns. Chronology is retained inside each block. The default block length is the rounded cube root of observation count; this is transparent guidance, not an optimality claim. IID resampling is available only as a labelled comparison.

Every run records input dates, exact returns, benchmark alignment, seed, simulations, block length, confidence level, annualization factor, candidate count, and warnings. Empty, duplicate, non-finite, short, misaligned, normalized-window, and undisclosed full-sample inputs are rejected.

Bootstrap distributions cover compounded return, annualized return, volatility, Sharpe, Sortino, drawdown, Calmar, terminal wealth, active return, and Information Ratio. Empirical percentile intervals and probabilities are reported. Sharpe inference is bootstrap-based; Probabilistic or Deflated Sharpe Ratios are deliberately not claimed.

Selection risk uses a centered circular moving-block max-mean reality check. The null is that no candidate has positive expected active return versus the configured benchmark. Every eligible parameter candidate is evaluated on each causal walk-forward test window from common normalized capital, including configured execution costs and end-boundary liquidation. These counterfactual paths are diagnostic and remain separate from the selected strategy's deployable continuous-capital OOS path.

Candidate active returns and their cost-matched next-open buy-and-hold benchmark are intersected on exact common dates without imputation. Duplicate dates, non-finite returns, mixed metadata, insufficient common samples, and diagnostic/deployable mislabelling are rejected. Tests are run independently by ticker within each strategy family and over the combined four-family universe. Candidate IDs are deterministic hashes of the experiment, family, ticker, canonical parameters, benchmark, costs, walk-forward design, data context, and schema.

The bootstrap centers each candidate by its sample mean, resamples common panel dates with circular moving blocks, and records the distribution of the maximum centered mean. The finite-sample adjusted p-value is `(1 + exceedances) / (simulations + 1)`. Selection frequency, parameter-value frequency, IS/OOS rank correlation, transitions, degradation, and adjacent canonical-grid sensitivity are exported with transparent definitions.

Regime-conditioned tests use the existing causal classifier and reject subsets with fewer than 30 common observations. They are exploratory because the primary correction does not additionally adjust across regime slices. Portfolio-policy reality checks remain one-policy diagnostics and must not be confused with strategy-grid correction. Probabilistic and Deflated Sharpe Ratios remain deliberately unclaimed.

Statistical confidence is conditional historical evidence and does not imply future profitability.

## Release Portability Gate

The final audit found that `std::uniform_int_distribution` produces different index mappings across libc++ and libstdc++. Fixed seeds therefore repeat within one standard-library implementation but do not define one portable stochastic path. TSLA MACD and zero-cost adjusted p-values are close enough to 0.05 that current Monte Carlo and platform-mapping variation can change an inferential band. The existing outputs remain historical methodology-v3 evidence, but they are not an acceptable v1.0.0 stochastic baseline. Release requires the platform-stable sampler and methodology migration specified in [Final Audit](FINAL_AUDIT.md).
