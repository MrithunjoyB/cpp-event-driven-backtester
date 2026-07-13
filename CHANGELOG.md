# Changelog

This project follows Keep a Changelog conventions. The software release version remains unchanged until final release engineering approves and publishes `v1.0.0`.

## [Unreleased]

### Added

- Offline deterministic five-asset synthetic fixture generator and validator.
- Provider-neutral user-supplied market-data validation and local hash manifests.
- Public data-boundary validation with removed-file hashes and sampled-row fingerprints.
- Public synthetic canonical suite with 13 packages and two suite plans.
- Data lineage, Git-history exposure, provenance, input, and blocker-closure documentation.

### Changed

- Public configurations and defaults now use `SYN_EQ_A`, `SYN_EQ_B`, `SYN_EQ_C`, `SYN_BENCH`, and `SYN_CRYPTO` under `data/synthetic/`.
- Export precision increased to preserve strict cross-language statistical identities.
- Historical empirical findings are classified as local, non-release-canonical evidence.

### Removed

- Five Yahoo-derived CSVs from the current tree.
- 289 tracked generated result and figure artifacts derived from the former public inputs.
- 15 provider-dependent public manifests.

### Security and Provenance

- Local data and generated results are ignored and rejected from public manifests.
- No history rewrite or force-push was performed; historical blob exposure is documented.

### Known Limitations

- Historical commits still contain the removed provider files.
- Synthetic performance is not empirical market evidence.
