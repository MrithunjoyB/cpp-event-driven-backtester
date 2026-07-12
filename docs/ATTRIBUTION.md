# Portfolio Attribution

Attribution explains historical accounting contribution; it does not establish economic causality. The analyzer consumes the exact union-calendar equity curve, per-asset quantities and marks, fills, commission, slippage, dividends, splits, rebalances, and configured benchmark.

For each valuation interval:

```text
ending value = beginning value
             + market P&L
             + dividend income
             + corporate-action effect
             + cash return
             - commission
             - spread cost
             - slippage cost
             + external cash flow
             + residual
```

Trade-aware market P&L removes actual reference-price purchases and sales from marked-value change. This is not an ending-weight approximation. Slippage is separated by reconstructing the unslipped reference price from each fill. Percentage commission is separate; fixed/minimum commission and spread are zero because the current execution model does not support them. Cash earns no interest. Cash drag is a descriptive comparison of average cash with the configured benchmark return.

Splits are expected to have zero economic contribution, while raw-mode dividends equal credited cash. Adjusted modes suppress separate dividend cash. Every daily residual is checked against `1e-8` of beginning portfolio value and exported.

Drawdown contribution sums arithmetic asset contributions from peak to trough. Covariance/Euler volatility and beta contributions are ex-post descriptions over aligned union-calendar observations, not forecasts. Arithmetic currency contribution is additive; geometric portfolio return is compounded and must not be represented as a stacked sum.

Regime aggregation uses existing causal labels. Portfolio-policy runs are not walk-forward experiments, so walk-forward attribution is explicitly marked not applicable. Complete allocation/selection Brinson attribution is not claimed because the configured SPY benchmark has no constituent-level benchmark weights.
