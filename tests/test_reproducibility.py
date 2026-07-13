#!/usr/bin/env python3
import copy
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from reproducibility import (ReproducibilityError, canonical_json, compare_inventory, inventory,
    load_manifest, manifest_identity, normalize_path, semantic_hash, sha256_bytes, sha256_file,
    validate_manifest, validate_suite, verify_inputs)

cases = 0
def check(condition, name):
    global cases
    cases += 1
    if not condition:
        raise AssertionError(name)

def rejected(function, name):
    global cases
    cases += 1
    try:
        function()
    except (ReproducibilityError, json.JSONDecodeError, OSError):
        return
    raise AssertionError(name)

manifest_path = ROOT / "manifests/public_synthetic_single_ma.json"
manifest = load_manifest(manifest_path)
suite = json.loads((ROOT / "manifests/public_reproducibility_suite.json").read_text())

check(manifest["manifest_schema_version"] == 1, "schema validation")
bad = copy.deepcopy(manifest); bad["manifest_schema_version"] = 99
rejected(lambda: validate_manifest(bad), "unsupported schema rejection")
check(canonical_json({"b": 2, "a": 1}) == '{"a":1,"b":2}', "deterministic serialization")
check(manifest_identity(manifest) == manifest["manifest_id"], "stable manifest id")
check(len(sha256_file(ROOT / "data/synthetic/SYN_EQ_A.csv")) == 64, "file sha256")
check(all(item.get("data_classification") == "synthetic" for item in manifest["inputs"]), "synthetic inputs classified")
bad = copy.deepcopy(manifest); bad["inputs"][0]["path"] = "data/missing.csv"
rejected(lambda: verify_inputs(ROOT, bad), "missing input rejection")
bad = copy.deepcopy(manifest); bad["inputs"][0]["sha256"] = "0" * 64
rejected(lambda: verify_inputs(ROOT, bad), "altered input rejection")
bad = copy.deepcopy(manifest); bad["configuration"]["sha256"] = "0" * 64
rejected(lambda: verify_inputs(ROOT, bad), "config hash rejection")
check("starting_capital" in manifest["configuration"]["resolved"], "resolved config preserved")
check(manifest["build"]["cxx_standard"] == 17, "build metadata")
check(manifest["runtime_environment"]["dependency_lock"] == "requirements-validation.txt", "dependency metadata")

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    (root / "a.csv").write_text("x,y\n1,2\n")
    (root / "b.json").write_text('{"b":2,"a":1}\n')
    (root / "c.md").write_bytes(b"a\r\nb\r\n")
    first = inventory(root, [])
    check(first[0]["sha256"] == sha256_file(root / "a.csv"), "exact output hash")
    check(first[0]["semantic_sha256"] == semantic_hash(root / "a.csv"), "semantic output hash")
    (root / "v.json").write_text('{"elapsed_seconds":1,"value":2}')
    one = semantic_hash(root / "v.json", ["elapsed_seconds"])
    (root / "v.json").write_text('{"elapsed_seconds":9,"value":2}')
    check(one == semantic_hash(root / "v.json", ["elapsed_seconds"]), "volatile exclusion")
    (root / "a.csv").write_text("x,y\n3,4\n1,2\n")
    check(first[0]["semantic_sha256"] != semantic_hash(root / "a.csv"), "row order preserved")
    (root / "j1.json").write_text('{"a":1,"b":2}')
    (root / "j2.json").write_text('{"b":2,"a":1}')
    check(semantic_hash(root / "j1.json") == semantic_hash(root / "j2.json"), "json key canonicalization")
    check(normalize_path("a\\b/c") == "a/b/c", "path normalization")
    rejected(lambda: normalize_path("../escape"), "path escape rejection")
    (root / "lf.md").write_bytes(b"a\nb\n")
    check(semantic_hash(root / "c.md") == semantic_hash(root / "lf.md"), "line ending normalization")
    (root / "number.csv").write_text("x\n1.0000000000001\n")
    before = semantic_hash(root / "number.csv")
    (root / "number.csv").write_text("x\n1.0000000000002\n")
    check(before != semantic_hash(root / "number.csv"), "numeric preservation")
    bad = copy.deepcopy(manifest); bad["outputs"]["artifacts"][0]["tolerance"] = 0.01
    rejected(lambda: validate_manifest(bad), "unsupported tolerance")
    check(len(manifest["outputs"]["artifacts"]) > 0, "output inventory complete")
    tiny = copy.deepcopy(manifest); tiny["outputs"]["artifacts"] = first[:1]
    rejected(lambda: compare_inventory(root, tiny), "extra output rejection")
    tiny["outputs"]["forbid_extra"] = False
    rejected(lambda: compare_inventory(root / "missing", tiny), "missing output rejection")

