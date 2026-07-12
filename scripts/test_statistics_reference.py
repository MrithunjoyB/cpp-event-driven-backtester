from __future__ import annotations
import csv, math, statistics, sys
from pathlib import Path

def q(x,p):
    x=sorted(x); return x[int(p*(len(x)-1))]
def main():
    root=Path(sys.argv[1] if len(sys.argv)>1 else "test_results/schema_v3/statistics")
    metrics=list(csv.DictReader((root/"bootstrap_metric_distributions.csv").open()))
    paths=list(csv.DictReader((root/"bootstrap_paths_sample.csv").open()))
    summary={r["metric"]:r for r in csv.DictReader((root/"bootstrap_summary.csv").open())}
    first=[float(r["return"]) for r in paths if r["path"]=="0"]
    ann=float(metrics[0]["annualization_method"].split("=")[-1]); sd=statistics.stdev(first)
    sharpe=statistics.mean(first)*math.sqrt(ann)/sd if sd else 0
    assert abs(sharpe-float(metrics[0]["sharpe"]))<1e-5
    cumulative=[float(r["cumulative_return"]) for r in metrics]; conf=float(metrics[0]["confidence_level"]); a=(1-conf)/2
    assert abs(q(cumulative,a)-float(summary["cumulative_return"]["lower_bound"]))<1e-5
    assert abs(q(cumulative,1-a)-float(summary["cumulative_return"]["upper_bound"]))<1e-5
    assert len(first)==int(metrics[0]["observation_count"])
    print("statistical Python reference checks passed")
    return 0
if __name__=="__main__": raise SystemExit(main())
