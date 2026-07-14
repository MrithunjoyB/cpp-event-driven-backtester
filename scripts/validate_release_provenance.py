#!/usr/bin/env python3
"""Validate the bounded implementation -> manifest -> evidence release ancestry."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHA1 = re.compile(r"^[0-9a-f]{40}$")
MANIFEST_PREFIX = "manifests/"
EVIDENCE_PREFIX = "audit/release_v1/"


class ProvenanceError(RuntimeError):
    pass


def git(root: Path, *args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args], cwd=root, text=True, stderr=subprocess.STDOUT
        ).strip()
    except subprocess.CalledProcessError as error:
        raise ProvenanceError(error.output.strip() or "Git provenance check failed") from error


def _require_commit(root: Path, value: str, label: str) -> None:
    if not SHA1.fullmatch(value):
        raise ProvenanceError(f"{label} is not a full commit SHA")
    git(root, "cat-file", "-e", f"{value}^{{commit}}")


def _require_ancestor(root: Path, ancestor: str, descendant: str, label: str) -> None:
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", ancestor, descendant], cwd=root
    )
    if result.returncode != 0:
        raise ProvenanceError(f"{label} is not an ancestor")


def changed_paths(root: Path, start: str, end: str) -> list[str]:
    value = git(root, "diff", "--name-only", f"{start}..{end}")
    return [line for line in value.splitlines() if line]


def require_prefixes(paths: list[str], prefixes: tuple[str, ...], label: str) -> None:
    invalid = [path for path in paths if not path.startswith(prefixes)]
    if invalid:
        raise ProvenanceError(
            f"{label} contains disallowed paths: {', '.join(sorted(invalid))}"
        )


def validate_path_closure(
    root: Path, implementation_commit: str, manifest_commit: str, candidate_commit: str
) -> dict:
    for value, label in (
        (implementation_commit, "implementation_commit"),
        (manifest_commit, "manifest_commit"),
        (candidate_commit, "candidate_commit"),
    ):
        _require_commit(root, value, label)
    _require_ancestor(root, implementation_commit, manifest_commit, "implementation commit")
    _require_ancestor(root, manifest_commit, candidate_commit, "manifest commit")
    manifest_paths = changed_paths(root, implementation_commit, manifest_commit)
    evidence_paths = changed_paths(root, manifest_commit, candidate_commit)
    if not manifest_paths:
        raise ProvenanceError("manifest commit contains no changes")
    require_prefixes(manifest_paths, (MANIFEST_PREFIX,), "manifest descendant")
    require_prefixes(evidence_paths, (EVIDENCE_PREFIX,), "evidence descendant")
    return {
        "implementation_commit": implementation_commit,
        "manifest_commit": manifest_commit,
        "candidate_commit": candidate_commit,
        "manifest_paths": manifest_paths,
        "evidence_paths": evidence_paths,
    }


def _validate_manifest_sources(root: Path, implementation_commit: str) -> tuple[int, int]:
    packages = 0
    suites = 0
    for path in sorted((root / "manifests").glob("*.json")):
        value = json.loads(path.read_text())
        if value.get("source_commit") != implementation_commit:
            raise ProvenanceError(f"stale source_commit in {path.relative_to(root)}")
        if value.get("package_type") == "suite":
            suites += 1
        else:
            packages += 1
    if (packages, suites) != (13, 2):
        raise ProvenanceError(
            f"expected 13 package manifests and 2 suite plans, found {packages} and {suites}"
        )
    return packages, suites


def validate_record(root: Path, record_path: Path, candidate_commit: str | None = None) -> dict:
    try:
        record = json.loads(record_path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ProvenanceError(f"cannot read provenance record: {error}") from error
    required = {
        "schema_version", "release", "implementation_commit", "manifest_commit",
        "stochastic_methodology_version", "rng_mapping", "financial_methodology_changed",
    }
    if set(record) != required or record["schema_version"] != 1 or record["release"] != "v1.0.0":
        raise ProvenanceError("release provenance fields are incomplete or unknown")
    if record["stochastic_methodology_version"] != 2:
        raise ProvenanceError("stochastic methodology version changed")
    if record["rng_mapping"] != "portable_bounded_v1":
        raise ProvenanceError("RNG mapping changed")
    if record["financial_methodology_changed"] is not False:
        raise ProvenanceError("release record claims a financial methodology change")
    candidate = candidate_commit or git(root, "rev-parse", "HEAD")
    result = validate_path_closure(
        root, record["implementation_commit"], record["manifest_commit"], candidate
    )
    packages, suites = _validate_manifest_sources(root, record["implementation_commit"])
    result.update({"package_manifests": packages, "suite_plans": suites})
    return result


def validate_compatible_source(root: Path, source_commit: str, candidate_commit: str) -> dict:
    record_path = root / "audit/release_v1/provenance.json"
    result = validate_record(root, record_path, candidate_commit)
    if result["implementation_commit"] != source_commit:
        raise ProvenanceError("manifest source does not match the release implementation boundary")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--record", type=Path)
    parser.add_argument("--candidate")
    args = parser.parse_args()
    root = args.root.resolve()
    record = args.record or root / "audit/release_v1/provenance.json"
    try:
        result = validate_record(root, record, args.candidate)
    except (ProvenanceError, json.JSONDecodeError) as error:
        print(f"Release provenance validation failed: {error}")
        return 1
    print(
        "Release provenance validation passed: "
        f"{result['package_manifests']} packages, {result['suite_plans']} suites, "
        f"candidate {result['candidate_commit']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
