# Contributing

Contributions should preserve causal timing, accounting identities, deterministic stochastic methodology, the synthetic public-data boundary, and exact provenance controls.

Before opening a pull request:

```bash
python3 -m pip install --require-hashes -r requirements-validation.lock
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DQUANT_ENABLE_STRICT_WARNINGS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 scripts/validate_public_data_boundary.py
python3 scripts/validate_reproducibility.py manifests --verify-inputs
```

Describe the methodological effect of a change, add deterministic regression coverage, and regenerate canonical manifests only after the implementation/configuration commit is frozen. Do not commit downloaded market data, generated results, credentials, local manifests, build trees, or release archives.

Contributions are submitted under Apache-2.0. Human-directed, AI-assisted patches are acceptable when the submitter has reviewed the work and can explain and maintain it; AI systems are not named as legal authors or copyright holders.
