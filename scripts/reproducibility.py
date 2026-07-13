#!/usr/bin/env python3
"""Deterministic manifest validation, hashing, and reconstruction primitives."""

import csv
import hashlib
import importlib.metadata
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SCHEMA_VERSION = 1
LEVELS = {"exact_byte", "canonical_semantic", "methodological"}
HEX = set("0123456789abcdef")
VOLATILE_JSON_FIELDS = {
    "generated_at_utc", "generated_at", "timestamp", "elapsed_seconds",
    "actual_commit", "source_tree_status", "hostname", "username",
    "git_commit_hash", "output_directory", "portfolio_output_directory", "run_timestamp_utc",
}
REQUIRED_ROOT = {
    "manifest_schema_version", "manifest_id", "experiment_id", "package_type",
    "description", "created_by", "source_commit", "source_tree_policy",
    "repository", "build", "runtime_environment", "inputs", "configuration",
    "execution", "randomness", "methodology", "commands", "outputs",
    "validators", "reports", "reproducibility_level", "known_volatile_fields",
    "limitations", "lineage",
}


class ReproducibilityError(RuntimeError):
    pass


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def sha256_bytes(value):
    return hashlib.sha256(value).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def normalize_path(value):
    normalized = str(value).replace("\\", "/")
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if normalized.startswith("./"):
        normalized = normalized[2:]
    if normalized.startswith("/") or (len(normalized) > 1 and normalized[1] == ":"):
        raise ReproducibilityError("manifest paths must be repository-relative")
    if ".." in Path(normalized).parts:
        raise ReproducibilityError("manifest paths cannot escape their root")
    return normalized


def _without_volatile(value, volatile):
    if isinstance(value, dict):
        return {key: _without_volatile(item, volatile) for key, item in sorted(value.items())
                if key not in volatile}
    if isinstance(value, list):
        return [_without_volatile(item, volatile) for item in value]
    if isinstance(value, str):
        return value.replace("\\", "/")
    return value


def semantic_bytes(path, volatile_fields=None):
    path = Path(path)
    volatile = set(volatile_fields or ()) | VOLATILE_JSON_FIELDS
    suffix = path.suffix.lower()
    if suffix == ".csv":
        with path.open(newline="", encoding="utf-8-sig") as handle:
            rows = list(csv.reader(handle))
        return canonical_json(rows).encode("ascii")
    if suffix == ".json":
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        return canonical_json(_without_volatile(data, volatile)).encode("ascii")
    if suffix in {".md", ".txt"}:
        return path.read_text(encoding="utf-8-sig").replace("\r\n", "\n").replace("\r", "\n").encode()
    raise ReproducibilityError(f"no semantic canonicalizer for {suffix or 'extensionless file'}")


def semantic_hash(path, volatile_fields=None):
    return sha256_bytes(semantic_bytes(path, volatile_fields))


def row_count(path):
    if Path(path).suffix.lower() != ".csv":
        return None
    with Path(path).open(newline="", encoding="utf-8-sig") as handle:
        return sum(1 for _ in csv.reader(handle))


def manifest_identity(manifest):
    inputs = [{"logical_name": item["logical_name"], "sha256": item["sha256"]}
              for item in manifest["inputs"]]
    stable = {
        "manifest_schema_version": manifest["manifest_schema_version"],
        "experiment_id": manifest["experiment_id"],
        "package_type": manifest["package_type"],
        "source_commit": manifest["source_commit"],
        "inputs": inputs,
        "configuration_sha256": manifest["configuration"]["sha256"],
        "methodology_version": manifest["methodology"]["version"],
        "seed": manifest["randomness"]["seed"],
        "execution_policy": manifest["execution"]["policy"],
    }
    return "sha256:" + sha256_bytes(canonical_json(stable).encode())


