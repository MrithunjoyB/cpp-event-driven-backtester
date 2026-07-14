# Reproducibility

## Policy

Public canonical reconstruction uses the five project-owned files under `data/synthetic/` after byte-for-byte offline regeneration and exact SHA-256 verification. Each package also hashes `metadata.json`, which records generator identity/version, seed, schema, calendars, missing dates, corporate actions, row counts, and redistribution classification. `download_data.py` is optional local acquisition and is never a public canonical input.

The manifest schema is versioned at `manifests/schema/reproducibility-manifest-v1.schema.json`. Runtime validation is deliberately stricter than the descriptive JSON Schema: unknown root fields, unsupported versions, invalid identities, duplicate inputs/artifacts, unsafe paths, missing lineage, undeclared validators, nonzero tolerances, and malformed suites fail before execution.

## Reproducibility Levels

| Level | Contract |
| --- | --- |
| Exact byte | Identical verified inputs and byte-stable artifact SHA-256 values. Used only when serialization and metadata are stable. |
| Canonical semantic | Identical schemas, row order, values, identities, selections, trades, accounting, statistics, seeds, warnings, and methodology metadata after documented volatile JSON fields are excluded. No financial value is rounded. |
| Methodological | Same verified inputs, configuration, and methodology with explicit field-level tolerances or a declared shape-only policy for raw stochastic paths. |

PNG/SVG/PDF figures are presentation-only because renderer, font, compression, and metadata differences are platform-sensitive. `performance_counters.csv` and `parallel_execution_metadata.json` are environment-only. Known volatile JSON fields include timestamps, staging output paths, host/user fields, elapsed time, and compiled commit metadata; the manifest independently verifies source provenance.

Stochastic methodology version 2 uses `mt19937` with the repository-owned `portable_bounded_v1` mapping. Raw retained paths, bootstrap distributions, summaries, probabilities, max-statistic draws, and adjusted p-values are canonical semantic artifacts; the old shape-only and broad numerical tolerance policies are retired. Presentation files remain presence-validated and the non-inferential rank-correlation diagnostic retains a `1e-15` floating-point tolerance.

Release manifests identify the exact committed implementation/configuration boundary. Because generated manifests necessarily enter Git afterward, `audit/release_v1/provenance.json` separately identifies the manifest commit. Reconstruction on the final release descendant requires `--allow-compatible-environment`, which now invokes `scripts/validate_release_provenance.py`: the implementation-to-manifest diff may contain only `manifests/`, and the manifest-to-candidate diff may contain only `audit/release_v1/`. Arbitrary descendants are rejected. This avoids the impossible claim that a committed manifest self-references its own commit SHA. Legacy provider-data manifests remain recoverable from Git history but are not valid public inputs.

One compiler-sensitive diagnostic field, `is_oos_spearman_rank_correlation`, permits `1e-15` absolute variation; observed GCC/AppleClang variation is approximately `1.83e-17`. Candidate rank ordering and every selection field remain exact.

CSV semantic hashing parses the exact cell strings and preserves header and row order. JSON hashing sorts keys recursively, normalizes path separators, and removes only declared volatile fields. Markdown/text hashing normalizes line endings. No tolerance or numerical rounding is applied. Every output records its type, schema, size, row count where applicable, byte hash where stable, semantic hash, required status, validator, and parent lineage.

## Manifest Identity

`manifest_id` is SHA-256 over canonical JSON containing the schema version, experiment/package identity, implementation source commit, ordered logical input hashes, configuration hash, methodology version, seed, and execution policy. It excludes timestamps, hostnames, usernames, output paths, and thread scheduling. Suite IDs use the same sorted, compact JSON convention.

The current manifests identify the committed deterministic-arithmetic implementation boundary. Reconstruction either targets that implementation commit or accepts the bounded manifest/evidence descendant only after executable provenance closure.

## Environment and Dependencies

Manifests record compiler, CMake, C++ standard, build type, strict-warning mode, OS, architecture, Python, CLI version, binary SHA-256, locale, timezone, and dependency lock. Validation/report dependencies are hash-locked in `requirements-validation.lock` and used by public CI; optional acquisition dependencies are separately hash-locked in `requirements-acquisition.lock`. The `.txt` files remain direct-input specifications, and `requirements.txt` remains a convenience union. Hash locking constrains Python distributions but does not make operating-system compilers and libraries hermetic.

Compiler and standard-library identities are recorded but are not part of semantic identity. Linux and macOS execute the same repository-owned integer mapping and reference vectors. Locale is forced to `C`, timezone to `UTC`, and Matplotlib uses an isolated cache. Platform-specific figures are generated and validated for presence, not byte identity.

