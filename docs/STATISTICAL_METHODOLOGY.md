# Statistical Methodology

The production engine defaults to a circular moving-block bootstrap over consecutive union-calendar portfolio returns or explicitly labelled continuous-OOS returns. Chronology is retained inside each block. The default block length is the rounded cube root of observation count; this is transparent guidance, not an optimality claim. IID resampling is available only as a labelled comparison.

Every run records input dates, exact returns, benchmark alignment, seed, simulations, block length, confidence level, annualization factor, candidate count, and warnings. Empty, duplicate, non-finite, short, misaligned, normalized-window, and undisclosed full-sample inputs are rejected.

Bootstrap distributions cover compounded return, annualized return, volatility, Sharpe, Sortino, drawdown, Calmar, terminal wealth, active return, and Information Ratio. Empirical percentile intervals and probabilities are reported. Sharpe inference is bootstrap-based; Probabilistic or Deflated Sharpe Ratios are deliberately not claimed.

Selection risk uses a centered circular moving-block max-mean reality check. The null is that no candidate has positive expected active return versus the configured benchmark. It controls for searching the supplied candidate family but remains sensitive to candidate definition, block length, and sample size. A one-policy run is diagnostic and not a substitute for grid-wide continuous-OOS candidate histories.

Statistical confidence is conditional historical evidence and does not imply future profitability.
