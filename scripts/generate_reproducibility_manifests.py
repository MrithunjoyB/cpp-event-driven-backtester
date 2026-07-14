#!/usr/bin/env python3
"""Generate canonical manifests by running and inventorying every declared package."""

import argparse
import csv
import json
import platform
import subprocess
from pathlib import Path

from reproducibility import manifest_identity, reconstruct, row_count, sha256_file

ROOT = Path(__file__).resolve().parents[1]
SOURCE_COMMIT = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
DATA = ["SYN_EQ_A", "SYN_EQ_B", "SYN_BENCH", "SYN_EQ_C", "SYN_CRYPTO"]
DATA_DIRECTORY = ROOT / "data" / "synthetic"
SYNTHETIC_METADATA = json.loads((DATA_DIRECTORY / "metadata.json").read_text())


def data_input(ticker):
    path = DATA_DIRECTORY / f"{ticker}.csv"
    with path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    return {
        "logical_name": f"synthetic_ohlcv_{ticker}", "role": "public_canonical_synthetic_market_data",
        "path": f"data/synthetic/{ticker}.csv", "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path), "format": "csv", "schema": ",".join(SYNTHETIC_METADATA["schema"]),
        "date_range": {"start": rows[0]["Date"], "end": rows[-1]["Date"]}, "row_count": len(rows),
        "ticker_or_universe": ticker, "adjustment_policy": "manifest_configuration",
        "corporate_action_availability": "deterministic synthetic dividends and splits declared in metadata.json",
        "source_description": "Repository-generated independent synthetic fixture; not real market data",
        "acquisition_method": "offline deterministic generation",
        "acquisition_date": None, "mutability": "regenerable_and_immutable_by_sha256",
        "data_classification": "synthetic", "redistribution_status": "project_owned_apache_2_0",
        "generator_id": SYNTHETIC_METADATA["generator_id"],
        "generator_version": SYNTHETIC_METADATA["generator_version"],
        "fixture_seed": SYNTHETIC_METADATA["fixture_seed"],
        "license_or_redistribution_constraint": "Apache-2.0 project-owned synthetic fixture",
    }


def synthetic_metadata_input():
    path = DATA_DIRECTORY / "metadata.json"
    return {
        "logical_name": "synthetic_fixture_metadata",
        "role": "public_canonical_generator_metadata",
        "path": "data/synthetic/metadata.json",
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path),
        "format": "json",
        "data_classification": "synthetic",
        "redistribution_status": "project_owned_apache_2_0",
        "generator_id": SYNTHETIC_METADATA["generator_id"],
        "generator_version": SYNTHETIC_METADATA["generator_version"],
        "fixture_seed": SYNTHETIC_METADATA["fixture_seed"],
    }


def commands(kind):
    if kind == "single":
        return [
            {"name": "experiment", "cwd_key": "legacy_cwd", "argv": ["{cli}", "--mode", "single", "--ticker", "SYN_EQ_A", "--strategy", "ma_cross"]},
            {"name": "validate_results", "argv": ["{python}", "scripts/validate_results.py", "{output}"]},
        ]
    if kind == "portfolio":
        return [
            {"name": "experiment", "argv": ["{cli}", "run", "--config", "{config}", "--execution-mode", "{execution_mode}", "--threads", "{threads}"]},
            {"name": "validate_results", "argv": ["{python}", "scripts/validate_results.py", "{output}"]},
            {"name": "statistics_reference", "argv": ["{python}", "scripts/test_statistics_reference.py", "{output}/statistics"]},
            {"name": "attribution_figures", "argv": ["{python}", "scripts/visualize_attribution.py", "--directory", "{output}/attribution"]},
            {"name": "attribution_report", "argv": ["{python}", "scripts/generate_attribution_report.py", "--directory", "{output}/attribution"]},
            {"name": "statistics_figures", "argv": ["{python}", "scripts/visualize_statistics.py", "--directory", "{output}/statistics"]},
            {"name": "statistics_report", "argv": ["{python}", "scripts/generate_statistics_report.py", "--directory", "{output}/statistics"]},
        ]
    return [
        {"name": "experiment", "argv": ["{cli}", "run", "--config", "{config}", "--execution-mode", "{execution_mode}", "--threads", "{threads}"]},
        {"name": "validate_selection_risk", "argv": ["{python}", "scripts/validate_selection_risk.py", "{output}/selection_risk"]},
        {"name": "selection_reference", "argv": ["{python}", "scripts/test_selection_risk_reference.py", "{output}/selection_risk"]},
        {"name": "selection_figures", "argv": ["{python}", "scripts/visualize_selection_risk.py", "--directory", "{output}/selection_risk"]},
        {"name": "selection_report", "argv": ["{python}", "scripts/generate_selection_risk_report.py", "--directory", "{output}/selection_risk"]},
    ]


