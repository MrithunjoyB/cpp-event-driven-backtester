# v1.0.0 Release Notes (Draft)

This is a draft for a future release. No `v1.0.0` tag or GitHub Release is created by the public-data migration stage.

## Public Data Boundary

Public canonical reconstruction uses five deterministic synthetic assets generated offline with fixed-point arithmetic, a fixed seed, mixed calendars, missing sessions, corporate actions, and byte-stable serialization. The fixtures validate simulation, portfolio accounting, attribution, bootstrap, selection risk, deterministic parallelism, and reconstruction; their returns are not empirical market evidence.

Five formerly tracked Yahoo-derived CSVs, 289 tracked generated derivatives, and 15 provider-dependent manifests were removed from the current tree. Historical commits remain accessible and are documented. User-supplied real data remains supported through ignored local files, schema validation, and local SHA-256 manifests. Optional acquisition is separated from canonical reconstruction.

## Methodology Preservation

Causal next-open execution, transaction costs, long-only accounting, walk-forward boundaries, benchmark parity, union-calendar valuation, corporate actions, attribution identities, moving-block bootstrap, selection-risk correction, stochastic methodology version 2, and `portable_bounded_v1` are unchanged.

Export precision was increased to preserve strict Python/C++ statistical and selection-risk identities without broadening tolerances.

## Limitations

Synthetic validation does not imply market profitability. Historical empirical findings require equivalent lawful inputs. Historical Git commits contain removed provider files. The daily-bar, calendar, corporate-action, execution, inference, and attribution limitations in `docs/LIMITATIONS.md` remain.
