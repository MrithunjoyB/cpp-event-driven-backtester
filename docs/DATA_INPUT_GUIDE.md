# Data Input Guide

## Schema

Files use UTF-8 CSV, one ticker per file, strictly increasing unique `YYYY-MM-DD` dates, positive OHLC prices, and nonnegative volume:

```text
Date,Open,High,Low,Close,Volume
```

The corporate-action schema adds:

```text
AdjustedClose,Dividends,StockSplits
```

`High` must be at least `Open` and `Close`; `Low` must be no greater than either. `AdjustedClose` must be positive. Dividends and split ratios must be nonnegative; zero means no action. Users must select a configuration adjustment policy consistent with the supplied columns and provider semantics.

## Local Workflow

```bash
mkdir -p data/local
python3 scripts/validate_market_data.py data/local/LOCAL.csv
python3 scripts/generate_local_data_manifest.py \
  --data-directory data/local --tickers LOCAL \
  --provider user_declared \
  --output data/local/local_data_manifest.json
```

Set `ticker_universe`, `benchmark`, and `data_directory` in a local configuration, then run:

```bash
./build/quant_cli validate-config --config data/local/local_experiment.json
./build/quant_cli run --config data/local/local_experiment.json
```

Re-run validation and local manifest generation before reconstruction. A changed SHA-256 identifies a different input package. Unresolved placeholders in `manifests/templates/local_real_data_manifest.template.json` are templates only and are rejected from public manifests.

Optional acquisition is isolated from canonical reconstruction:

```bash
python3 -m pip install --require-hashes -r requirements-acquisition.lock
python3 scripts/download_data.py --output-directory data/local
```

The user is responsible for provider authorization, terms, data quality, adjustment semantics, and any redistribution restrictions. Local files, manifests, results, and credentials must not be committed.