def validate_manifest(manifest, path=None):
    errors = []
    missing = REQUIRED_ROOT - set(manifest)
    unknown = set(manifest) - REQUIRED_ROOT
    if missing:
        errors.append("missing fields: " + ", ".join(sorted(missing)))
    if unknown:
        errors.append("unknown fields: " + ", ".join(sorted(unknown)))
    if manifest.get("manifest_schema_version") != SCHEMA_VERSION:
        errors.append(f"unsupported manifest schema: {manifest.get('manifest_schema_version')}")
    if manifest.get("reproducibility_level") not in LEVELS:
        errors.append("unknown reproducibility level")
    randomness = manifest.get("randomness", {})
    if randomness.get("engine") != "mt19937" or randomness.get("mapping") != "portable_bounded_v1":
        errors.append("unknown or legacy RNG methodology")
    if randomness.get("stochastic_methodology_version") != 2:
        errors.append("stochastic methodology version must be 2")
    if manifest.get("source_tree_policy") != "exact_commit":
        errors.append("release-candidate manifests require exact_commit source policy")
    names = [item.get("logical_name") for item in manifest.get("inputs", [])]
    if len(names) != len(set(names)):
        errors.append("duplicate logical input names")
    artifacts = manifest.get("outputs", {}).get("artifacts", [])
    paths = [item.get("path") for item in artifacts]
    if len(paths) != len(set(paths)):
        errors.append("duplicate output artifact entries")
    for item in manifest.get("inputs", []):
        digest = item.get("sha256", "")
        if len(digest) != 64 or any(char not in HEX for char in digest):
            errors.append(f"invalid input hash for {item.get('logical_name')}")
        try:
            normalize_path(item.get("path", ""))
        except ReproducibilityError as error:
            errors.append(str(error))
    for item in artifacts:
        try:
            normalize_path(item.get("path", ""))
        except ReproducibilityError as error:
            errors.append(str(error))
        if item.get("reproducibility_level") not in LEVELS | {"presentation_only", "environment_only"}:
            errors.append(f"invalid artifact reproducibility level: {item.get('path')}")
        if item.get("required", True) and not item.get("validator"):
            errors.append(f"missing validator mapping: {item.get('path')}")
        if not item.get("parents"):
            errors.append(f"missing artifact parents: {item.get('path')}")
        tolerance = item.get("tolerance")
        if tolerance not in (None, 0, 0.0):
            valid_tolerance = (item.get("reproducibility_level") == "methodological" and
                               item.get("comparison_policy") == "numeric_field_tolerance" and
                               isinstance(tolerance, dict) and tolerance and
                               all(isinstance(value, (int, float)) and value > 0 for value in tolerance.values()) and
                               isinstance(item.get("expected_rows"), list))
            if not valid_tolerance:
                errors.append(f"unsupported tolerance for {item.get('path')}")
    volatile = manifest.get("known_volatile_fields", [])
    if len(volatile) != len(set(volatile)):
        errors.append("duplicate volatile field")
    if not manifest.get("lineage", {}).get("edges"):
        errors.append("missing artifact lineage")
    commands = manifest.get("commands", [])
    if not commands or any(not item.get("argv") for item in commands):
        errors.append("invalid command sequence")
    expected_id = manifest_identity(manifest) if not missing else None
    if expected_id and manifest.get("manifest_id") != expected_id:
        errors.append("manifest identity mismatch")
    if errors:
        location = f" ({path})" if path else ""
        raise ReproducibilityError("manifest validation failed" + location + ": " + "; ".join(errors))
    return manifest


def load_manifest(path):
    try:
        value = json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ReproducibilityError(f"cannot load manifest {path}: {error}") from error
    return validate_manifest(value, path)


def validate_suite(suite, path=None):
    required = {"manifest_schema_version", "package_type", "suite_id", "suite_id_hash", "description",
                "children", "source_commit", "default_execution_mode", "default_threads"}
    if set(suite) != required:
        raise ReproducibilityError("suite fields are incomplete or unknown")
    if suite["manifest_schema_version"] != SCHEMA_VERSION or suite["package_type"] != "suite":
        raise ReproducibilityError("unsupported suite manifest")
    ids = [item.get("id") for item in suite["children"]]
    paths = [item.get("manifest") for item in suite["children"]]
    if not ids or len(ids) != len(set(ids)) or len(paths) != len(set(paths)):
        raise ReproducibilityError("suite children must be non-empty and unique")
    for item in suite["children"]:
        normalize_path(item["manifest"])
    stable = dict(suite)
    claimed = stable.pop("suite_id_hash")
    expected = "sha256:" + sha256_bytes(canonical_json(stable).encode())
    if claimed != expected:
        raise ReproducibilityError("suite identity mismatch")
    return suite


