# Public Data Boundary Report

## Decision

The current migration tree contains project source, documentation, five deterministic synthetic market-data fixtures, validators, public manifests, and user-local workflow templates. It contains no tracked Yahoo-derived CSV and no tracked generated `results/` artifact.

Public canonical identity is `public_reproducibility_suite`, comprising 13 package manifests and two suite plans. Every package hashes `data/synthetic/metadata.json` and all five synthetic CSVs, records generator `synthetic_market_data_v1`, version `1`, seed `20260713`, classification `synthetic`, and redistribution status `project_owned_apache_2_0`.

## Executable Controls

`scripts/validate_public_data_boundary.py` rejects:

- the five removed paths and exact SHA-256 hashes;
- 15 sampled row fingerprints from the removed files;
- untracked or missing synthetic canonical inputs;
- provider, local, unresolved-placeholder, or network-dependent public manifests;
- ambiguous data classifications or redistribution status;
- tracked generated results;
- release archives naming removed provider paths;
- missing or non-deterministic synthetic metadata.

`validate_synthetic_market_data.py --regenerate-check` independently regenerates every fixture and compares bytes. Corruption tests alter an OHLC relationship while updating the declared file hash and still require rejection.

## Local Data

`data/local/` is ignored. `validate_market_data.py` validates six- and nine-column files, and `generate_local_data_manifest.py` records local hashes and provider responsibility. Optional yfinance acquisition writes locally and is outside public reconstruction.

## Historical Evidence

Historical empirical summaries remain qualified audit context only. Raw provider files, their old manifests, and their tracked result/figure derivatives are excluded from the current release tree. Historical commits still contain the old blobs; see `git_history_data_report.md`.
