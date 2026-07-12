# Testing

Configure, build, and run every registered test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Targets cover the preserved regression suite, typed date/config behavior, causal methodology, exporters, deterministic bootstrap analysis, and CLI smoke checks. Fixtures in `tests/fixtures` are local and deterministic; tests never download live data.

`calendar_tests`, `union_portfolio_tests`, and `corporate_action_tests` cover union/intersection timelines, stale marks, weekend risk, closed-market execution prevention, civil schedules, causal deferral, splits, reverse splits, dividends, adjusted-mode double-count prevention, and invalid actions. Run `python3 scripts/test_download_data.py` for deterministic downloader normalization.

`attribution_tests` covers pure appreciation, multiple assets, actual trade flows, commission/slippage decomposition, cash treatment, stale marks, split neutrality, dividends, drawdown recovery, volatility/beta contribution, year aggregation, schema export, and residual rejection. The Python validator independently recomputes accounting identities and contribution sums.

Methodology coverage includes next-bar execution, causal regime attribution, calendar walk-forward boundaries, continuous OOS capital, benchmark execution parity, and configured benchmark propagation. `tests/fixtures/regression/stage0_architecture_baseline.csv` records eight numerical snapshots from commit `dc040a9...`; compare generated artifacts with:

```bash
python3 scripts/check_regression_snapshots.py
python3 scripts/validate_results.py results
```

Separate sanitizer builds are supported:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DQUANT_SANITIZER=address
cmake --build build-asan --parallel && ctest --test-dir build-asan --output-on-failure
cmake -S . -B build-ubsan -DCMAKE_BUILD_TYPE=Debug -DQUANT_SANITIZER=undefined
cmake --build build-ubsan --parallel && ctest --test-dir build-ubsan --output-on-failure
```
