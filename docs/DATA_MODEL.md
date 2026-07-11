# Data Model

Legacy files use `Date,Open,High,Low,Close,Volume`. Corporate-action-aware files add `AdjustedClose,Dividends,StockSplits`.

Rows must be canonical ISO dates, strictly ordered, unique, finite, and internally valid OHLC values. Sidecar `<ticker>.metadata.json` files record ticker, source, requested and actual ranges, download timestamp, timezone normalization, adjustment provenance, SHA-256, and schema version.

The downloader normalizes yFinance MultiIndex columns deterministically, requests actions explicitly, rejects duplicates and missing adjusted prices, and refuses to replace an incompatible schema unless `--force` is given. Dependencies are bounded in `requirements.txt`. CI tests normalization with an in-memory fixture and never downloads live data.
