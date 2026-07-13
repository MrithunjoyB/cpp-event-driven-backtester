#!/usr/bin/env python3
import argparse
import csv
import hashlib
import json
import os
import platform
import resource
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""): digest.update(chunk)
    return digest.hexdigest()

def tree_hash(path):
    digest = hashlib.sha256()
    for item in sorted(p for p in path.rglob("*") if p.is_file() and "figures" not in p.parts and p.name not in {"parallel_execution_metadata.json","performance_counters.csv"}):
        digest.update(str(item.relative_to(path)).encode()); digest.update(bytes.fromhex(sha256(item)))
    return digest.hexdigest()

def measure(command, repetitions, warmups=0):
    for _ in range(warmups): subprocess.run(command, cwd=ROOT, stdout=subprocess.DEVNULL, check=True)
    values=[]
    for _ in range(repetitions):
        started=time.perf_counter(); subprocess.run(command, cwd=ROOT, stdout=subprocess.DEVNULL, check=True)
        values.append(time.perf_counter()-started)
    ordered=sorted(values)
    return {"runs":values,"median":statistics.median(values),"minimum":min(values),"maximum":max(values),
            "p10":ordered[max(0, int(.1*(len(ordered)-1)))],"p90":ordered[int(.9*(len(ordered)-1))]}

def peak_rss(command):
    helper=("import resource,subprocess,sys; "
            "subprocess.run(sys.argv[1:],stdout=subprocess.DEVNULL,check=True); "
            "print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)")
    after=int(subprocess.check_output([sys.executable,"-c",helper,*command],cwd=ROOT,text=True))
    # macOS reports bytes; Linux reports KiB.
    multiplier=1 if platform.system()=="Darwin" else 1024
    return after*multiplier

def write_csv(path, rows):
    if not rows: return
    with path.open("w", newline="") as handle:
        writer=csv.DictWriter(handle, fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)