def verify_inputs(root, manifest):
    results = []
    for item in manifest["inputs"]:
        path = root / normalize_path(item["path"])
        if not path.is_file():
            raise ReproducibilityError(f"missing input: {item['logical_name']} ({item['path']})")
        actual = sha256_file(path)
        if actual != item["sha256"]:
            raise ReproducibilityError(f"input hash mismatch: {item['logical_name']}")
        if path.stat().st_size != item["size_bytes"]:
            raise ReproducibilityError(f"input size mismatch: {item['logical_name']}")
        results.append({"logical_name": item["logical_name"], "status": "exact_match", "sha256": actual})
    config = root / normalize_path(manifest["configuration"]["path"])
    if not config.is_file() or sha256_file(config) != manifest["configuration"]["sha256"]:
        raise ReproducibilityError("configuration hash mismatch")
    return results


def inventory(directory, volatile_fields, validator="reproducibility_validator"):
    artifacts = []
    for path in sorted(item for item in Path(directory).rglob("*") if item.is_file()):
        relative = normalize_path(path.relative_to(directory))
        suffix = path.suffix.lower()
        presentation = suffix in {".png", ".svg", ".pdf"}
        environment_only = path.name in {"performance_counters.csv", "parallel_execution_metadata.json"}
        artifacts.append({
            "path": relative,
            "artifact_type": suffix.lstrip(".") or "file",
            "schema": _artifact_schema(path),
            "size_bytes": path.stat().st_size,
            "row_count": row_count(path),
            "sha256": sha256_file(path) if not presentation and not environment_only else None,
            "semantic_sha256": None if presentation or environment_only else semantic_hash(path, volatile_fields),
            "reproducibility_level": "environment_only" if environment_only else ("presentation_only" if presentation else "canonical_semantic"),
            "validator": validator,
            "parents": ["canonical_inputs", "resolved_configuration", "quant_cli"],
            "required": True,
            "tolerance": None,
        })
    return artifacts


def _artifact_schema(path):
    if Path(path).suffix.lower() == ".csv":
        with Path(path).open(newline="", encoding="utf-8-sig") as handle:
            header = next(csv.reader(handle), [])
        return {"columns": header}
    if Path(path).suffix.lower() == ".json":
        try:
            value = json.loads(Path(path).read_text())
            return {"root_type": type(value).__name__}
        except json.JSONDecodeError:
            return {"root_type": "invalid"}
    return None


def compare_inventory(directory, manifest):
    expected = {item["path"]: item for item in manifest["outputs"]["artifacts"]}
    actual_paths = {normalize_path(path.relative_to(directory)): path for path in Path(directory).rglob("*") if path.is_file()}
    missing = sorted(path for path, item in expected.items() if item.get("required", True) and path not in actual_paths)
    extra = sorted(set(actual_paths) - set(expected))
    if missing:
        raise ReproducibilityError("missing outputs: " + ", ".join(missing))
    if extra and manifest["outputs"].get("forbid_extra", True):
        raise ReproducibilityError("undeclared outputs: " + ", ".join(extra))
    results = []
    for name, item in sorted(expected.items()):
        if name not in actual_paths:
            results.append({"path": name, "status": "not_applicable"})
            continue
        path = actual_paths[name]
        if item["reproducibility_level"] in {"presentation_only", "environment_only"}:
            status = "presentation_generated" if item["reproducibility_level"] == "presentation_only" else "environment_recorded"
            results.append({"path": name, "status": status})
            continue
        if item["reproducibility_level"] == "methodological":
            policy = item.get("comparison_policy")
            if row_count(path) != item.get("row_count"):
                raise ReproducibilityError(f"methodological row-count mismatch: {name}")
            if policy == "shape_only":
                results.append({"path": name, "status": "methodological_shape_match"})
                continue
            if policy == "presence_only":
                results.append({"path": name, "status": "methodological_presence_match"})
                continue
            if policy == "numeric_field_tolerance":
                with path.open(newline="", encoding="utf-8-sig") as handle:
                    actual_rows = list(csv.DictReader(handle))
                expected_rows = item.get("expected_rows", [])
                if len(actual_rows) != len(expected_rows):
                    raise ReproducibilityError(f"methodological row mismatch: {name}")
                tolerances = item["tolerance"]
                for expected_row, actual_row in zip(expected_rows, actual_rows):
                    if set(expected_row) != set(actual_row):
                        raise ReproducibilityError(f"methodological schema mismatch: {name}")
                    for field, expected_value in expected_row.items():
                        if expected_value == actual_row[field]:
                            continue
                        if field not in tolerances:
                            raise ReproducibilityError(f"untolerated field mismatch: {name}:{field}")
                        try:
                            difference = abs(float(expected_value) - float(actual_row[field]))
                        except ValueError as error:
                            raise ReproducibilityError(f"nonnumeric tolerated field: {name}:{field}") from error
                        if difference > tolerances[field]:
                            raise ReproducibilityError(
                                f"methodological tolerance exceeded: {name}:{field} ({difference} > {tolerances[field]})")
                results.append({"path": name, "status": "methodological_tolerance_match", "tolerance": tolerances})
                continue
            raise ReproducibilityError(f"unknown methodological comparison policy: {name}")
        semantic = semantic_hash(path, manifest["known_volatile_fields"])
        if semantic != item.get("semantic_sha256"):
            raise ReproducibilityError(f"semantic output mismatch: {name}")
        byte_status = "exact_match" if sha256_file(path) == item.get("sha256") else "semantic_match"
        if row_count(path) != item.get("row_count"):
            raise ReproducibilityError(f"row-count mismatch: {name}")
        results.append({"path": name, "status": byte_status, "semantic_sha256": semantic})
    return results


