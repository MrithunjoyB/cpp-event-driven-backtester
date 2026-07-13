# Reproducibility

## Policy

Canonical reconstruction uses the five repository-tracked OHLCV files only after exact SHA-256 verification. `download_data.py` remains an acquisition convenience, but a fresh Yahoo/yFinance download is mutable and never qualifies as the same canonical input. Each manifest records file size, row count, date range, schema, adjustment context, source description, acquisition boundary, mutability classification, and redistribution caveat. Corporate-action provenance in the downloaded data remains incomplete.

The manifest schema is versioned at `manifests/schema/reproducibility-manifest-v1.schema.json`. Runtime validation is deliberately stricter than the descriptive JSON Schema: unknown root fields, unsupported versions, invalid identities, duplicate inputs/artifacts, unsafe paths, missing lineage, undeclared validators, nonzero tolerances, and malformed suites fail before execution.

## Reproducibility Levels

| Level | Contract |
| --- | --- |
| Exact byte | Identical verified inputs and byte-stable artifact SHA-256 values. Used only when serialization and metadata are stable. |
| Canonical semantic | Identical schemas, row order, values, identities, selections, trades, accounting, statistics, seeds, warnings, and methodology metadata after documented volatile JSON fields are excluded. No financial value is rounded. |
| Methodological | Same verified inputs, configuration, and methodology with explicit field-level tolerances or a declared shape-only policy for raw stochastic paths. |

PNG/SVG/PDF figures are presentation-only because renderer, font, compression, and metadata differences are platform-sensitive. `performance_counters.csv` and `parallel_execution_metadata.json` are environment-only. Known volatile JSON fields include timestamps, staging output paths, host/user fields, elapsed time, and compiled commit metadata; the manifest independently verifies source provenance.

The C++ bootstrap currently uses the standard library's integer-distribution mapping. Fixed seeds are deterministic within one standard-library implementation, but libc++ and libstdc++ generate different resampled paths. Changing that generator would alter the established methodology outputs, so this stage does not rewrite it. Raw bootstrap distribution/path files therefore require identical schema, row count, seed, method, simulation count, and validator success rather than cross-platform variate identity. Derived bootstrap summaries use explicit field-level tolerances recorded in each manifest: return upper bounds permit at most 3.0, return lower bounds 0.25, Sharpe fields 0.10, probability fields 0.04, p-values 0.01, and the remaining distribution summary fields their documented per-column limits. Evidence conclusions use preregistered bands of at most 0.05, at least 0.95, or inconclusive between them; observed Linux/macOS differences do not change these classifications. Deterministic simulation, portfolio, attribution, selection, and input-series artifacts remain zero-tolerance canonical semantic comparisons.

Selection-risk max-statistic resampling has the same standard-library boundary. Raw max-statistic distributions use shape/method/seed validation and family/cross-family adjusted p-value fields permit at most 0.05 absolute cross-platform Monte Carlo variation. Exploratory regime-slice p-values permit 0.06 and are not promoted to corrected inferential conclusions. Candidate definitions, eligibility, dated candidate and active returns, selected deployable OOS history, parameter selections, ranks, transitions, degradation, neighbourhood diagnostics, warnings, and all financial values remain zero-tolerance semantic comparisons.

One compiler-sensitive diagnostic field, `is_oos_spearman_rank_correlation`, permits `1e-15` absolute variation; observed GCC/AppleClang variation is approximately `1.83e-17`. Candidate rank ordering and every selection field remain exact.

CSV semantic hashing parses the exact cell strings and preserves header and row order. JSON hashing sorts keys recursively, normalizes path separators, and removes only declared volatile fields. Markdown/text hashing normalizes line endings. No tolerance or numerical rounding is applied. Every output records its type, schema, size, row count where applicable, byte hash where stable, semantic hash, required status, validator, and parent lineage.

## Manifest Identity

`manifest_id` is SHA-256 over canonical JSON containing the schema version, experiment/package identity, implementation source commit, ordered logical input hashes, configuration hash, methodology version, seed, and execution policy. It excludes timestamps, hostnames, usernames, output paths, and thread scheduling. Suite IDs use the same sorted, compact JSON convention.

The implementation source commit recorded by the current manifests is `ba31b17...`. Because manifests necessarily enter Git after the code they describe, exact commit matching is the default and a later documentation/manifest commit requires the explicit `--allow-compatible-environment` policy. That flag does not relax input, config, dependency, schema, value, output, or validator checks; it only records the actual commit as a compatible override. Release engineering should regenerate manifests against the final audited implementation boundary.

## Environment and Dependencies