def definitions():
    return [
        ("public_synthetic_single_ma", "single_strategy", "configs/repro_single_syn_eq_a_ma.json", "single"),
        ("public_synthetic_portfolio_equal_weight", "portfolio", "configs/portfolio_equal_weight.json", "portfolio"),
        ("public_synthetic_portfolio_inverse_volatility", "portfolio", "configs/portfolio_inverse_volatility.json", "portfolio"),
        ("public_synthetic_portfolio_momentum_top_n", "portfolio", "configs/portfolio_momentum_top_n.json", "portfolio"),
        ("public_synthetic_attribution_equal_weight", "attribution", "configs/portfolio_equal_weight.json", "portfolio"),
        ("public_synthetic_statistics_equal_weight", "statistics", "configs/portfolio_equal_weight.json", "portfolio"),
        ("public_synthetic_selection_risk_ma", "selection_risk", "configs/selection_risk_ma.json", "selection"),
        ("public_synthetic_selection_risk_rsi", "selection_risk", "configs/selection_risk_rsi.json", "selection"),
        ("public_synthetic_selection_risk_macd", "selection_risk", "configs/selection_risk_macd.json", "selection"),
        ("public_synthetic_selection_risk_volatility_breakout", "selection_risk", "configs/selection_risk_volatility_breakout.json", "selection"),
        ("public_synthetic_selection_risk_combined", "selection_risk", "configs/selection_risk_all.json", "selection"),
        ("public_synthetic_selection_risk_combined_zero_cost", "selection_risk", "configs/selection_risk_all_zero_cost.json", "selection"),
        ("public_synthetic_selection_risk_combined_high_cost", "selection_risk", "configs/selection_risk_all_high_cost.json", "selection"),
    ]


def make_manifest(identifier, package_type, config_path, kind, cli):
    config_file = ROOT / config_path
    raw = json.loads(config_file.read_text())
    if kind == "single":
        resolved = raw
        driver = "legacy_cli"
    else:
        resolved = json.loads(subprocess.check_output([str(cli), "print-resolved-config", "--config", config_path], cwd=ROOT, text=True))
        driver = "typed_config"
    seed = int(resolved.get("random_seed", 0))
    manifest = {
        "manifest_schema_version": 1, "manifest_id": "pending", "experiment_id": identifier,
        "package_type": package_type, "description": f"Public synthetic canonical reproducibility package for {identifier}; not empirical market evidence",
        "created_by": "generate_reproducibility_manifests.py/public_synthetic_v1",
        "source_commit": SOURCE_COMMIT, "source_tree_policy": "exact_commit",
        "repository": "https://github.com/MrithunjoyB/cpp-event-driven-backtester",
        "build": {"system": "CMake", "build_type": "Release", "cxx_standard": 17, "strict_warnings": True,
                  "sanitizer": "none", "required_cli_version": "1.0.0", "compiler_policy": "recorded_not_identity_bound"},
        "runtime_environment": {"python": platform.python_version(), "dependency_lock": "requirements-validation.lock",
                                "locale": "C", "timezone": "UTC", "platform_policy": "semantic_cross_platform"},
        "inputs": [synthetic_metadata_input()] + [data_input(ticker) for ticker in DATA],
        "configuration": {"path": config_path, "sha256": sha256_file(config_file), "driver": driver,
                          "resolved": resolved, "defaults_applied": True},
        "execution": {"policy": "close_decision_next_open_fill", "default_mode": "serial", "effective_threads": 1,
                      "supported_modes": ["serial", "parallel"], "supported_threads": [1, 2, 4, 8]},
        "randomness": {"seed": seed, "seed_derivation": "existing deterministic experiment seed; independent of worker scheduling",
                       "engine": "mt19937", "mapping": "portable_bounded_v1", "stochastic_methodology_version": 2,
                       "simulation_count": 1000 if package_type in {"portfolio", "statistics", "selection_risk", "attribution"} else 0},
        "methodology": {"version": "causal_daily_v3_stochastic_v2", "result_schema": 3 if package_type != "single_strategy" else 1,
                        "benchmark": resolved.get("benchmark", "same_asset"), "adjustment_policy": resolved.get("adjustment_policy", "raw_price"),
                        "calendar_policy": resolved.get("calendar_valuation_mode", "asset_calendar")},
        "commands": commands(kind),
        "outputs": {"root_policy": "caller_selected_isolated_directory", "forbid_extra": True, "artifacts": []},
        "validators": [{"name": command["name"], "required": True} for command in commands(kind) if "validate" in command["name"] or "reference" in command["name"]],
        "reports": [{"name": command["name"], "required": True} for command in commands(kind) if "report" in command["name"] or "figures" in command["name"]],
        "reproducibility_level": "canonical_semantic",
        "known_volatile_fields": ["actual_commit", "elapsed_seconds", "generated_at", "generated_at_utc", "git_commit_hash", "hostname", "output_directory", "portfolio_output_directory", "run_timestamp_utc", "source_tree_status", "timestamp", "username"],
        "limitations": ["Synthetic outputs validate software and methodology but are not empirical market evidence", "Mutable remote downloads are excluded from exact reconstruction", "PNG bytes are presentation-only across platforms",
                        "Compiler and standard-library versions are recorded but not identity-bound for semantic reconstruction"],
        "lineage": {"nodes": ["inputs", "resolved_config", "quant_cli", "simulation", "analytics", "validators", "reports"],
                    "edges": [["inputs", "simulation"], ["resolved_config", "simulation"], ["quant_cli", "simulation"],
                              ["simulation", "analytics"], ["analytics", "validators"], ["analytics", "reports"]]},
    }
    manifest["manifest_id"] = manifest_identity(manifest)
    return manifest


