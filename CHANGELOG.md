# Changelog

This project follows Keep a Changelog conventions.

## [Unreleased]

No unreleased changes.

## [1.0.0] - 2026-07-14

### Added

- Modular `quant_core` C++17 library and thin typed-configuration CLI.
- Causal next-open event flow, explicit commissions and slippage, long-only accounting, benchmark parity, and calendar walk-forward evaluation.
- Shared-cash portfolio simulation with union-calendar valuation, allocation policies, stale-mark controls, and corporate-action accounting.
- Reconciled portfolio, drawdown, regime, calendar-year, benchmark-relative, and transaction-cost attribution.
- Circular moving-block bootstrap, IID comparison, centered reality-check diagnostics, complete candidate-grid selection-risk evaluation, and deterministic parallel candidate execution.
- Repository-owned `portable_bounded_v1` sampling with stochastic methodology version 2 and cross-language golden vectors.
- Offline deterministic five-asset synthetic fixture generator and validator.
- Provider-neutral user-supplied market-data validation and local hash manifests.
- Public data-boundary validation with removed-file hashes and sampled-row fingerprints.
- Public synthetic canonical suite with 13 packages and two suite plans.
- Data lineage, Git-history exposure, provenance, input, and blocker-closure documentation.
- Hash-locked Python validation and acquisition environments, dependency-license inventory, SPDX SBOM generation, checksums, and validated Linux/macOS release packaging.

### Changed

- Public configurations and defaults now use `SYN_EQ_A`, `SYN_EQ_B`, `SYN_EQ_C`, `SYN_BENCH`, and `SYN_CRYPTO` under `data/synthetic/`.
- Export precision increased to preserve strict cross-language statistical identities.
- Historical empirical findings are classified as local, non-release-canonical evidence.
- Cross-platform realized-P&L, union-calendar valuation, and financially relevant aggregation arithmetic use explicit deterministic operations where compiler contraction previously changed serialized values.
- Strict-warning builds treat warnings as errors on supported compilers.

### Removed

- Five Yahoo-derived CSVs from the current tree.
- 289 tracked generated result and figure artifacts derived from the former public inputs.
- 15 provider-dependent public manifests.

### Security and Provenance

- Local data and generated results are ignored and rejected from public manifests.
- No history rewrite or force-push was performed; historical blob exposure is documented.

### Known Limitations

- Historical commits still contain the removed provider files.
- Synthetic performance is not empirical market evidence.
- Daily-bar and long-only scope; no live trading, order-book simulation, authoritative exchange calendars, taxes, financing, or complete corporate-action settlement.
- Exact reconstruction covers the documented source, data, dependency, and semantic boundaries; system compiler and operating-system packages are recorded rather than hermetically vendored.
