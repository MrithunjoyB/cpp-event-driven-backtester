# Methodology

Decision time is the close-information cutoff used to calculate target weights. Execution time is the later session open at which a current tradable bar exists. Valuation time is each union-calendar close. Market-data availability time is represented by the source bar date; stale marks identify an earlier source date and age.

Deferred targets are frozen at their original decision cutoff. Closed assets may be valued but cannot trade. Portfolio value reconciles as cash plus marked holdings; consecutive union values feed drawdown and risk metrics. BTC weekend moves therefore affect mixed-asset risk while closed equity marks remain constant.

Schema v3 is an intentional methodology change for shared-cash portfolios. Schema-v2 single-asset, walk-forward, causal regime, benchmark-parity, and explicit legacy-intersection behavior remain unchanged.

Attribution is an additive currency-P&L decomposition of consecutive union-calendar valuations. It is distinct from geometric compounded return and is descriptive rather than causal. Deferred and partial rebalances use their recorded scheduled, decision, and execution dates; holding-period contribution is measured after actual execution.

Statistical inference defaults to moving-block resampling of continuous linked returns. IID bootstrap is comparison-only. Normalized-window OOS curves cannot be treated as deployable histories, and full-sample inputs require an explicit diagnostic label.

Grid-wide selection-risk analysis retains every eligible candidate's exact dated OOS returns. Each candidate/window path starts from normalized capital and liquidates with configured costs, so it is a counterfactual diagnostic rather than a deployable capital history. Training rank and eligibility use only the preceding training window. Candidate and cost-matched benchmark returns are compared only on exact common dates. Family-wise and combined panels use the centered circular moving-block max-mean reality check documented in `STATISTICAL_METHODOLOGY.md`.