## One-Command Reconstruction

Representative public synthetic package:

```bash
python3 scripts/reproduce.py \
  --manifest manifests/public_synthetic_portfolio_equal_weight.json \
  --output-directory results/reproduced/public-synthetic-equal-weight \
  --allow-compatible-environment \
  --json-report results/reproduced/equal_weight-report.json
```

Complete canonical suite:

```bash
python3 scripts/reproduce.py \
  --manifest manifests/public_reproducibility_suite.json \
  --output-directory results/reproduced/public-synthetic-suite \
  --execution-mode parallel --threads 4 \
  --allow-compatible-environment \
  --json-report results/reproduced/canonical-suite-report.json
```

Use `--verify-only` to verify manifests, repository policy, inputs, configuration, and dependencies without running research. `--no-build` verifies and uses an existing binary. `--keep-failed-output` retains an isolated `.failed` staging directory for diagnosis. Serial mode requires one thread; supported parallel equivalence settings are 2, 4, and 8.

The orchestrator validates the manifest, commit policy, input/config hashes, exact dependency versions, and executable; builds when requested; writes a temporary resolved configuration with only output/execution overrides; executes experiments, validators, figures, and reports; compares the complete declared inventory; writes JSON and Markdown reconstruction reports; and atomically publishes only after success. Failures return nonzero and remove staging output unless explicitly retained. Existing output is moved to a rollback location during publication and restored if replacement fails.

## Canonical Manifests

The tracked public set contains 13 synthetic packages: a representative single strategy; Equal Weight, Inverse Volatility, and Momentum Top-N portfolios; Equal Weight attribution and statistics views; and MA, RSI, MACD, Volatility Breakout, combined, zero-cost combined, and high-cost combined selection-risk packages. Two suite plans orchestrate selection-risk and complete public reconstruction. Package manifests classify every input as synthetic and embed generator identity, seed, resolved configuration, validators, output inventory, and lineage.

## Clean Checkout

```bash
git clone https://github.com/MrithunjoyB/cpp-event-driven-backtester.git
cd cpp-event-driven-backtester
python3 -m pip install --require-hashes -r requirements-validation.lock
python3 scripts/generate_synthetic_market_data.py
python3 scripts/validate_synthetic_market_data.py --regenerate-check
python3 scripts/validate_public_data_boundary.py
python3 scripts/validate_reproducibility.py manifests --verify-inputs
python3 scripts/reproduce.py --manifest manifests/public_reproducibility_suite.json \
  --output-directory results/reproduced/public-synthetic-suite \
  --allow-compatible-environment
```

No existing build or result directory is required. Generated outputs, caches, failed stages, release archives, and machine-specific benchmark artifacts remain ignored.

## Threat Model

| Threat | Classification | Control |
| --- | --- | --- |
| Fixture drift, copied provider rows, changed CSV/config | Semantic-result and release boundary | Offline regeneration, SHA-256/size/schema checks, forbidden hashes/row fingerprints, tracked-tree gate |
| User/local provider data leaking into a public package | Legal/provenance boundary | Ignored local directory, manifest classification, public-boundary validator |
| Compiler, standard library, CMake, Python dependency drift | Environment and possible semantic-result | Recorded environment, exact Python lock, Linux/macOS semantic CI |
| Timestamp, staging path, hostname, commit metadata | Exact-byte | Explicit volatile JSON fields; independent provenance checks |
| Random seed, thread count, unordered completion | Semantic-result | Recorded seed/settings, indexed deterministic collection, cross-thread reconstruction |
| Locale, timezone, line endings | Exact-byte and semantic-result | `C`, UTC, canonical path/text/structured hashing |
| Matplotlib/font/PNG metadata | Presentation-only | Presence/report validation; no cross-platform PNG byte claim |
| Git-ignored outputs, interrupted commands, partial packages | Artifact-retention | Expected inventory, isolated staging, atomic publication, nonzero failure |
| Missing tools or dependency versions | Environment-only | Preflight/build failure and exact dependency comparison |

## Limitations

Synthetic fixtures cannot support empirical market conclusions. Local user data may have incomplete corporate-action provenance, and the project cannot grant provider rights. Historical commits still contain removed provider files. Figure appearance can vary with platform rendering. Exact semantic reconstruction depends on the recorded implementation boundary, synthetic input hashes, configuration, and supported toolchain; it is not a claim of arbitrary future compiler equivalence.