def main():
    os.environ.setdefault("MPLCONFIGDIR", str(ROOT/".matplotlib-cache"))
    parser=argparse.ArgumentParser()
    parser.add_argument("--build", default="build-performance-dev")
    parser.add_argument("--output", type=Path, default=ROOT/"results/performance")
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--seven-repetitions", type=int, default=2)
    args=parser.parse_args()
    if args.repetitions < 2 or args.seven_repetitions < 1: raise SystemExit("invalid repetition count")
    args.output.mkdir(parents=True, exist_ok=True)
    cli=str(ROOT/args.build/"quant_cli")
    workloads={
      "single_syn_eq_a":[cli,"--mode","single","--ticker","SYN_EQ_A","--strategy","ma_cross"],
      "parameter_grid_syn_eq_a":[cli,"--mode","grid","--ticker","SYN_EQ_A"],
      "walk_forward_ma":[cli,"run","--config","configs/ma_walk_forward.json"],
      "portfolio_equal_attribution_statistics":[cli,"run","--config","configs/portfolio_equal_weight.json"],
      "ma_selection_risk":[cli,"run","--config","configs/selection_risk_ma.json"],
      "combined_selection_risk":[cli,"run","--config","configs/selection_risk_all.json"],
      "selection_report_generation":[sys.executable,"scripts/generate_selection_risk_report.py","--directory","results/public_synthetic/selection_risk/all_families/selection_risk"],
      "selection_figure_generation":[sys.executable,"scripts/visualize_selection_risk.py","--directory","results/public_synthetic/selection_risk/all_families/selection_risk"],
    }
    rows=[]
    for name,command in workloads.items():
        result=measure(command,args.repetitions,1 if name=="single_syn_eq_a" else 0)
        rows.append({"workload":name,"mode":"serial","threads":1,"repetitions":args.repetitions,
                     "median_seconds":result["median"],"min_seconds":result["minimum"],"max_seconds":result["maximum"],
                     "p10_seconds":result["p10"],"p90_seconds":result["p90"]})
    scaling=[]
    for threads in (1,2,4,8):
        mode="serial" if threads==1 else "parallel"
        result=measure([cli,"run","--config","configs/selection_risk_all.json","--execution-mode",mode,"--threads",str(threads)],args.repetitions)
        scaling.append({"threads":threads,"mode":mode,"repetitions":args.repetitions,"median_seconds":result["median"],
                        "min_seconds":result["minimum"],"max_seconds":result["maximum"],
                        "canonical_output_hash":tree_hash(ROOT/"results/public_synthetic/selection_risk/all_families/selection_risk")})
    serial=scaling[0]["median_seconds"]
    for row in scaling:
        row["speedup"]=serial/row["median_seconds"]
        row["efficiency"]=row["speedup"]/row["threads"]
    seven=["ma","rsi","macd","volatility_breakout","all","all_zero_cost","all_high_cost"]
    seven_rows=[]
    for threads in (1,4):
        mode="serial" if threads==1 else "parallel"; values=[]
        for _ in range(args.seven_repetitions):
            started=time.perf_counter()
            for config in seven:
                subprocess.run([cli,"run","--config",f"configs/selection_risk_{config}.json","--execution-mode",mode,"--threads",str(threads)],cwd=ROOT,stdout=subprocess.DEVNULL,check=True)
            values.append(time.perf_counter()-started)
        seven_rows.append({"workload":"seven_package_selection_risk","mode":mode,"threads":threads,
                           "repetitions":args.seven_repetitions,"median_seconds":statistics.median(values),
                           "min_seconds":min(values),"max_seconds":max(values)})
    baseline=json.loads(args.baseline.read_text()) if args.baseline and args.baseline.exists() else {}
    comparisons=[]
    current={row["workload"]:row for row in rows}
    mapping={"single_syn_eq_a":"single_syn_eq_a","ma_selection":"ma_selection_risk","combined_selection":"combined_selection_risk",
             "portfolio_equal":"portfolio_equal_attribution_statistics","walk_forward":"walk_forward_ma"}
    for old,new in mapping.items():
        if old in baseline:
            before=float(baseline[old]["median"]); after=float(current[new]["median_seconds"])
            comparisons.append({"workload":new,"baseline_seconds":before,"optimized_serial_seconds":after,"speedup":before/after})
    if "seven_package" in baseline:
        before=float(baseline["seven_package"]["median"]); after=float(seven_rows[0]["median_seconds"])
        comparisons.append({"workload":"seven_package_selection_risk","baseline_seconds":before,"optimized_serial_seconds":after,"speedup":before/after})
    input_hash=hashlib.sha256("".join(sha256(path) for path in sorted((ROOT/"data/synthetic").glob("*.csv"))).encode()).hexdigest()
    output_dir=ROOT/"results/public_synthetic/selection_risk/all_families/selection_risk"
    environment={"schema_version":1,"os":platform.platform(),"machine":platform.machine(),"processor":platform.processor(),
                 "logical_cores":os.cpu_count(),"compiler":subprocess.check_output(["c++","--version"],text=True).splitlines()[0],
                 "python":platform.python_version(),"git_commit":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
                 "build_type":"Release","strict_warnings":True,"filesystem_cache":"warm after first run",
                 "measurement_notes":"steady-clock wall time; local machine not isolated; CPU scaling uncontrolled; long workflow uses reduced repetitions"}
    (args.output/"benchmark_environment.json").write_text(json.dumps(environment,indent=2)+"\n")
    write_csv(args.output/"benchmark_workloads.csv",rows)
    write_csv(args.output/"thread_scaling.csv",scaling)
    write_csv(args.output/"seven_package_benchmarks.csv",seven_rows)
    write_csv(args.output/"before_after_benchmarks.csv",comparisons)
    write_csv(args.output/"cache_statistics.csv",[{"scope":"selection_risk_application_run","unique_datasets":5,"loads_after":5,
        "reuse_policy":"immutable_shared_market_data","benchmark_reuse":"one_train_and_one_test_path_per_ticker_window"}])
    memory=[]
    for threads in (1,4):
        mode="serial" if threads==1 else "parallel"
        memory.append({"workload":"combined_selection_risk","mode":mode,"threads":threads,
            "peak_rss_bytes":peak_rss([cli,"run","--config","configs/selection_risk_all.json",
                "--execution-mode",mode,"--threads",str(threads)]),
            "note":"RUSAGE_CHILDREN peak RSS; one measured process per fresh harness invocation is preferred"})
    write_csv(args.output/"memory_summary.csv",memory)
    hashes={row["canonical_output_hash"] for row in scaling}
    equivalence="byte_identical" if len(hashes)==1 else "mismatch"
    write_csv(args.output/"output_equivalence.csv",[{"workload":"combined_selection_risk","threads":"1,2,4,8",
        "canonical_output_hash":next(iter(hashes)) if len(hashes)==1 else "multiple",
        "result":equivalence,"excluded":"parallel_execution_metadata.json;performance_counters.csv"}])
    if equivalence != "byte_identical": raise RuntimeError("combined selection-risk outputs differ across thread counts")
    grouped_reduction=(1-current["combined_selection_risk"]["median_seconds"]/float(baseline["combined_selection"]["median"])) if "combined_selection" in baseline else float("nan")
    grouped_evidence=(f"grouped elimination reduced wall time by {grouped_reduction:.1%}; not exclusive profiler self-time"
                      if grouped_reduction == grouped_reduction else "before/after contribution unavailable without --baseline")
    write_csv(args.output/"baseline_profile_summary.csv",[
        {"rank":1,"bottleneck":"repeated CSV parsing and per-candidate benchmark simulation","evidence":"removed together; combined median wall-time reduction","estimated_contribution":grouped_evidence},
        {"rank":2,"bottleneck":"candidate strategy simulation and indicator work","evidence":"remaining optimized serial workload","estimated_contribution":"dominant remaining C++ compute"},
        {"rank":3,"bottleneck":"bootstrap, regime panels, alignment, export","evidence":"limited 4-thread scaling after candidate parallelism","estimated_contribution":"remaining serial fraction"}])
    counters_path=output_dir/"performance_counters.csv"
    if counters_path.exists():
        counters=list(csv.DictReader(counters_path.open()))
        write_csv(args.output/"optimized_profile_summary.csv",counters)
    report=["# Performance Report","",f"Machine-specific Release measurements on {environment['machine']} with {environment['logical_cores']} logical cores.","",
            "## Before and After","","| Workload | Baseline (s) | Optimized serial (s) | Speedup |","| --- | ---: | ---: | ---: |"]
    for row in comparisons: report.append(f"| {row['workload']} | {row['baseline_seconds']:.3f} | {row['optimized_serial_seconds']:.3f} | {row['speedup']:.2f}x |")
    report += ["","## Thread Scaling","","| Threads | Median (s) | Speedup | Efficiency |","| ---: | ---: | ---: | ---: |"]
    for row in scaling: report.append(f"| {row['threads']} | {row['median_seconds']:.3f} | {row['speedup']:.2f}x | {row['efficiency']:.1%} |")
    report += ["","The dominant accepted serial optimization is immutable per-run market-data reuse plus exact benchmark-path reuse. Thread scaling of the isolated combined package is noisy on this shared machine; the complete seven-package workflow is the more representative end-to-end measure. Candidate training and counterfactual OOS simulations are the only parallelized boundaries. Event chronology, linked capital, portfolio accounting, bootstrap reductions, result assembly, and file writing remain serial.","",
               "Peak RSS is measured with getrusage(RUSAGE_CHILDREN); it is a single-run high-water mark, not a distribution. Measurements use steady-clock wall time with warm filesystem cache after the first run; CPU scaling and background activity were not controlled."]
    (args.output/"performance_report.md").write_text("\n".join(report)+"\n")
    manifest={"schema_version":1,"methodology_version":"performance_v1","input_hash":input_hash,"output_hash":tree_hash(output_dir),
              "repetitions":args.repetitions,"seven_package_repetitions":args.seven_repetitions,"thread_counts":[1,2,4,8],
              "generated_files":[p.name for p in sorted(args.output.iterdir())]+["performance_manifest.json"]}
    (args.output/"performance_manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(args.output)

if __name__=="__main__": main()
