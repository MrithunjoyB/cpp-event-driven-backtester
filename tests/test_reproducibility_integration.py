#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from reproducibility import (ReproducibilityError, manifest_identity, reconstruct, semantic_hash,
                             sha256_file)

cases = 0
def check(value, name):
    global cases
    cases += 1
    if not value:
        raise AssertionError(name)

def reject(function, name):
    global cases
    cases += 1
    try:
        function()
    except ReproducibilityError:
        return
    raise AssertionError(name)

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    (root / "data").mkdir(); (root / "configs").mkdir(); (root / "build").mkdir()
    (root / "data/input.csv").write_text("Date,Close\n2020-01-01,1\n")
    (root / "configs/test.json").write_text('{"output_directory":"unused"}\n')
    (root / "requirements-validation.txt").write_text("")
    cli = root / "build/quant_cli"
    cli.write_text("#!/bin/sh\necho 2.0.0\n"); cli.chmod(0o755)
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.email", "fixture@example.invalid"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.name", "Fixture"], cwd=root, check=True)
    subprocess.run(["git", "add", "."], cwd=root, check=True)
    subprocess.run(["git", "commit", "-qm", "fixture"], cwd=root, check=True)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    expected = root / "expected.csv"; expected.write_text("x\n1\n")
    manifest = {
        "manifest_schema_version": 1, "manifest_id": "pending", "experiment_id": "fixture",
        "package_type": "fixture", "description": "fixture", "created_by": "test",
        "source_commit": commit, "source_tree_policy": "exact_commit",
        "repository": "fixture", "build": {"cxx_standard": 17},
        "runtime_environment": {"dependency_lock": "requirements-validation.txt"},
        "inputs": [{"logical_name": "input", "role": "fixture", "path": "data/input.csv",
                    "size_bytes": (root / "data/input.csv").stat().st_size, "sha256": sha256_file(root / "data/input.csv")}],
        "configuration": {"path": "configs/test.json", "sha256": sha256_file(root / "configs/test.json"),
                          "driver": "typed_config", "resolved": {}, "defaults_applied": True},
        "execution": {"policy": "fixture", "default_mode": "serial", "effective_threads": 1},
        "randomness": {"seed": 0, "engine": "mt19937", "mapping": "portable_bounded_v1",
                       "stochastic_methodology_version": 2},
        "methodology": {"version": "fixture_stochastic_v2"},
        "commands": [
            {"name": "generate", "argv": [sys.executable, "-c", "from pathlib import Path; p=Path(r'{output}'); p.mkdir(exist_ok=True); (p/'a.csv').write_text('x\\n1\\n')"]},
            {"name": "validator", "argv": [sys.executable, "-c", "raise SystemExit(0)"]},
        ],
        "outputs": {"root_policy": "isolated", "forbid_extra": True, "artifacts": [{
            "path": "a.csv", "artifact_type": "csv", "schema": {"columns": ["x"]},
            "size_bytes": expected.stat().st_size, "row_count": 2, "sha256": sha256_file(expected),
            "semantic_sha256": semantic_hash(expected), "reproducibility_level": "canonical_semantic",
            "validator": "validator", "parents": ["input", "config", "cli"], "required": True, "tolerance": None}]},
        "validators": [{"name": "validator", "required": True}], "reports": [],
        "reproducibility_level": "canonical_semantic", "known_volatile_fields": [], "limitations": [],
        "lineage": {"edges": [["input", "output"]]},
    }
    manifest["manifest_id"] = manifest_identity(manifest)
    target = root / "published"
    report = reconstruct(root, manifest, target, root / "build", "serial", 1, build=False)
    check(report["status"] == "success" and (target / "a.csv").is_file(), "atomic publication")
    check(reconstruct(root, manifest, target, root / "build", "serial", 1, verify_only=True, build=False)["mode"] == "verify_only", "verify-only")
    previous = (target / "a.csv").read_bytes()
    failed = json.loads(json.dumps(manifest)); failed["commands"][0]["argv"] = [sys.executable, "-c", "raise SystemExit(7)"]
    reject(lambda: reconstruct(root, failed, target, root / "build", "serial", 1, build=False), "command failure")
    check((target / "a.csv").read_bytes() == previous and not list(root.glob(".reproduce-*")), "failed staging cleanup")
    failed = json.loads(json.dumps(manifest)); failed["commands"][1]["argv"] = [sys.executable, "-c", "raise SystemExit(8)"]
    reject(lambda: reconstruct(root, failed, target, root / "build", "serial", 1, build=False), "validator failure")
    (root / "data/input.csv").write_text("altered\n")
    reject(lambda: reconstruct(root, manifest, target, root / "build", "serial", 1, verify_only=True, build=False), "modified input")
    subprocess.run(["git", "checkout", "--", "data/input.csv"], cwd=root, check=True)
    wrong = json.loads(json.dumps(manifest)); wrong["source_commit"] = "0" * 40; wrong["manifest_id"] = manifest_identity(wrong)
    reject(lambda: reconstruct(root, wrong, target, root / "build", "serial", 1, verify_only=True, build=False), "wrong commit")
    (root / "configs/test.json").write_text("dirty\n")
    reject(lambda: reconstruct(root, manifest, target, root / "build", "serial", 1, verify_only=True, build=False), "dirty tree")

if cases != 8:
    raise AssertionError(f"expected 8 integration cases, observed {cases}")
print(f"{cases} reproducibility integration cases passed")
