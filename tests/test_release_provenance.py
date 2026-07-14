#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from validate_release_provenance import ProvenanceError, validate_path_closure


def commit(root: Path, message: str) -> str:
    subprocess.run(["git", "add", "."], cwd=root, check=True)
    subprocess.run(["git", "commit", "-qm", message], cwd=root, check=True)
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()


def reject(function, label: str) -> None:
    try:
        function()
    except ProvenanceError:
        return
    raise AssertionError(f"accepted invalid provenance: {label}")


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.email", "fixture@example.invalid"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.name", "Fixture"], cwd=root, check=True)
    (root / "runtime.cpp").write_text("int value = 1;\n")
    implementation = commit(root, "implementation")

    (root / "manifests").mkdir()
    (root / "manifests/package.json").write_text("{}\n")
    manifest = commit(root, "manifests")

    (root / "audit/release_v1").mkdir(parents=True)
    (root / "audit/release_v1/evidence.json").write_text("{}\n")
    evidence = commit(root, "evidence")
    result = validate_path_closure(root, implementation, manifest, evidence)
    assert result["manifest_paths"] == ["manifests/package.json"]
    assert result["evidence_paths"] == ["audit/release_v1/evidence.json"]

    (root / "runtime.cpp").write_text("int value = 2;\n")
    changed_runtime = commit(root, "runtime after manifests")
    reject(
        lambda: validate_path_closure(root, implementation, manifest, changed_runtime),
        "runtime change after manifest capture",
    )
    reject(
        lambda: validate_path_closure(root, manifest, implementation, evidence),
        "reversed ancestry",
    )

print("Release provenance closure tests passed")
