# Performance Engineering

## Scope

Performance work is constrained by exact research semantics. The serial path remains the reference implementation. Only independent candidate training and counterfactual OOS simulations inside a fixed selection-risk window run concurrently; causally ordered events, linked OOS capital, shared-cash accounting, bootstrap reductions, output assembly, and filesystem publication remain serial.

## Method

The benchmark harness uses a strict Release build, fixed repository datasets/configurations/seeds, `std::chrono::steady_clock` wall time, a warm-up for the short single-backtest case, and repeated measurements. Representative workloads use three repetitions. The expensive complete seven-package workflow uses two repetitions per execution mode and is therefore reported with its range, not treated as a precise population estimate. Peak RSS is a single-run `getrusage(RUSAGE_CHILDREN)` high-water mark.

Measurements are machine-specific. The local machine was an Apple M1 with 8 cores and 8 GB RAM, AppleClang 17, and macOS 15.7.5. CPU frequency scaling, background activity, and filesystem cache state were not controlled beyond documenting warm-cache behavior. macOS `sample` could not attach in the managed environment, so bottleneck attribution combines internal steady-clock stage counters with controlled before/after elimination; it is not external-profiler self-time.

## Retained Optimizations

- Each selection-risk application run loads every required ticker once into immutable shared `MarketData` ownership. Malformed input still fails through the original parser and failures are not cached.
- Backtests validate configured boundary dates once and compare validated ISO civil-date strings in the bar loop.
- One training and one test buy-and-hold path are reused per exact ticker/window context instead of rerunning an identical benchmark for every candidate.
- A bounded repository-owned executor enumerates stable task indices, gives each task isolated mutable state, stores results by index, propagates exceptions after joining workers, and never writes files concurrently.

Indicator caching and generalized global caches were rejected. The measured end-to-end gain came from data and benchmark reuse; adding cache keys for adjustment policy and every indicator/data context would add memory and invalidation complexity without evidence of a further material benefit. Bootstrap parallelism and floating-point reductions were also rejected because their serial fraction is methodologically sensitive and changing summation or random-stream order would threaten exact equivalence.

## Results

Local Release medians:

| Workload | Baseline serial | Optimized serial | Speedup |
| --- | ---: | ---: | ---: |
| MA selection risk | 8.63 s | 0.99 s | 8.71x |
| Combined selection risk | 24.08 s | 6.02 s | 4.00x |
| Seven-package workflow | 109.06 s | 22.05 s | 4.95x |

Combined-package thread scaling:

| Threads | Median | Speedup vs optimized serial | Efficiency |
| ---: | ---: | ---: | ---: |
| 1 | 4.648 s | 1.00x | 100.0% |
| 2 | 4.393 s | 1.06x | 52.9% |
| 4 | 5.658 s | 0.82x | 20.5% |
| 8 | 5.069 s | 0.92x | 11.5% |

The isolated combined-package scaling run was noisy and does not establish a universal local optimum. The more representative complete seven-package 4-thread median was 14.45 s, a further 1.53x over optimized serial. Scaling is limited because regime analysis, alignment, bootstrap, export, and linked-capital work remain serial. A representative optimized combined run attributed about 13 ms to data loading, 400 ms to candidate/benchmark evaluation, 1.80 s to regime analysis, and 1.62 s to alignment/bootstrap/export/selected continuity.

Single-backtest and shared-portfolio paths do not use the selection-risk reuse/executor boundary; their small run-to-run changes are treated as noise rather than gains. Separate single-run peak-RSS measurements were 46.3 MB serial and 51.3 MB at four threads, an approximately 10.7% increase. These high-water marks are not distributions, but they show the bounded parallel path has a modest, not unbounded, memory cost.

## Determinism and Validation

The seven selection-risk packages were byte-identical across serial, 2, 4, and 8 threads for all canonical files. Only `parallel_execution_metadata.json` and `performance_counters.csv` are excluded because they intentionally describe execution and timing. Task completion order does not control ranking, candidate IDs, seeds, rows, or file publication. Randomized inference remains serial with the existing fixed seed behavior.

Release, ASan, UBSan, and TSan test matrices cover the executor and unchanged research paths. Generated machine-specific artifacts are written to `results/performance/` and remain untracked; `scripts/validate_performance_results.py` rejects missing, empty, non-finite, or failed equivalence evidence.
