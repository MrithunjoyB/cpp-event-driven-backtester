from __future__ import annotations
import subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
  with tempfile.TemporaryDirectory() as t:
    d=Path(t)/"statistics";d.mkdir();(d/"statistical_manifest.json").write_text("{}")
    (d/"bootstrap_summary.csv").write_text("schema_version,experiment_id,method,seed,simulation_count,block_length,input_series,benchmark,confidence_level,candidate_count,observation_count,annualization_method,metric,mean,median,standard_deviation,lower_bound,upper_bound,probability\n3,bad,moving_block_circular,1,10,99,normalized_window_oos,SPY,0.95,1,5,configured,x,0,0,0,1,-1,2\n")
    p=subprocess.run(["python3",str(ROOT/"scripts/validate_results.py"),t],capture_output=True,text=True)
    assert p.returncode!=0 and "normalized-window" in p.stdout and "missing required statistical files" in p.stdout
  print("statistical validator rejection test passed");return 0
if __name__=="__main__":raise SystemExit(main())
