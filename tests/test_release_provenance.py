#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from reconstruct_tagged_release import (  # noqa: E402
    detached_release_worktree,
    reconstruction_commands,
)
from validate_release_provenance import (  # noqa: E402
    ProvenanceError,
    validate_compatible_source,
    validate_path_closure,
    validate_record,
    validate_tagged_release,
)


cases = 0


def check(condition: bool, label: str) -> None:
    global cases
    cases += 1
    if not condition:
        raise AssertionError(label)


def reject(function, label: str) -> None:
    global cases
    cases += 1
    try:
        function()
    except ProvenanceError:
        return
    raise AssertionError(f"accepted invalid provenance: {label}")


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=root, text=True).strip()


def commit(root: Path, message: str) -> str:
    subprocess.run(["git", "add", "."], cwd=root, check=True)
    subprocess.run(["git", "commit", "-qm", message], cwd=root, check=True)
    return git(root, "rev-parse", "HEAD")


def commit_path_from(
    root: Path, parent: str, path: str, contents: str, message: str
) -> str:
    subprocess.run(["git", "switch", "--detach", "-q", parent], cwd=root, check=True)
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(contents)
    return commit(root, message)


with tempfile.TemporaryDirectory(prefix="release-provenance-test-") as temporary:
    base = Path(temporary)
    root = base / "repository"
    root.mkdir()
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)
    subprocess.run(
        ["git", "config", "user.email", "fixture@example.invalid"], cwd=root, check=True
    )
    subprocess.run(["git", "config", "user.name", "Fixture"], cwd=root, check=True)

    (root / "runtime.cpp").write_text("int value = 1;\n")
    implementation = commit(root, "implementation")

    manifests = root / "manifests"
    manifests.mkdir()
    for index in range(13):
        (manifests / f"package_{index:02d}.json").write_text(
            json.dumps(
                {
                    "source_commit": implementation,
                    "package_type": "package",
                }
            )
            + "\n"
        )
    for index in range(2):
        (manifests / f"suite_{index:02d}.json").write_text(
            json.dumps(
                {
                    "source_commit": implementation,
                    "package_type": "suite",
                }
            )
            + "\n"
        )
    manifest = commit(root, "manifests")

    audit = root / "audit/release_v1"
    audit.mkdir(parents=True)
    record = {
        "schema_version": 1,
        "release": "v1.0.0",
        "implementation_commit": implementation,
        "manifest_commit": manifest,
        "stochastic_methodology_version": 2,
        "rng_mapping": "portable_bounded_v1",
        "financial_methodology_changed": False,
    }
    record_path = audit / "provenance.json"
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    (audit / "evidence.json").write_text("{}\n")
    candidate = commit(root, "evidence-only release candidate")
    subprocess.run(
        ["git", "tag", "-a", "v1.0.0", "-m", "fixture v1.0.0", candidate],
        cwd=root,
        check=True,
    )
    tag_object = git(root, "rev-parse", "refs/tags/v1.0.0")
    tag_target = git(root, "rev-parse", "refs/tags/v1.0.0^{}")
    identity_path = audit / "post_release_verification.json"
    identity_path.write_text(
        json.dumps(
            {
                "release": "v1.0.0",
                "tag": {
                    "name": "v1.0.0",
                    "object_sha": tag_object,
                    "target_sha": tag_target,
                },
            },
            indent=2,
            sort_keys=True,
        )
        + "\n"
    )
    post_release = commit(root, "record immutable tag identity")
    (root / "README.md").write_text("documentation after the immutable release\n")
    documentation_descendant = commit(root, "post-release documentation")

    closure = validate_path_closure(root, implementation, manifest, candidate)
    check(
        closure["evidence_paths"]
        == ["audit/release_v1/evidence.json", "audit/release_v1/provenance.json"],
        "valid evidence-only candidate closure",
    )
    exact = validate_record(root, record_path, candidate)
    check(
        exact["validation_mode"] == "strict_release_candidate"
        and exact["candidate_commit"] == candidate,
        "explicit strict candidate identity",
    )
    reject(
        lambda: validate_record(root, record_path),
        "implicit HEAD as a strict release candidate",
    )

    invalid_readme = commit_path_from(
        root, candidate, "README.md", "late docs\n", "invalid readme candidate"
    )
    invalid_source = commit_path_from(
        root, candidate, "runtime.cpp", "int value = 2;\n", "invalid source candidate"
    )
    invalid_config = commit_path_from(
        root, candidate, "configs/runtime.json", "{}\n", "invalid config candidate"
    )
    invalid_workflow = commit_path_from(
        root,
        candidate,
        ".github/workflows/late.yml",
        "name: late\n",
        "invalid workflow candidate",
    )
    subprocess.run(
        ["git", "switch", "--detach", "-q", documentation_descendant],
        cwd=root,
        check=True,
    )
    reject(
        lambda: validate_record(root, record_path, invalid_readme),
        "README after manifest capture",
    )
    reject(
        lambda: validate_record(root, record_path, invalid_source),
        "C++ source after manifest capture",
    )
    reject(
        lambda: validate_record(root, record_path, invalid_config),
        "configuration after manifest capture",
    )
    reject(
        lambda: validate_record(root, record_path, invalid_workflow),
        "workflow after manifest capture",
    )
    reject(
        lambda: validate_path_closure(root, manifest, implementation, candidate),
        "reversed ancestry",
    )

    subprocess.run(["git", "tag", "-d", "v1.0.0"], cwd=root, check=True)
    reject(lambda: validate_tagged_release(root), "missing release tag")
    subprocess.run(
        ["git", "update-ref", "refs/tags/v1.0.0", tag_object], cwd=root, check=True
    )

    subprocess.run(
        ["git", "update-ref", "refs/tags/v1.0.0", candidate], cwd=root, check=True
    )
    reject(lambda: validate_tagged_release(root), "lightweight release tag")
    subprocess.run(
        ["git", "update-ref", "refs/tags/v1.0.0", tag_object], cwd=root, check=True
    )

    subprocess.run(
        [
            "git",
            "tag",
            "-f",
            "-a",
            "v1.0.0",
            "-m",
            "moved fixture tag",
            post_release,
        ],
        cwd=root,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    reject(lambda: validate_tagged_release(root), "moved release tag")
    subprocess.run(
        ["git", "update-ref", "refs/tags/v1.0.0", tag_object], cwd=root, check=True
    )

    tree = git(root, "rev-parse", f"{candidate}^{{tree}}")
    unrelated = git(root, "commit-tree", tree, "-m", "unrelated candidate tree")
    subprocess.run(
        [
            "git",
            "tag",
            "-f",
            "-a",
            "v1.0.0",
            "-m",
            "non-descendant fixture tag",
            unrelated,
        ],
        cwd=root,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    unrelated_tag_object = git(root, "rev-parse", "refs/tags/v1.0.0")
    unrelated_identity = base / "unrelated-tag-identity.json"
    unrelated_identity.write_text(
        json.dumps(
            {
                "release": "v1.0.0",
                "tag": {
                    "name": "v1.0.0",
                    "object_sha": unrelated_tag_object,
                    "target_sha": unrelated,
                },
            }
        )
    )
    reject(
        lambda: validate_tagged_release(root, identity_path=unrelated_identity),
        "non-descendant annotated release tag",
    )
    subprocess.run(
        ["git", "update-ref", "refs/tags/v1.0.0", tag_object], cwd=root, check=True
    )

    tagged = validate_tagged_release(root)
    check(
        tagged["tag_object"] == tag_object
        and tagged["tag_target"] == candidate
        and tagged["current_commit"] == documentation_descendant,
        "tagged release remains valid under later documentation",
    )
    check(
        tagged["release_candidate_commit"] == candidate
        and tagged["release_candidate_commit"] != tagged["current_commit"],
        "tagged release identity is separate from current HEAD",
    )

    compatible = validate_compatible_source(
        root, implementation, documentation_descendant
    )
    check(
        compatible["candidate_commit"] == candidate
        and compatible["current_descendant"] == documentation_descendant,
        "compatible descendant never replaces the release candidate",
    )
    reject(
        lambda: validate_compatible_source(root, implementation, unrelated),
        "non-descendant current commit",
    )

    outer_head = git(root, "rev-parse", "HEAD")
    worktree_root = base / "worktrees"
    with detached_release_worktree(root, candidate, worktree_root) as release_root:
        check(
            git(release_root, "rev-parse", "HEAD") == candidate,
            "isolated worktree checks out the exact immutable source",
        )
        check(
            git(root, "rev-parse", "HEAD") == outer_head,
            "current worktree remains on the descendant",
        )
    check(
        len(git(root, "worktree", "list", "--porcelain").split("worktree ")) == 2,
        "temporary release worktree is removed",
    )

    commands = reconstruction_commands(
        Path("/exact-release"),
        candidate,
        Path("/tmp/output"),
        Path("/tmp/build"),
        Path("/tmp/report.json"),
        4,
    )
    command_text = "\n".join(" ".join(command) for command in commands)
    check(
        f"--candidate {candidate}" in command_text
        and "manifests/public_reproducibility_suite.json" in command_text,
        "canonical reconstruction is anchored to the exact candidate",
    )
    check(
        command_text.count("validate_reproducibility.py") == 2
        and "--verify-inputs" in command_text
        and "--allow-compatible-environment" in command_text,
        "manifest, input, output, and validator gates remain mandatory",
    )

ci = (ROOT / ".github/workflows/ci.yml").read_text()
release_workflow = (ROOT / ".github/workflows/release.yml").read_text()
reproducibility_workflow = (ROOT / ".github/workflows/reproducibility.yml").read_text()
check(
    "scripts/reconstruct_tagged_release.py" in ci
    and "--install-dependencies" in ci
    and "fetch-tags: true" in ci,
    "CI invokes isolated hash-locked tagged reconstruction with full history",
)
check(
    "scripts/reproduce.py --manifest" not in ci
    and "github.sha" not in ci
    and "pull_request.merge_commit_sha" not in ci,
    "current PR merge commit is never supplied as the v1.0.0 candidate",
)
check(
    '--candidate "$CANDIDATE_SHA"' in release_workflow
    and '--candidate "$CANDIDATE_SHA"' in reproducibility_workflow,
    "manual exact-candidate workflows retain strict validation",
)

print(f"{cases} release provenance and isolated reconstruction cases passed")
