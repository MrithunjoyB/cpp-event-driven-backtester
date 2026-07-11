# Market Calendar

Portfolio schema v3 uses a sorted union of all asset bar dates. A `TradingSession` contains one `AssetSession` per ticker, separating bar presence, tradability, execution permission, mark availability, mark source, freshness, and stale age. A last-known close may value a closed asset within the configured calendar-day limit, but it is never an execution price.

Decision information is cut off before execution. Orders execute only against a current bar's open. Monthly scheduling selects the first union valuation of each civil month. Weekly scheduling selects the first union valuation in each Monday-through-Sunday civil week. The export records scheduled, decision, and execution dates. `defer` freezes targets and the decision timestamp until all assets are tradable; `skip_asset` and `partial_rebalance` execute only tradable assets.

Union returns use consecutive union valuations. `inferred_observed_periods` derives periods per year from observation count and elapsed civil days; `configured` uses `configured_periods_per_year`. Benchmarks use the same observations and marks.

`intersection_legacy` remains schema v2 and preserves the corrected historical baseline. The project does not yet embed exchange holiday calendars, so absence of a bar is treated as market closure. This is not complete exchange-calendar accuracy.