bad = copy.deepcopy(manifest); bad["lineage"]["edges"] = []
rejected(lambda: validate_manifest(bad), "lineage validation")
bad = copy.deepcopy(manifest); bad["commands"] = []
rejected(lambda: validate_manifest(bad), "command sequence validation")
bad = copy.deepcopy(manifest); bad["validators"] = []
check(validate_manifest(bad) is bad, "validator list may be empty when artifact mappings remain explicit")
bad = copy.deepcopy(manifest); bad["outputs"]["artifacts"][0]["validator"] = ""
rejected(lambda: validate_manifest(bad), "artifact validator mapping")
check(validate_suite(suite) is suite, "suite orchestration validation")
bad_suite = copy.deepcopy(suite); bad_suite["children"].append(copy.deepcopy(bad_suite["children"][0]))
rejected(lambda: validate_suite(bad_suite), "duplicate suite child")
bad_suite = copy.deepcopy(suite); bad_suite["suite_id_hash"] = "sha256:" + "0" * 64
rejected(lambda: validate_suite(bad_suite), "suite identity corruption")
bad = copy.deepcopy(manifest); bad["manifest_id"] = "sha256:" + "0" * 64
rejected(lambda: validate_manifest(bad), "corrupted manifest id")
bad = copy.deepcopy(manifest); bad["inputs"][0]["sha256"] = "xyz"
rejected(lambda: validate_manifest(bad), "corrupted expected hash")
bad = copy.deepcopy(manifest); bad["inputs"].append(copy.deepcopy(bad["inputs"][0]))
rejected(lambda: validate_manifest(bad), "duplicate input")
bad = copy.deepcopy(manifest); bad["outputs"]["artifacts"].append(copy.deepcopy(bad["outputs"]["artifacts"][0]))
rejected(lambda: validate_manifest(bad), "duplicate artifact")
bad = copy.deepcopy(manifest); bad["known_volatile_fields"].append(bad["known_volatile_fields"][0])
rejected(lambda: validate_manifest(bad), "duplicate volatile")
bad = copy.deepcopy(manifest); bad["reproducibility_level"] = "marketing_exact"
rejected(lambda: validate_manifest(bad), "unknown level")
bad = copy.deepcopy(manifest); bad["surprise"] = True
rejected(lambda: validate_manifest(bad), "unknown manifest field")
check(manifest["source_tree_policy"] == "exact_commit", "commit policy")
check(manifest["execution"]["supported_threads"] == [1, 2, 4, 8], "thread policy")
check(manifest["randomness"]["seed_derivation"].find("worker") >= 0, "seed policy")
check(manifest["randomness"]["engine"] == "mt19937", "RNG engine metadata")
check(manifest["randomness"]["mapping"] == "portable_bounded_v1", "RNG mapping metadata")
check(manifest["randomness"]["stochastic_methodology_version"] == 2, "stochastic methodology metadata")
bad = copy.deepcopy(manifest); bad["randomness"]["mapping"] = "standard_library_distribution"
rejected(lambda: validate_manifest(bad), "legacy RNG mapping rejection")
bad = copy.deepcopy(manifest); del bad["randomness"]["engine"]
rejected(lambda: validate_manifest(bad), "missing RNG engine rejection")
check(all(not Path(item["path"]).is_absolute() for item in manifest["inputs"]), "portable paths")
portfolio_manifest = load_manifest(ROOT / "manifests/public_synthetic_portfolio_equal_weight.json")
check(any(item["reproducibility_level"] == "presentation_only" for item in portfolio_manifest["outputs"]["artifacts"]), "presentation boundary")
check(all(item["tolerance"] is None for item in manifest["outputs"]["artifacts"]), "zero tolerance policy")
check(verify_inputs(ROOT, manifest)[0]["status"] == "exact_match", "verify-only primitive")
check(sha256_bytes(b"abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "standard sha256")
rejected(lambda: load_manifest(ROOT / "manifests/templates/local_real_data_manifest.template.json"), "unresolved local template rejection")

if cases != 53:
    raise AssertionError(f"expected 53 cases, observed {cases}")
print(f"{cases} reproducibility cases passed")
