#!/usr/bin/env python3
import csv, hashlib, json, sys
from pathlib import Path

root=Path(sys.argv[1]) if len(sys.argv)>1 else Path("audit/final")
required={"audit_claim_inventory.csv","audit_findings.csv","rng_portability_evidence.csv","threshold_stability.csv",
"tolerance_review.csv","cross_platform_comparison.csv","validator_corruption_results.csv","test_quality_review.csv",
"release_acceptance_criteria.md","rng_migration_decision.md","release_blockers.md","audit_manifest.json","final_audit_report.md"}
missing=required-{p.name for p in root.glob("*")}; errors=[]
if missing: errors.append("missing files: "+", ".join(sorted(missing)))
try: manifest=json.loads((root/"audit_manifest.json").read_text())
except Exception as error: manifest={};errors.append("invalid manifest: "+str(error))
if manifest.get("audit_schema_version")!=2: errors.append("invalid audit schema")
decision="A. RNG MIGRATION COMPLETE — PASS TO FINAL RELEASE ENGINEERING"
if manifest.get("decision")!=decision: errors.append("invalid release decision")
for item in manifest.get("files",[]):
 path=root/item["path"]
 if not path.is_file(): errors.append("manifest file missing: "+item["path"]);continue
 if hashlib.sha256(path.read_bytes()).hexdigest()!=item["sha256"]: errors.append("audit hash mismatch: "+item["path"])
findings=list(csv.DictReader((root/"audit_findings.csv").open())) if (root/"audit_findings.csv").exists() else []
if any(row["severity"] in {"Critical","High"} and row.get("status")!="resolved" for row in findings): errors.append("unresolved Critical/High finding")
for row in findings:
 for key in ("id","evidence","impact","remediation","reproduction","status","closure_test"):
  if not row.get(key): errors.append(f"finding {row.get('id')} missing {key}")
thresholds=list(csv.DictReader((root/"threshold_stability.csv").open())) if (root/"threshold_stability.csv").exists() else []
if any(row.get("migrated_band","")=="" for row in thresholds): errors.append("migrated threshold evidence missing")
for name in ("final_audit_report.md","rng_migration_decision.md"):
 if name in required and (root/name).is_file() and decision not in (root/name).read_text(): errors.append(name+" decision mismatch")
claims=list(csv.DictReader((root/"audit_claim_inventory.csv").open())) if (root/"audit_claim_inventory.csv").exists() else []
for key in ("source_file","documentation","supporting_test","supporting_artifact","independent_audit_method","status"):
 if claims and key not in claims[0]: errors.append("claim inventory missing "+key)
if (root/"final_audit_report.md").is_file() and "## Expert Recommendations" not in (root/"final_audit_report.md").read_text(): errors.append("expert recommendations missing")
if errors:
 print("Final audit validation failed:");[print("-",e) for e in sorted(set(errors))];raise SystemExit(1)
print(f"Final audit validation passed: {len(findings)} findings, {len(thresholds)} threshold cases")
