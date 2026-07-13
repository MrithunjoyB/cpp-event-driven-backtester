# RNG Migration Decision

## Decision

**C. FAIL — RNG MIGRATION REQUIRED**

The current `std::mt19937` engine is portable, but `std::uniform_int_distribution` does not specify one cross-library mapping. Current inferential tolerances overlap release decision thresholds. v1.0.0 must wait.

## Required Migration

1. Retain `std::mt19937` as the 32-bit engine or adopt an explicitly versioned engine with published state-transition vectors.
2. Add a repository-owned `stable_bounded_uint32(engine, bound)` using the Lemire multiply-high method with rejection: compute the 64-bit product of one engine word and `bound`; reject low words below `(-bound) % bound`; return the upper 32 bits. Reject zero bounds and bounds above the supported 32-bit domain.
3. Document unbiasedness: each accepted source interval has equal cardinality; rejection removes the incomplete range.
4. Define exact engine-word consumption, including rejection draws, and publish vectors for multiple bounds, seeds, rejection cases, IID samples, and circular block starts.
5. Replace both production mappings in `StatisticalAnalysis.cpp` and `BootstrapAnalyzer.cpp` through one shared implementation.
6. Increment the statistical methodology version (recommended `stochastic_sampling_v4`) and add engine/mapping identifiers to manifests and CSV metadata.
7. Regenerate every bootstrap, portfolio-policy, family, combined, zero-cost, high-cost, regime, figure, report, and reproducibility baseline affected by resampling.
8. Preserve current artifacts as historical v3 fixtures; do not present them as the new canonical baseline.
9. Add cross-platform reference-vector CI, two-seed threshold tests, repeated-run equality, and serial/2/4/8-thread equality.
10. Close the blocker only when Linux/libstdc++ and macOS/libc++ produce identical sampled indices, distributions, summaries, p-values, and conclusion bands with zero inferential tolerance.