def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root, text=True).strip()


def environment(root, build_dir):
    def first(command):
        try:
            return subprocess.check_output(command, cwd=root, text=True, stderr=subprocess.STDOUT).splitlines()[0]
        except (OSError, subprocess.CalledProcessError):
            return "unavailable"
    return {
        "os": platform.platform(), "architecture": platform.machine(),
        "python": platform.python_version(), "compiler": first(["c++", "--version"]),
        "cmake": first(["cmake", "--version"]), "cli_version": first([str(build_dir / "quant_cli"), "--version"]),
        "cli_sha256": sha256_file(build_dir / "quant_cli") if (build_dir / "quant_cli").is_file() else "unavailable",
    }


def verify_dependencies(root, lock_path):
    results = []
    for line in (root / normalize_path(lock_path)).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "==" not in line:
            raise ReproducibilityError(f"dependency lock entry is not exact: {line}")
        name, expected = line.split("==", 1)
        try:
            actual = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError as error:
            raise ReproducibilityError(f"missing dependency: {name}") from error
        if actual != expected:
            raise ReproducibilityError(f"dependency version mismatch for {name}: expected {expected}, found {actual}")
        results.append({"name": name, "version": actual, "status": "exact_match"})
    return results


def substitute(argv, values):
    return [str(part).format(**values) for part in argv]


def _run(argv, root, env, records):
    started = time.monotonic()
    result = subprocess.run(argv, cwd=root, env=env, text=True, capture_output=True)
    records.append({"argv": argv, "returncode": result.returncode, "elapsed_seconds": time.monotonic() - started})
    if result.returncode:
        message = (result.stderr or result.stdout).strip()
        raise ReproducibilityError(f"command failed ({result.returncode}): {' '.join(argv)}\n{message}")


