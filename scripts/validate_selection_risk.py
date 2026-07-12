#!/usr/bin/env python3
import argparse
import csv
import json
import math
from collections import Counter, defaultdict
from pathlib import Path

REQUIRED = {
    "candidate_definitions.csv", "candidate_eligibility.csv", "candidate_window_metrics.csv",
    "candidate_oos_returns.csv", "candidate_active_returns.csv", "candidate_selection_history.csv",
    "candidate_parameter_frequency.csv", "candidate_rank_stability.csv", "parameter_transition_matrix.csv",
    "neighbourhood_sensitivity.csv", "is_oos_degradation.csv", "family_selection_risk.csv",
    "cross_family_selection_risk.csv", "multiple_testing_summary.csv",
    "multiple_testing_bootstrap_distribution.csv", "selection_risk_warnings.csv",
    "selection_risk_manifest.json",
    "parameter_value_frequency.csv", "family_selection_frequency.csv", "regime_selection_risk.csv",
    "selected_deployable_oos_returns.csv",
}

def rows(path):
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))

def finite(value):
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False

def validate(directory):
    errors = []
    missing = sorted(REQUIRED - {p.name for p in directory.iterdir() if p.is_file()})
    if missing:
        return ["missing required outputs: " + ", ".join(missing)]
    try:
        manifest = json.loads((directory / "selection_risk_manifest.json").read_text())
    except (OSError, json.JSONDecodeError) as exc:
        return [f"malformed selection-risk manifest: {exc}"]
    for key in ("schema_version", "experiment_id", "history_policy", "benchmark", "seed",
                "simulation_count", "block_length", "bootstrap_method", "null_hypothesis"):
        if key not in manifest:
            errors.append(f"manifest missing {key}")
    if not manifest.get("git_commit_hash"):
        errors.append("manifest missing git_commit_hash")
    if manifest.get("schema_version") != 3:
        errors.append("manifest schema_version must be 3")
    if manifest.get("history_policy") != "candidate_normalized_window_diagnostic":
        errors.append("candidate diagnostic history is mislabeled")
    if manifest.get("bootstrap_method") != "moving_block_circular":
        errors.append("moving-block bootstrap must be the primary method")
    if int(manifest.get("simulation_count", 0)) <= 0 or int(manifest.get("block_length", 0)) <= 0:
        errors.append("invalid bootstrap metadata")

    definitions = rows(directory / "candidate_definitions.csv")
    ids = [r.get("candidate_id", "") for r in definitions]
    if not ids or any(not value for value in ids): errors.append("missing candidate ID")
    if len(ids) != len(set(ids)): errors.append("duplicate candidate IDs")
    if any(not r.get("parameter_serialization") for r in definitions): errors.append("missing parameter serialization")
    if int(manifest.get("candidate_count", -1)) != len(definitions): errors.append("manifest candidate-count mismatch")

    eligibility = rows(directory / "candidate_eligibility.csv")
    metrics = rows(directory / "candidate_window_metrics.csv")
    eligibility_keys = {(r["ticker"], r["strategy_family"], r["window_id"], r["candidate_id"]) for r in eligibility}
    metric_keys = {(r["ticker"], r["strategy_family"], r["window_id"], r["candidate_id"]) for r in metrics}
    if eligibility_keys != metric_keys: errors.append("eligibility/window-metric candidate mismatch")
    selected = defaultdict(int)
    for row in eligibility:
        key = (row["ticker"], row["strategy_family"], row["window_id"])
        flag = row.get("selected")
        if flag not in {"0", "1"}: errors.append("invalid selected flag")
        if flag == "1":
            selected[key] += 1
            if row.get("eligible") != "1": errors.append("selected candidate marked ineligible")
        if row["train_end"] >= row["test_start"]: errors.append("training/test overlap")
        if row["test_start"] > row["test_end"]: errors.append("inconsistent test boundaries")
    if any(count > 1 for count in selected.values()): errors.append("more than one selected candidate per family/window")
    if any(key not in selected for key in {(r["ticker"], r["strategy_family"], r["window_id"]) for r in eligibility}):
        errors.append("window has no eligible selected candidate")

    observations = rows(directory / "candidate_oos_returns.csv")
    seen = set()
    last = {}
    for row in observations:
        key = (row["candidate_id"], row["date"])
        if key in seen: errors.append("duplicate candidate/date row")
        seen.add(key)
        if row["candidate_id"] in last and row["date"] <= last[row["candidate_id"]]: errors.append("non-chronological OOS dates")
        last[row["candidate_id"]] = row["date"]
        for field in ("daily_return", "benchmark_return", "active_return", "diagnostic_value"):
            if not finite(row.get(field)): errors.append(f"non-finite {field}")
        if row.get("history_type") != "counterfactual_diagnostic" or row.get("continuity_type") != "normalized_window":
            errors.append("diagnostic history presented as deployable capital")
    if len(observations) != int(manifest.get("observation_rows", -1)): errors.append("manifest observation-count mismatch")
    if (directory / "candidate_oos_returns.csv").read_bytes() != (directory / "candidate_active_returns.csv").read_bytes():
        errors.append("candidate active-return export mismatch")
    deployable_seen = set()
    for row in rows(directory / "selected_deployable_oos_returns.csv"):
        key = (row["ticker"], row["strategy_family"], row["date"])
        if key in deployable_seen: errors.append("duplicate deployable selected-strategy date")
        deployable_seen.add(key)
        if row.get("history_type") != "deployable_selected_strategy" or row.get("continuity_type") != "continuous_capital":
            errors.append("selected-strategy history is not labelled deployable continuous capital")
        if not finite(row.get("daily_return")) or not finite(row.get("portfolio_value")):
            errors.append("non-finite deployable selected-strategy history")

    for filename in ("family_selection_risk.csv", "cross_family_selection_risk.csv", "multiple_testing_summary.csv"):
        for row in rows(directory / filename):
            if row.get("status") == "not_applicable":
                continue
            if not finite(row.get("adjusted_p_value")) or not 0 <= float(row["adjusted_p_value"]) <= 1:
                errors.append("adjusted p-value outside [0,1]")
            if int(row["eligible_candidates"]) > int(row["configured_candidates"]): errors.append("eligible candidate count exceeds configured count")
            if int(row["common_observations"]) < 30: errors.append("insufficient common observations")
            if row.get("method") != "centered_moving_block_reality_check": errors.append("incorrect multiple-testing method")
    for row in rows(directory / "candidate_rank_stability.csv"):
        if not finite(row.get("is_oos_spearman_rank_correlation")) or not -1 <= float(row["is_oos_spearman_rank_correlation"]) <= 1:
            errors.append("invalid IS/OOS rank correlation")
    for row in rows(directory / "regime_selection_risk.csv"):
        if not finite(row.get("adjusted_p_value")) or not 0 <= float(row["adjusted_p_value"]) <= 1:
            errors.append("invalid regime adjusted p-value")
    return sorted(set(errors))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    errors = validate(args.directory)
    if errors:
        for error in errors: print("ERROR:", error)
        raise SystemExit(1)
    print("Selection-risk validation passed.")

if __name__ == "__main__": main()
