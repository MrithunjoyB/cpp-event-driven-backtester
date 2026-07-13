# Limitations

- Public canonical inputs are synthetic. Their performance is validation evidence, not empirical market evidence.
- Historical real-market summaries require equivalent lawful user-supplied inputs and are not publicly reconstructible from the current tree.
- Historical Git commits still expose formerly tracked Yahoo-derived CSVs; no history rewrite was performed.
- The engine is daily-bar and long-only by default. It does not model tick/order-book queues, taxes, financing, borrow costs, or withholding taxes.
- Exchange closures are inferred from input availability; authoritative exchange calendars are not integrated.
- Payable-date settlement, delistings, symbol changes, and cash-in-lieu processing are incomplete.
- User data may have incomplete or ambiguous adjustment and corporate-action provenance.
- Candidate panels remain conditional on eligibility and strict common-date intersection. Regime slices are exploratory.
- Attribution is accounting description, not economic causality. Statistical evidence does not imply future profitability.