def reconstruct(root, manifest, output_directory, build_dir, execution_mode, threads,
                verify_only=False, allow_compatible_environment=False, allow_dirty=False,
                keep_failed_output=False, build=True, capture_expected=False):
    started = time.monotonic()
    actual_commit = git(root, "rev-parse", "HEAD")
    dirty = bool(git(root, "status", "--porcelain", "--untracked-files=no"))
    if dirty and not allow_dirty:
        raise ReproducibilityError("repository is dirty; commit/stash tracked changes or pass --allow-dirty")
    if actual_commit != manifest["source_commit"] and not allow_compatible_environment:
        raise ReproducibilityError(f"wrong Git commit: expected {manifest['source_commit']}, found {actual_commit}")
    input_results = verify_inputs(root, manifest)
    dependency_results = verify_dependencies(root, manifest["runtime_environment"]["dependency_lock"])
    if verify_only:
        return {"status": "success", "mode": "verify_only", "manifest_id": manifest["manifest_id"],
                "actual_commit": actual_commit, "inputs": input_results, "dependencies": dependency_results,
                "elapsed_seconds": time.monotonic() - started}
    if build:
        subprocess.run(["cmake", "-S", str(root), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release",
                        "-DQUANT_ENABLE_STRICT_WARNINGS=ON"], check=True)
        subprocess.run(["cmake", "--build", str(build_dir), "--parallel"], check=True)
    cli = build_dir / "quant_cli"
    if not cli.is_file():
        raise ReproducibilityError(f"missing executable: {cli}")
    output_directory = Path(output_directory).resolve()
    output_directory.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".reproduce-", dir=output_directory.parent))
    package = stage / ("results" if manifest["configuration"].get("driver") == "legacy_cli" else "package")
    package.mkdir()
    records = []
    env = os.environ.copy()
    env.update({"LC_ALL": "C", "TZ": "UTC", "MPLCONFIGDIR": str(stage / "matplotlib")})
    values = {"root": root, "cli": cli, "python": sys.executable, "output": package,
              "threads": threads, "execution_mode": execution_mode, "build": build_dir}
    try:
        config = json.loads((root / manifest["configuration"]["path"]).read_text())
        if manifest["configuration"].get("driver") == "typed_config":
            config["output_directory"] = str(package)
            if "portfolio_output_directory" in config:
                config["portfolio_output_directory"] = str(package)
            config["execution_mode"] = execution_mode
            config["threads"] = threads
            config_path = stage / "resolved-input-config.json"
            config_path.write_text(json.dumps(config, indent=2, sort_keys=True) + "\n")
            values["config"] = config_path
        else:
            values["config"] = root / manifest["configuration"]["path"]
            (stage / "data").symlink_to(root / "data", target_is_directory=True)
            values["legacy_cwd"] = stage
        for command in manifest["commands"]:
            argv = substitute(command["argv"], values)
            command_root = Path(values.get(command.get("cwd_key", "root"), root))
            _run(argv, command_root, env, records)
        if capture_expected:
            manifest["outputs"]["artifacts"] = inventory(package, manifest["known_volatile_fields"])
        output_results = compare_inventory(package, manifest)
        report = {
            "report_schema_version": 1, "status": "success", "manifest_id": manifest["manifest_id"],
            "source_commit": manifest["source_commit"], "actual_commit": actual_commit,
            "source_commit_status": "exact" if actual_commit == manifest["source_commit"] else "compatible_override",
            "execution_mode": execution_mode, "threads": threads, "environment": environment(root, build_dir),
            "inputs": input_results, "dependencies": dependency_results, "commands": records, "outputs": output_results,
            "validators": [{"name": item["name"], "status": "passed"} for item in manifest["validators"]],
            "known_volatile_fields": manifest["known_volatile_fields"],
            "elapsed_seconds": time.monotonic() - started,
        }
        (package / "reconstruction_report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        summary = ["# Reconstruction Report", "", f"- Manifest: `{manifest['manifest_id']}`",
                   f"- Status: success", f"- Commit: `{actual_commit}`", f"- Execution: {execution_mode}, {threads} thread(s)",
                   f"- Inputs: {len(input_results)} exact hash matches", f"- Outputs: {len(output_results)} verified artifacts"]
        (package / "reconstruction_report.md").write_text("\n".join(summary) + "\n")
        backup = None
        if output_directory.exists():
            backup = output_directory.with_name(output_directory.name + ".previous")
            if backup.exists():
                shutil.rmtree(backup)
            output_directory.rename(backup)
        try:
            package.rename(output_directory)
        except BaseException:
            if backup and backup.exists() and not output_directory.exists():
                backup.rename(output_directory)
            raise
        if backup:
            shutil.rmtree(backup)
        shutil.rmtree(stage, ignore_errors=True)
        return report
    except BaseException:
        if keep_failed_output:
            failed = output_directory.with_name(output_directory.name + ".failed")
            if failed.exists():
                shutil.rmtree(failed)
            stage.rename(failed)
        else:
            shutil.rmtree(stage, ignore_errors=True)
        raise