def apply_cross_platform_statistical_policy(manifest, capture):
    policies = {
        "statistics/statistical_report.md": ("presence_only", None),
    }
    for artifact in manifest["outputs"]["artifacts"]:
        if artifact["path"] not in policies:
            continue
        policy, tolerance = policies[artifact["path"]]
        artifact["reproducibility_level"] = "methodological"
        artifact["comparison_policy"] = policy
        artifact["tolerance"] = tolerance
        artifact["sha256"] = None
        artifact["semantic_sha256"] = None
        if policy == "numeric_field_tolerance":
            with (capture / artifact["path"]).open(newline="") as handle:
                artifact["expected_rows"] = list(csv.DictReader(handle))


def apply_cross_platform_selection_policy(manifest, capture):
    policies = {
        "selection_risk/selection_risk_report.md": ("presence_only", None),
        "selection_risk/candidate_rank_stability.csv": ("numeric_field_tolerance", {"is_oos_spearman_rank_correlation": 1e-15}),
    }
    for artifact in manifest["outputs"]["artifacts"]:
        if artifact["path"] not in policies:
            continue
        policy, tolerance = policies[artifact["path"]]
        artifact["reproducibility_level"] = "methodological"
        artifact["comparison_policy"] = policy
        artifact["tolerance"] = tolerance
        artifact["sha256"] = None
        artifact["semantic_sha256"] = None
        if policy == "numeric_field_tolerance":
            with (capture / artifact["path"]).open(newline="") as handle:
                artifact["expected_rows"] = list(csv.DictReader(handle))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build-reproduce-capture")
    parser.add_argument("--output", type=Path, default=ROOT / "manifests")
    parser.add_argument("--capture-directory", type=Path, default=ROOT / "results/reproducibility_capture")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.capture_directory = args.capture_directory.resolve()
    build = (ROOT / args.build).resolve()
    subprocess.run(["cmake", "-S", str(ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release", "-DQUANT_ENABLE_STRICT_WARNINGS=ON"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel"], check=True)
    args.output.mkdir(parents=True, exist_ok=True)
    for path in args.output.glob("*.json"):
        path.unlink()
    children = []
    for index, definition in enumerate(definitions()):
        identifier, package_type, _, _ = definition
        manifest = make_manifest(*definition, build / "quant_cli")
        reconstruct(ROOT, manifest, args.capture_directory / identifier, build, "serial", 1,
                    allow_compatible_environment=True, allow_dirty=True, build=False, capture_expected=True)
        apply_cross_platform_statistical_policy(manifest, args.capture_directory / identifier)
        apply_cross_platform_selection_policy(manifest, args.capture_directory / identifier)
        path = args.output / f"{identifier}.json"
        path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        children.append({"id": identifier, "manifest": str(path.relative_to(ROOT))})
        print(path)
    selection_children = [item for item in children if "selection_risk_" in item["id"]]
    selection_suite = {"manifest_schema_version": 1, "package_type": "suite", "suite_id": "public_synthetic_selection_risk_suite",
             "description": "Public synthetic seven-package selection-risk reconstruction plan", "children": selection_children,
             "source_commit": SOURCE_COMMIT, "default_execution_mode": "serial", "default_threads": 1}
    selection_suite["suite_id_hash"] = "sha256:" + __import__("hashlib").sha256(json.dumps(selection_suite, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    (args.output / "public_synthetic_selection_risk_suite.json").write_text(json.dumps(selection_suite, indent=2, sort_keys=True) + "\n")
    suite = {"manifest_schema_version": 1, "package_type": "suite", "suite_id": "public_reproducibility_suite",
             "description": "Complete offline public synthetic canonical reconstruction plan", "children": children,
             "source_commit": SOURCE_COMMIT, "default_execution_mode": "serial", "default_threads": 1}
    suite["suite_id_hash"] = "sha256:" + __import__("hashlib").sha256(json.dumps(suite, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    (args.output / "public_reproducibility_suite.json").write_text(json.dumps(suite, indent=2, sort_keys=True) + "\n")
    print(args.output / "public_reproducibility_suite.json")

if __name__ == "__main__":
    main()
