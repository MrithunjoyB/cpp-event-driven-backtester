# Git History Data Report

## Scope

Baseline `d806943b5bb6d433e4a56e042fd16acdf5ee726d` was compared with the public-data migration tree. GitHub was inspected on 2026-07-13. The repository was public, had one branch (`main`), no tags, no GitHub Releases, and no detectable forks. Fork and clone visibility is limited to information exposed by GitHub; prior private clones or downloaded archives cannot be detected.

## Occurrences

All five files first appeared in `2243b3837a2f341b0d87b943c94241be968caaa1` and remained present through `d806943...`:

- `data/AAPL.csv`
- `data/MSFT.csv`
- `data/SPY.csv`
- `data/TSLA.csv`
- `data/BTC-USD.csv`

Their hashes and blob IDs are recorded in `history_occurrences.csv`. Historical GitHub commit pages and source archives for affected commits can expose those blobs. No tag or GitHub Release asset was found, so no published release asset requires deletion.

## Current Boundary

The files are absent from the migration tree and from all new public manifests. A future `v1.0.0` tag targeting a post-migration commit will therefore exclude them from that tag's tree and source archives. Current-tree removal does not erase earlier commits, existing clones, or archives downloaded before migration.

History remediation classification: **not required for the v1.0.0 release boundary**. Rewriting public history may reduce ordinary discoverability but cannot guarantee removal from clones, caches, or forks. It would invalidate commit identities, require a coordinated force-push, disrupt collaborators, and require a separate legal and owner decision. No rewrite, force-push, tag deletion, or remote-ref deletion occurred in this stage.

This is a technical exposure assessment, not a legal guarantee.
