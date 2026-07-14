# v1.0.0 Release Notes

Version 1.0.0 establishes the first immutable release boundary for the C++ systematic strategy evaluation and robustness platform. It packages the daily-bar research engine, deterministic public synthetic suite, validated release metadata, and reproducibility controls. The Git tag and GitHub Release, rather than the mutable branch head, define the published release.

## Major Capabilities

- Causal close-decision, next-open execution with long-only accounting, commissions, slippage, and benchmark execution parity.
- Single-asset walk-forward research and shared-cash multi-asset portfolio simulation across union calendars.
- MA, RSI, MACD, and volatility-breakout candidate grids; Equal Weight, Inverse Volatility, and Momentum Top-N allocation policies.
- Corporate-action accounting, attribution reconciliation, moving-block bootstrap inference, and selection-risk correction.
- Deterministic serial/parallel scheduling and stochastic methodology version 2 with `portable_bounded_v1`.

## Public Data Boundary

Public canonical reconstruction uses five deterministic synthetic assets generated offline with fixed-point arithmetic, a fixed seed, mixed calendars, missing sessions, corporate actions, and byte-stable serialization. The fixtures validate simulation, portfolio accounting, attribution, bootstrap, selection risk, deterministic parallelism, and reconstruction; their returns are not empirical market evidence.

Five formerly tracked Yahoo-derived CSVs, 289 tracked generated derivatives, and 15 provider-dependent manifests were removed from the current tree. Historical commits remain accessible and are documented. User-supplied real data remains supported through ignored local files, schema validation, and local SHA-256 manifests. Optional acquisition is separated from canonical reconstruction.

## Methodology Preservation

Causal next-open execution, transaction costs, long-only accounting, walk-forward boundaries, benchmark parity, union-calendar valuation, corporate actions, attribution identities, moving-block bootstrap, selection-risk correction, stochastic methodology version 2, and `portable_bounded_v1` are unchanged.

Export precision was increased to preserve strict Python/C++ statistical and selection-risk identities without broadening tolerances.

Cross-platform arithmetic remediation replaced only proven compiler-sensitive financial expressions with explicit deterministic arithmetic. Execution timing, prices, quantities, costs, ordering, accounting policy, statistical method, RNG mapping, and semantic tolerances are unchanged.

## Reproducibility and Supply Chain

The public suite contains 13 reconstruction packages and two suite plans over project-owned synthetic fixtures. Validation dependencies and optional acquisition dependencies are separated and hash-locked. GitHub Actions are pinned to immutable revisions. Release assets include target-runner-smoked Linux and macOS CLI archives, a source/reproducibility bundle, release notes, an SPDX 2.3 SBOM, a validation report, and `SHA256SUMS`.

Manifests bind the exact implementation/configuration commit. The final tag may be a reviewed manifest/evidence descendant only when the executable provenance validator proves that no runtime or methodology path changed after capture.

## Data and License Boundary

Source, documentation, configuration, and project-owned synthetic fixtures are Apache-2.0, Copyright 2026 Mrithunjoy Basumatary. User-supplied and third-party market data is outside that grant. No Yahoo-derived CSV or generated derivative is included in the release tree or assets. Optional yfinance acquisition writes only to ignored local paths and does not grant data rights.

Development was human-directed and AI-assisted. Mrithunjoy Basumatary is the sole named author, copyright holder, maintainer, and release authority.

## Limitations

Synthetic validation does not imply market profitability. Historical empirical findings require equivalent lawful inputs. Historical Git commits contain removed provider files. The daily-bar, calendar, corporate-action, execution, inference, and attribution limitations in `docs/LIMITATIONS.md` remain.

This is not a live-trading system and is not investment advice.
