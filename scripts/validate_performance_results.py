#!/usr/bin/env python3
import csv,json,math,sys
from pathlib import Path

directory=Path(sys.argv[1]) if len(sys.argv)>1 else Path("results/performance")
required={"benchmark_environment.json","benchmark_workloads.csv","baseline_profile_summary.csv","before_after_benchmarks.csv",
          "thread_scaling.csv","seven_package_benchmarks.csv","cache_statistics.csv","memory_summary.csv",
          "output_equivalence.csv","performance_manifest.json"}
required.update({"optimized_profile_summary.csv","performance_report.md","seven_package_benchmarks.csv"})
missing=required-{p.name for p in directory.glob("*")}
errors=[]
if missing: errors.append("missing: "+", ".join(sorted(missing)))
try: manifest=json.loads((directory/"performance_manifest.json").read_text())
except Exception as error: manifest={}; errors.append("invalid manifest: "+str(error))
if manifest.get("schema_version")!=1: errors.append("invalid schema version")
for filename in ("benchmark_workloads.csv","before_after_benchmarks.csv","thread_scaling.csv","seven_package_benchmarks.csv"):
    if not (directory/filename).exists(): continue
    rows=list(csv.DictReader((directory/filename).open()))
    if not rows: errors.append(filename+" empty")
    for row in rows:
        for key,value in row.items():
            if key.endswith("seconds") or key in {"speedup","efficiency"}:
                try:
                    if not math.isfinite(float(value)) or float(value)<=0: errors.append(filename+" invalid "+key)
                except ValueError: errors.append(filename+" nonnumeric "+key)
if (directory/"output_equivalence.csv").exists():
    if any(row["result"]!="byte_identical" for row in csv.DictReader((directory/"output_equivalence.csv").open())): errors.append("output equivalence failed")
if errors:
    print("Performance validation failed:"); [print("-",error) for error in sorted(set(errors))]; raise SystemExit(1)
print("Performance validation passed.")
