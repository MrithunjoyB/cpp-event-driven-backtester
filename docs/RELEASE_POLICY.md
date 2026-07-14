# Release and Immutability Policy

An annotated semantic-version tag identifies one exact reviewed commit. A published tag is never moved, and published assets are never silently replaced. Corrective work uses a new patch release; compatible feature work uses a new minor release; methodology-breaking changes require an appropriate major-version decision.

Release manifests bind an implementation/configuration commit. Mechanical manifest regeneration follows that boundary, and release evidence may follow the manifest commit. `scripts/validate_release_provenance.py` rejects any runtime, configuration, workflow, dependency, or methodology change after manifest capture; only manifest files may differ in the manifest commit, and only `audit/release_v1/` evidence may differ afterward.

Platform binary archives are published only after build, test, extraction, version, help, configuration, representative synthetic experiment, result-validation, data-boundary, secret/path-scan, SBOM, and checksum gates pass on the named target runner. Release source and reproducibility archives contain project-owned synthetic fixtures, not third-party market data.

The GitHub Release notes and checksums are part of the release record. Any discovered post-release defect is documented and corrected through a new version rather than mutable replacement.
