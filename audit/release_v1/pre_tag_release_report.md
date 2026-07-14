# v1.0.0 Pre-Tag Release Report

**PRE-TAG DECISION: PASS**

The validated release boundary is `0b4f401e8ee7238e43b61e5116d18c332ef5b4ed` (implementation and configuration), followed by `ca172ff9914f52359302a2ba54b22c049d94342a` (mechanically regenerated manifests) and `082cae326607e8f439775e33ac839faeee21c806` (provenance evidence). The commit containing this report is structurally unable to embed its own SHA; it is approved only if the executable provenance closure accepts it as an `audit/release_v1/`-only descendant and all exact-SHA remote workflows pass on that commit before tagging.

## Validation Summary

- Clean warnings-as-errors Release builds passed locally with AppleClang and remotely with GCC/Clang.
- All 30 CTest targets passed in Release, ASan, UBSan, and TSan builds.
- Stable RNG and Python reference vectors passed with stochastic methodology version 2 and `portable_bounded_v1` unchanged.
- Representative, complete 13-package, and detached clean-worktree reconstructions passed.
- Seven selection-risk packages were byte-identical across serial/1, 2, 4, and 8 workers (28 comparisons).
- Regression snapshots matched 8/8 scenarios.
- Result, attribution, statistics, selection-risk, reproducibility, manifest, provenance, and public-boundary validators passed, including corruption rejection tests.
- Both hash-locked Python environments passed dependency checks; the reviewed inventory covers 34 distributions.
- SPDX 2.3 validation passed for 35 packages, including independent `spdx-tools` parsing.
- Repository and archive scans found no forbidden provider data, sampled rows, secrets, private keys, credentials, or developer-local paths.

## Remote Preflight

All runs below used head SHA `082cae326607e8f439775e33ac839faeee21c806`:

| Gate | Run | Result |
| --- | --- | --- |
| Linux/macOS Release, ASan, UBSan, TSan | `29342651012` | Passed |
| Complete 13-package reconstruction | `29342713022` | Passed |
| Linux/macOS packaging and aggregate validation | `29342722745` | Passed |

The aggregate preflight contained 11 checksummed assets. Linux x86_64 and macOS arm64 target-runner smoke tests each produced and validated seven genuine CSV outputs. The reproducibility bundle contained 226 tracked files, and the SBOM contained 35 packages.

## Findings

No Critical, High, Medium, or Low release finding remains open. Two informational limitations are accepted: operating-system toolchains are recorded rather than hermetically vendored, and removed third-party data remains in historical Git objects while being excluded and actively rejected from the current tree and release assets.

The machine-readable report records all 40 mandatory gates. The exact commit containing these reports must receive fresh CMake CI, complete reconstruction, and release-candidate workflow passes before the annotated tag is created.
