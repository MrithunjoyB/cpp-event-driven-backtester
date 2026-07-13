#!/usr/bin/env python3
import argparse
import csv
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VOLATILE = {"parallel_execution_metadata.json", "performance_counters.csv"}
PACKAGES = {
    "ma": "configs/selection_risk_ma.json",
    "rsi": "configs/selection_risk_rsi.json",
    "macd": "configs/selection_risk_macd.json",
    "volatility_breakout": "configs/selection_risk_volatility_breakout.json",
    "all_families": "configs/selection_risk_all.json",
    "cost_zero": "configs/selection_risk_all_zero_cost.json",
    "cost_high": "configs/selection_risk_all_high_cost.json",
}

def snapshot(directory):
    result = {}
    for path in sorted(p for p in directory.rglob("*") if p.is_file() and p.name not in VOLATILE):
        relative = str(path.relative_to(directory))
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        rows = None
        if path.suffix == ".csv":
            with path.open(newline="") as handle:
                rows = sum(1 for _ in csv.reader(handle))
        result[relative] = {"sha256": digest, "rows": rows}
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build")
    parser.add_argument("--threads", default="1,2,4,8")
    parser.add_argument("--packages", default=",".join(PACKAGES))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    cli = ROOT / args.build / "quant_cli"
    thread_counts = [int(value) for value in args.threads.split(",")]
    packages = args.packages.split(",")
    if not thread_counts or thread_counts[0] != 1:
        raise SystemExit("thread list must begin with the serial reference 1")
    rows = []
    for package in packages:
        config = PACKAGES.get(package)
        if config is None:
            raise SystemExit(f"unknown package: {package}")
        output_dir = ROOT / "results/public_synthetic/selection_risk" / package / "selection_risk"
        reference = None
        for threads in thread_counts:
            mode = "serial" if threads == 1 else "parallel"
            subprocess.run([str(cli), "run", "--config", config, "--execution-mode", mode,
                            "--threads", str(threads)], cwd=ROOT, stdout=subprocess.DEVNULL, check=True)
            current = snapshot(output_dir)
            if reference is None:
                reference = current
            if current != reference:
                missing = sorted(set(reference) - set(current))
                extra = sorted(set(current) - set(reference))
                changed = sorted(key for key in set(reference) & set(current) if reference[key] != current[key])
                raise SystemExit(f"{package} differs at {threads} threads: missing={missing}, extra={extra}, changed={changed}")
            tree = hashlib.sha256(json.dumps(current, sort_keys=True).encode()).hexdigest()
            rows.append({"package": package, "mode": mode, "threads": threads,
                         "canonical_files": len(current), "canonical_tree_hash": tree,
                         "result": "byte_identical"})
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
            writer.writeheader(); writer.writerows(rows)
    print(f"Parallel equivalence passed: {len(packages)} packages across {len(thread_counts)} execution settings")

if __name__ == "__main__":
    main()
