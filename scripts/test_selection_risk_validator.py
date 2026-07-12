#!/usr/bin/env python3
import csv
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root / "results/research_v3/selection_risk/ma/selection_risk"
with tempfile.TemporaryDirectory() as temp:
    target = Path(temp) / "selection_risk"
    shutil.copytree(source, target)
    path = target / "candidate_eligibility.csv"
    rows = list(csv.DictReader(path.open()))
    rows[0]["selected"] = "1"
    rows[0]["eligible"] = "0"
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)
    manifest = json.loads((target / "selection_risk_manifest.json").read_text())
    manifest["bootstrap_method"] = "iid_comparison"
    (target / "selection_risk_manifest.json").write_text(json.dumps(manifest))
    result = subprocess.run(["python3", str(root / "scripts/validate_selection_risk.py"), str(target)], capture_output=True, text=True)
    if result.returncode == 0 or "selected candidate marked ineligible" not in result.stdout or "moving-block" not in result.stdout:
        raise SystemExit("selection-risk validator failed to reject corruption\n" + result.stdout)
print("selection-risk validator rejection test passed")
