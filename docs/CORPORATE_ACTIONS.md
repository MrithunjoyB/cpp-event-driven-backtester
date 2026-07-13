# Corporate Actions

The expanded CSV schema carries `AdjustedClose`, `Dividends`, and `StockSplits`. Values must be finite; dividends cannot be negative and split ratios must be zero or positive.

Under `raw_price`, a split multiplies quantity by the declared ratio before same-date execution and a reverse split works identically with a ratio below one. Fractional quantities are retained. Cash dividends are credited once on the ex-date. Under `split_adjusted` and `total_return_adjusted`, adjusted prices are required. Corporate-action rows remain auditable, but quantities and cash are not separately changed, preventing double counting.

Some providers, including the optional Yahoo/yfinance acquisition path, expose adjusted closes that may reflect both distributions and splits. `split_adjusted` is therefore defensible only when user-supplied source metadata establishes a split-only series. The project does not invent missing actions.

Current support does not model payable-date settlement, withholding tax, cash-in-lieu for fractional shares, symbol changes, or delistings.
