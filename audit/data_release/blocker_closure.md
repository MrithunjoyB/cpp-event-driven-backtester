# Dataset Redistribution Blocker Closure

## Blocker

Five tracked Yahoo-derived CSVs lacked verified public redistribution permission. Apache-2.0 project licensing could not grant rights in those third-party inputs.

## Remediation

- Removed all five CSVs from the current tree.
- Removed 289 tracked derived result/figure artifacts and 15 provider-dependent root manifests.
- Added five independently generated synthetic assets with deterministic offline provenance.
- Added 13 synthetic package manifests and two suite plans.
- Added user-supplied local data validation and hash manifests.
- Added exact-hash, sampled-row, manifest, archive, and tracked-tree boundary controls.
- Preserved historical findings only as qualified non-canonical audit context.
- Reported historical Git exposure without rewriting history.

## Objective Closure Test

```bash
python3 scripts/generate_synthetic_market_data.py
python3 scripts/validate_synthetic_market_data.py data/synthetic --regenerate-check
python3 scripts/validate_public_data_boundary.py
python3 scripts/validate_reproducibility.py manifests --verify-inputs
python3 scripts/reproduce.py --manifest manifests/public_reproducibility_suite.json \
  --output-directory results/reproduced/public-synthetic-suite \
  --allow-compatible-environment
```

The data-redistribution blocker is closed for handoff to final release engineering. Candidate `cc218d14e5fcc5a38e787b034496df9fd6f47a67` passed the full local gate, exact-SHA Linux/macOS Release CI and Linux sanitizer CI in run `29332687893`, and the complete 13-package reconstruction in run `29332736581`. This stage does not create `v1.0.0` and does not assert historical erasure or legal guarantees.
