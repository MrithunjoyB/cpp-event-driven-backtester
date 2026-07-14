# v1.0.0 Post-Release Verification

Post-release verification passed for the public [v1.0.0 release](https://github.com/MrithunjoyB/cpp-event-driven-backtester/releases/tag/v1.0.0), published at `2026-07-14T15:15:33Z`.

## Identity

- Annotated tag object: `20ac53c5e4b61ae7b431d5bb263f246e35f8d2a2`
- Tag target: `2f86b71dbc9f29dbda861942d8afbb10c04b6625`
- Implementation/configuration boundary: `0b4f401e8ee7238e43b61e5116d18c332ef5b4ed`
- Manifest commit: `ca172ff9914f52359302a2ba54b22c049d94342a`
- Stochastic methodology: version 2, `portable_bounded_v1`

Local and remote tag objects and peeled targets agree. The tag is annotated and retains the message `cpp-event-driven-backtester v1.0.0`.

## Public Assets

All 12 published assets downloaded successfully. `SHA256SUMS` verified all 11 other files. The release validator accepted the Linux x86_64, macOS arm64, and 228-file reproducibility archives; the SPDX 2.3 SBOM contains 35 packages and parsed independently.

The downloaded macOS archive passed a fresh smoke test and generated seven validated CSV files. The published Linux archive is byte-identical to the exact-candidate artifact smoke-tested on the Linux x86_64 target runner, whose report also records seven validated CSV files.

## Tag Reconstruction

A new detached checkout of `v1.0.0` produced a clean strict Release build and passed all 30 CTest targets. The complete 13-package canonical suite reconstructed successfully into a new output tree, and the reconstruction report, manifest inputs, provenance closure, public-data boundary, 48 local documentation links, and `CITATION.cff` metadata all validated.

No provider data, sampled forbidden row, secret, credential, private key, unsafe archive member, or developer-local path was found in the tag or downloaded assets. The GitHub Release is public, non-draft, non-prerelease, and contains the complete validated inventory.

## Immutability

No tag or asset was moved, replaced, or mutated during verification. These reports are audit-only descendants on `main`; the immutable `v1.0.0` tag remains fixed at `2f86b71dbc9f29dbda861942d8afbb10c04b6625`.

No post-release defect was found.
