# Data Provenance

## Public Canonical Inputs

The current public suite uses five independently generated assets: `SYN_EQ_A`, `SYN_EQ_B`, `SYN_EQ_C`, `SYN_BENCH`, and `SYN_CRYPTO`. `scripts/generate_synthetic_market_data.py` uses fixed-point integer arithmetic, a fixed seed (`20260713`), stable civil-date rules, and a repository-owned LCG. It performs no network calls and does not use real prices, returns, provider labels, or downloaded rows.

`data/synthetic/metadata.json` records generator version, seed, schema, calendar type, missing dates, corporate actions, row counts, date ranges, and SHA-256 hashes. `validate_synthetic_market_data.py --regenerate-check` requires byte-for-byte regeneration. The fixtures are project-owned and covered by Apache-2.0.

## Historical Provider Data

Commits through `d806943b5bb6d433e4a56e042fd16acdf5ee726d` contained five Yahoo-derived CSVs acquired through yfinance. Their redistribution authorization was not verified. They and 289 tracked derived result/figure artifacts were removed from the current tree at migration commit `2e92218...`; old public manifests were retired.

The files remain reachable from historical Git commits. Current-tree removal prevents their inclusion in a future tag and source archive that targets a post-migration commit, but it is not historical erasure. No history rewrite or force-push occurred. See `audit/data_release/git_history_data_report.md` and `history_occurrences.csv`.

Historical empirical summaries are retained only as qualified audit context. They are not part of the public canonical suite and cannot be reproduced without equivalent lawfully obtained user inputs.

## User-Supplied Data

Real-market research reads local files from an ignored directory such as `data/local/`. The user chooses the provider, establishes their right to use the data, declares adjustment/corporate-action assumptions, validates the schema, and records local hashes. The project neither redistributes nor sublicenses those inputs.

The optional yfinance script writes to `data/local/` by default. It is acquisition convenience, not release provenance, canonical reconstruction, or permission to use provider content.
