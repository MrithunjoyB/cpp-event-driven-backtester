#!/usr/bin/env python3
"""Validate exact release candidates or an already published annotated release tag."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SHA1 = re.compile(r"^[0-9a-f]{40}$")
RELEASE_NAME = re.compile(r"^v[0-9]+\.[0-9]+\.[0-9]+$")
MANIFEST_PREFIX = "manifests/"
EVIDENCE_PREFIX = "audit/release_v1/"
DEFAULT_RECORD = Path("audit/release_v1/provenance.json")
DEFAULT_RELEASE_IDENTITY = Path("audit/release_v1/post_release_verification.json")


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
        ["git", "merge-base", "--is-ancestor", ancestor, descendant],
        cwd=root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if result.returncode != 0:
        raise ProvenanceError(f"{label} is not an ancestor")


def changed_paths(root: Path, start: str, end: str) -> list[str]:
    """Return every path touched in the ancestry range, including later-reverted paths."""
    value = git(root, "log", "--format=", "--name-only", "--no-renames", f"{start}..{end}")
    return sorted({line for line in value.splitlines() if line})


def require_prefixes(paths: list[str], prefixes: tuple[str, ...], label: str) -> None:
    invalid = [path for path in paths if not path.startswith(prefixes)]
    if invalid:
        raise ProvenanceError(
            f"{label} contains disallowed paths: {', '.join(sorted(invalid))}"
        )


def validate_path_closure(
    root: Path, implementation_commit: str, manifest_commit: str, candidate_commit: str
) -> dict[str, Any]:
    """Validate the strict implementation -> manifest -> exact candidate closure."""
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


def _read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ProvenanceError(f"cannot read {label}: {error}") from error
    if not isinstance(value, dict):
        raise ProvenanceError(f"{label} is not a JSON object")
    return value


def _validate_record_fields(record: dict[str, Any]) -> None:
    required = {
        "schema_version",
        "release",
        "implementation_commit",
        "manifest_commit",
        "stochastic_methodology_version",
        "rng_mapping",
        "financial_methodology_changed",
    }
    if set(record) != required or record.get("schema_version") != 1:
        raise ProvenanceError("release provenance fields are incomplete or unknown")
    if not isinstance(record.get("release"), str) or not RELEASE_NAME.fullmatch(record["release"]):
        raise ProvenanceError("release provenance name is malformed")
    if record["release"] != "v1.0.0":
        raise ProvenanceError("release provenance does not identify v1.0.0")
    if record["stochastic_methodology_version"] != 2:
        raise ProvenanceError("stochastic methodology version changed")
    if record["rng_mapping"] != "portable_bounded_v1":
        raise ProvenanceError("RNG mapping changed")
    if record["financial_methodology_changed"] is not False:
        raise ProvenanceError("release record claims a financial methodology change")


def _record_at_commit(root: Path, commit: str, record_path: Path) -> dict[str, Any]:
    try:
        relative = record_path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise ProvenanceError("provenance record must be inside the repository") from error
    try:
        value = json.loads(git(root, "show", f"{commit}:{relative}"))
    except json.JSONDecodeError as error:
        raise ProvenanceError("tagged provenance record is malformed") from error
    if not isinstance(value, dict):
        raise ProvenanceError("tagged provenance record is not a JSON object")
    return value


def _validate_manifest_sources(
    root: Path, implementation_commit: str, candidate_commit: str
) -> tuple[int, int]:
    paths = [
        path
        for path in git(root, "ls-tree", "-r", "--name-only", candidate_commit, "--", "manifests").splitlines()
        if Path(path).parent.as_posix() == "manifests" and path.endswith(".json")
    ]
    packages = 0
    suites = 0
    for path in sorted(paths):
        try:
            value = json.loads(git(root, "show", f"{candidate_commit}:{path}"))
        except json.JSONDecodeError as error:
            raise ProvenanceError(f"malformed manifest at {candidate_commit}:{path}") from error
        if value.get("source_commit") != implementation_commit:
            raise ProvenanceError(f"stale source_commit in {path}")
        if value.get("package_type") == "suite":
            suites += 1
        else:
            packages += 1
    if (packages, suites) != (13, 2):
        raise ProvenanceError(
            f"expected 13 package manifests and 2 suite plans, found {packages} and {suites}"
        )
    return packages, suites


def validate_record(
    root: Path, record_path: Path, candidate_commit: str | None = None
) -> dict[str, Any]:
    """Validate one explicitly supplied exact release-candidate commit."""
    if candidate_commit is None:
        raise ProvenanceError("strict release-candidate validation requires an explicit candidate SHA")
    record = _read_json(record_path, "provenance record")
    _validate_record_fields(record)
    result = validate_path_closure(
        root, record["implementation_commit"], record["manifest_commit"], candidate_commit
    )
    packages, suites = _validate_manifest_sources(
        root, record["implementation_commit"], candidate_commit
    )
    result.update(
        {
            "release": record["release"],
            "package_manifests": packages,
            "suite_plans": suites,
            "validation_mode": "strict_release_candidate",
        }
    )
    return result


def _expected_tag_identity(identity_path: Path, release: str) -> tuple[str, str]:
    identity = _read_json(identity_path, "post-release identity record")
    tag = identity.get("tag")
    if identity.get("release") != release or not isinstance(tag, dict) or tag.get("name") != release:
        raise ProvenanceError("post-release identity does not match the provenance release")
    object_sha = tag.get("object_sha")
    target_sha = tag.get("target_sha")
    if not isinstance(object_sha, str) or not SHA1.fullmatch(object_sha):
        raise ProvenanceError("recorded tag object is not a full SHA")
    if not isinstance(target_sha, str) or not SHA1.fullmatch(target_sha):
        raise ProvenanceError("recorded peeled target is not a full SHA")
    return object_sha, target_sha


def validate_tagged_release(
    root: Path,
    record_path: Path | None = None,
    identity_path: Path | None = None,
) -> dict[str, Any]:
    """Validate the published release through its recorded annotated tag identity."""
    record_path = record_path or root / DEFAULT_RECORD
    identity_path = identity_path or root / DEFAULT_RELEASE_IDENTITY
    record = _read_json(record_path, "provenance record")
    _validate_record_fields(record)
    release = record["release"]
    tag_ref = f"refs/tags/{release}"
    exists = subprocess.run(
        ["git", "show-ref", "--verify", "--quiet", tag_ref], cwd=root
    )
    if exists.returncode != 0:
        raise ProvenanceError(f"release tag is missing: {release}")
    tag_object = git(root, "rev-parse", tag_ref)
    if git(root, "cat-file", "-t", tag_object) != "tag":
        raise ProvenanceError(f"release tag is lightweight, not annotated: {release}")
    try:
        tag_target = git(root, "rev-parse", f"{tag_ref}^{{commit}}")
    except ProvenanceError as error:
        raise ProvenanceError(f"release tag does not peel to a commit: {release}") from error
    expected_object, expected_target = _expected_tag_identity(identity_path, release)
    if tag_object != expected_object:
        raise ProvenanceError(
            f"release tag object moved: expected {expected_object}, found {tag_object}"
        )
    if tag_target != expected_target:
        raise ProvenanceError(
            f"release tag target moved: expected {expected_target}, found {tag_target}"
        )
    tagged_record = _record_at_commit(root, tag_target, record_path)
    _validate_record_fields(tagged_record)
    if tagged_record != record:
        raise ProvenanceError("current provenance record differs from the immutable tagged record")
    result = validate_path_closure(
        root,
        tagged_record["implementation_commit"],
        tagged_record["manifest_commit"],
        tag_target,
    )
    packages, suites = _validate_manifest_sources(
        root, tagged_record["implementation_commit"], tag_target
    )
    result.update(
        {
            "release": release,
            "release_candidate_commit": tag_target,
            "tag_name": release,
            "tag_object": tag_object,
            "tag_target": tag_target,
            "package_manifests": packages,
            "suite_plans": suites,
            "current_commit": git(root, "rev-parse", "HEAD"),
            "validation_mode": "immutable_tagged_release",
        }
    )
    return result


def validate_compatible_source(
    root: Path, source_commit: str, current_commit: str
) -> dict[str, Any]:
    """Validate a current descendant without redefining it as the release candidate."""
    result = validate_tagged_release(root)
    if result["implementation_commit"] != source_commit:
        raise ProvenanceError("manifest source does not match the release implementation boundary")
    _require_commit(root, current_commit, "current descendant")
    _require_ancestor(root, result["tag_target"], current_commit, "tagged release")
    result["current_descendant"] = current_commit
    result["validation_mode"] = "compatible_current_descendant"
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--record", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--candidate", help="exact release-candidate commit SHA")
    mode.add_argument(
        "--tagged-release",
        action="store_true",
        help="validate the immutable annotated release tag (default)",
    )
    args = parser.parse_args()
    root = args.root.resolve()
    record = args.record or root / DEFAULT_RECORD
    try:
        if args.candidate:
            result = validate_record(root, record, args.candidate)
            print(
                "Strict release-candidate provenance validation passed: "
                f"{result['package_manifests']} packages, {result['suite_plans']} suites, "
                f"candidate {result['candidate_commit']}"
            )
        else:
            result = validate_tagged_release(root, record)
            print(
                "Immutable tagged-release provenance validation passed: "
                f"{result['tag_name']} tag object {result['tag_object']}, "
                f"peeled target {result['tag_target']}, current HEAD {result['current_commit']}"
            )
    except (ProvenanceError, json.JSONDecodeError) as error:
        print(f"Release provenance validation failed: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
