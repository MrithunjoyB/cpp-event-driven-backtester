#!/usr/bin/env python3
from __future__ import annotations

import csv
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cases = 0


def check(value: bool, name: str) -> None:
    global cases
    cases += 1
    if not value:
        raise AssertionError(name)


subprocess.run([sys.executable, str(ROOT / "scripts/run_final_audit.py")], cwd=ROOT, check=True)
subprocess.run([sys.executable, str(ROOT / "scripts/validate_final_audit.py"), str(ROOT / "audit/final")], cwd=ROOT, check=True)

decision = "A. RNG MIGRATION COMPLETE — PASS TO FINAL RELEASE ENGINEERING"
manifest = json.loads((ROOT / "audit/final/audit_manifest.json").read_text())
findings = list(csv.DictReader((ROOT / "audit/final/audit_findings.csv").open()))
thresholds = list(csv.DictReader((ROOT / "audit/final/threshold_stability.csv").open()))
tolerances = list(csv.DictReader((ROOT / "audit/final/tolerance_review.csv").open()))
source = "\n".join(path.read_text() for path in (ROOT / "src").rglob("*.cpp"))
generator = (ROOT / "scripts/generate_reproducibility_manifests.py").read_text()

check(manifest["decision"] == decision, "closure decision")
check(manifest["audit_schema_version"] == 2, "closure schema")
check(not any(row["severity"] in {"Critical", "High"} and row["status"] != "resolved" for row in findings), "no unresolved High")
check(sum(row["severity"] == "High" and row["status"] == "resolved" for row in findings) == 2, "two High closures")
check("std::uniform_int_distribution" not in source, "production distribution removed")
check("portable_bounded_v1" in source, "stable mapping used")
check("shape_only" not in generator, "raw stochastic shape policy removed")
check("adjusted_p_value\": 0.05" not in generator, "broad p tolerance removed")
check(all(row["migrated_band"] for row in thresholds), "migrated bands recorded")
check(all(row["retained_tolerance"] not in {"0.03", "0.04", "0.05", "0.06"} for row in tolerances), "unsafe tolerance absent")
check((ROOT / "tests/fixtures/rng/portable_bounded_v1.csv").is_file(), "golden vectors present")
check(sum(1 for _ in (ROOT / "tests/fixtures/rng/portable_bounded_v1.csv").open()) == 8705, "golden vector count")
check((ROOT / "docs/RNG_METHODOLOGY.md").is_file(), "RNG methodology documented")
check((ROOT / "audit/final/release_blockers.md").read_text().find("No Critical or High") >= 0, "blockers closed")
check((ROOT / "audit/final/final_audit_report.md").read_text().find(decision) >= 0, "report decision")
check(all(row["closure_test"] for row in findings), "closure tests recorded")
check(any(row["result"] == "tsla_macd_zero_cost" for row in thresholds), "zero-cost reassessed")
check(any(row["result"] == "momentum_probability_sharpe_positive" for row in thresholds), "Momentum reassessed")
check(any(row["result"] == "tsla_regime_bull_low_volatility" and row["release_relevant"] == "0" for row in thresholds), "regime remains exploratory")
check(manifest["migration_implementation_commit"] == "c21852e", "migration implementation identity")

if cases != 20:
    raise AssertionError(f"expected 20 audit closure cases, observed {cases}")
print(f"{cases} final audit closure cases passed")
