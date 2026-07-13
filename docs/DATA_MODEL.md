# Data Model

Legacy files use `Date,Open,High,Low,Close,Volume`. Corporate-action-aware files add `AdjustedClose,Dividends,StockSplits`.

Rows must be canonical ISO dates, strictly ordered, unique, finite, and internally valid OHLC values. Public `data/synthetic/metadata.json` records generator identity, seed, schema, calendars, missing sessions, corporate actions, ranges, row counts, SHA-256 hashes, and synthetic classification. Optional local acquisition may emit provider sidecars under ignored `data/local/`.

Public canonical generation is offline and uses no third-party package. `validate_market_data.py` applies the same structural constraints to provider-neutral user inputs. The optional yfinance downloader normalizes MultiIndex columns, requests actions explicitly, and rejects duplicates or missing adjusted prices, but is not installed or executed by canonical CI.
