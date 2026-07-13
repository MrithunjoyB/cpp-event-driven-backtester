# Final Independent Audit Report

## Release Gate

**C. FAIL — RNG MIGRATION REQUIRED**

No core execution, accounting, calendar, attribution, candidate-selection, concurrency, or deterministic financial-output defect was found. The release is blocked because stochastic index mapping is standard-library-dependent and current inferential tolerances overlap decision thresholds.

## Principal Evidence

- Production uses `std::mt19937` with `std::uniform_int_distribution<std::size_t>` in both bootstrap paths.
- TSLA MACD adjusted p-values are close to 0.05; the canonical margin and zero-cost margin are smaller than one binomial Monte Carlo standard error at 1,000 simulations.
- Current family/cross-family tolerance is 0.05, which can approve values on opposite sides of the 0.05 decision boundary.
- An exploratory TSLA regime slice already crosses 0.05 between observed platforms.
- Momentum probability diagnostics vary enough to cross 0.5 and approach the 0.95 evidence boundary.
- Deterministic financial, portfolio, attribution, candidate, selected OOS, and concurrency outputs remain stable.

## Finding Summary

- Critical: 0
- High: 2 unresolved release blockers
- Medium: 2
- Low: 1

Detailed evidence is in the accompanying CSV files. Closure requires the migration specified in `rng_migration_decision.md`; this audit does not authorize or implement it.

## Methodology and Engineering Results

| Surface | Result | Evidence boundary |
| --- | --- | --- |
| Execution causality | Verified | Signals are produced after close information and pending orders fill at the next eligible open; end-boundary liquidation is separately labelled at close. |
| Cash, long-only, and costs | Verified | Affordability and oversell checks reject invalid fills; slippage enters fill price once and attribution records its explicit cost once. |
| Walk-forward and calendars | Verified | Civil dates, leap/month clamping, strict train/test boundaries, union calendars, stale-mark expiry, and causal regime cutoffs have deterministic fixtures. |
| Strategy selection | Verified | 41 candidates per ticker, 205 combined definitions, 1,025 candidate-window rows, one family/window selection, and no deployable duplicate dates. |
| Attribution | Verified | Period identity rejects residuals above configured scale; split/dividend/cost fixtures and independent Python corruption checks pass. |
| Bootstrap formulas | Verified | Circular blocks, path lengths, percentile intervals, probability counts, Sharpe inputs, and finite-sample `(1+e)/(B+1)` correction match independent references. |
| Reality check | Verified except portability | Centering, joint date sampling, max-mean statistic, candidate universes, and common-date alignment are supported; bounded-index mapping is platform-dependent. |
| Concurrency/performance | Verified within scope | Indexed task collection, immutable data reuse, serial causal paths, TSan, and thread equivalence preserve deterministic non-stochastic outputs. |
| Reproducibility | Partially verified | Input/config/hash/staging/rollback controls work, but Level 3 inferential tolerances do not preserve threshold classifications by construction. |

## Monte Carlo and Threshold Assessment

At 1,000 simulations, TSLA MACD's canonical distance from 0.05 is less than one binomial standard error. The zero-cost distance is smaller still. A fixed seed makes one run repeatable but does not establish a stable inferential conclusion under another valid index mapping or seed. Observed platform differences are consistent with different Monte Carlo samples, while the current tolerances are empirically fitted to too few platform/seed pairs and overlap decision boundaries.

## Validation, Security, and Hygiene

Validators strongly reject malformed schemas, hashes, duplicate/missing rows, invalid bounds, failed commands, dirty/wrong commits, and attribution residuals. The audit adds the missing threshold/tolerance gate. Supply-chain review found exact Python versions without distribution hashes and mutable GitHub Action tags. No secrets or personal paths were found in tracked manifests or audit artifacts. Generated builds/results remain ignored; historical schemas are clearly compatibility-labelled.

## Expert Recommendations

| Priority | Timing | Recommendation | Rationale and expected value | Risk | Implemented |
| --- | --- | --- | --- | --- | --- |
| Important | now | Treat the stable bounded-sampler migration and methodology-v4 baseline as the next controlled stage. | Removes the only observed source of conclusion-changing platform variation and permits exact stochastic reconstruction. | Baseline migration must be reviewed rather than presented as continuity. | No; outside audit authority. |
| Important | before release | Regenerate strict manifests at the final audited commit and disallow compatible-descendant release reconstruction. | Makes release lineage exact and closes the current manifest-source ambiguity. | New hashes must not be accepted without clean Linux/macOS reconstruction. | No; release-stage action. |
| Recommended | before release | Hash-pin Python distributions and GitHub Actions. | Narrows supply-chain variability in validation and CI. | Maintenance overhead when dependencies update. | No; outside audit scope. |
| Optional | after v1.0.0 | Add authoritative exchange calendars when source licensing and provenance are settled. | Improves closure/holiday semantics beyond data-availability inference. | Provider coupling and historical-calendar provenance complexity. | No. |