Manifests record compiler, CMake, C++ standard, build type, strict-warning mode, OS, architecture, Python, CLI version, binary SHA-256, locale, timezone, and dependency lock. Validation/report dependencies are pinned in `requirements-validation.txt`; acquisition dependencies are separated in `requirements-acquisition.txt`. `requirements.txt` is the exact union used by CI.

Compiler and standard-library identities are recorded but are not part of deterministic financial semantic identity. Linux/macOS evidence does not establish threshold-stable stochastic equivalence; current Level 3 tolerances are historical compatibility diagnostics and are release-blocked by the [Final Audit](FINAL_AUDIT.md). Locale is forced to `C`, timezone to `UTC`, and Matplotlib uses an isolated cache. Platform-specific figures are generated and validated for presence, not byte identity.

## One-Command Reconstruction

Representative package:

```bash
python3 scripts/reproduce.py \
  --manifest manifests/portfolio_equal_weight.json \
  --output-directory results/reproduced/equal_weight \
  --allow-compatible-environment \
  --json-report results/reproduced/equal_weight-report.json
```

Complete canonical suite:

```bash
python3 scripts/reproduce.py \
  --manifest manifests/canonical_research_suite.json \
  --output-directory results/reproduced/canonical-suite \
  --execution-mode parallel --threads 4 \
  --allow-compatible-environment \
  --json-report results/reproduced/canonical-suite-report.json
```

Use `--verify-only` to verify manifests, repository policy, inputs, configuration, and dependencies without running research. `--no-build` verifies and uses an existing binary. `--keep-failed-output` retains an isolated `.failed` staging directory for diagnosis. Serial mode requires one thread; supported parallel equivalence settings are 2, 4, and 8.

The orchestrator validates the manifest, commit policy, input/config hashes, exact dependency versions, and executable; builds when requested; writes a temporary resolved configuration with only output/execution overrides; executes experiments, validators, figures, and reports; compares the complete declared inventory; writes JSON and Markdown reconstruction reports; and atomically publishes only after success. Failures return nonzero and remove staging output unless explicitly retained. Existing output is moved to a rollback location during publication and restored if replacement fails.

## Canonical Manifests

The tracked set contains a representative single strategy; Equal Weight, Inverse Volatility, and Momentum Top-N portfolios; Equal Weight attribution and statistics views; MA, RSI, MACD, Volatility Breakout, combined, zero-cost combined, and high-cost combined selection-risk packages; a seven-package selection-risk plan; and the complete research-suite plan. Package manifests embed complete resolved configuration snapshots and a compact lineage graph from inputs/configuration/executable through simulation, analytics, validation, and reporting.

## Clean Checkout

```bash
git clone https://github.com/MrithunjoyB/cpp-event-driven-backtester.git
cd cpp-event-driven-backtester
python3 -m pip install -r requirements-validation.txt
python3 scripts/validate_reproducibility.py manifests --verify-inputs
python3 scripts/reproduce.py --manifest manifests/canonical_research_suite.json \
  --output-directory results/reproduced/canonical-suite \
  --allow-compatible-environment
```

No existing build or result directory is required. Generated outputs, caches, failed stages, and machine-specific benchmark artifacts remain ignored.

## Threat Model

| Threat | Classification | Control |
| --- | --- | --- |
| Mutable Yahoo data, changed CSV/config, missing corporate actions | Semantic-result and exact-byte | Tracked inputs, SHA-256/size/schema checks, explicit provenance limitation |
| Compiler, standard library, CMake, Python dependency drift | Environment and possible semantic-result | Recorded environment, exact Python lock, Linux/macOS semantic CI |
| Timestamp, staging path, hostname, commit metadata | Exact-byte | Explicit volatile JSON fields; independent provenance checks |
| Random seed, thread count, unordered completion | Semantic-result | Recorded seed/settings, indexed deterministic collection, cross-thread reconstruction |
| Locale, timezone, line endings | Exact-byte and semantic-result | `C`, UTC, canonical path/text/structured hashing |
| Matplotlib/font/PNG metadata | Presentation-only | Presence/report validation; no cross-platform PNG byte claim |
| Git-ignored outputs, interrupted commands, partial packages | Artifact-retention | Expected inventory, isolated staging, atomic publication, nonzero failure |
| Missing tools or dependency versions | Environment-only | Preflight/build failure and exact dependency comparison |

## Limitations

The manifests cannot repair incomplete upstream corporate-action provenance or grant redistribution rights. Exact compiler-level bootstrap variate portability is not asserted, and the audit found current inferential tolerances insufficient for release-sensitive thresholds. Figure appearance can vary with platform rendering. The compatible-descendant commit policy is explicit but weaker than a manifest regenerated at a final release commit. Both stochastic migration and final-commit manifest regeneration are release blockers.
