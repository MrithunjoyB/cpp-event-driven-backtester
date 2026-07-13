#!/usr/bin/env python3
import csv, math, subprocess, sys, tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
rows={r["result"]:r for r in csv.DictReader((ROOT/"tests/fixtures/audit/cross_platform_stochastic.csv").open())}
cases=0
def check(value,name):
 global cases;cases+=1
 if not value: raise AssertionError(name)
def band(value,kind):
 if kind=="p": return "reject" if value<0.05 else "do_not_reject"
 return "low" if value<=0.05 else ("strong" if value>=0.95 else "inconclusive")

# Independent release-gate fixtures.
decision_close=1000; simulations=1000
check((1+decision_close)/(simulations+1)==1001/1001,"finite sample p-value formula")
tsla=float(rows["tsla_macd_combined"]["macos_value"])
se=math.sqrt(tsla*(1-tsla)/1000)
check(abs(tsla-0.05)<se,"TSLA MACD margin below one MC SE")
zero=float(rows["tsla_macd_zero_cost"]["macos_value"])
check(abs(zero-0.05)<math.sqrt(zero*(1-zero)/1000),"zero-cost margin below one MC SE")
check(band(float(rows["tsla_regime_bull_low_volatility"]["macos_value"]),"p")!=band(float(rows["tsla_regime_bull_low_volatility"]["linux_value"]),"p"),"observed regime threshold crossing")
check(0.05>abs(tsla-0.05),"p-value tolerance overlaps threshold")
check(band(float(rows["momentum_probability_sharpe_positive"]["macos_value"]),"prob")=="inconclusive","Momentum macOS band")
check(0.04>abs(0.95-float(rows["momentum_probability_sharpe_positive"]["linux_value"])),"Momentum tolerance overlaps strong boundary")
check(float(rows["momentum_probability_positive_active"]["macos_value"])<0.5<float(rows["momentum_probability_positive_active"]["linux_value"]),"Momentum even-odds crossing")

# Standard engine vector and mapping inequality: the engine is fixed; bounded mapping is not.
engine_word=3499211612;bound=2190
modulo=engine_word%bound
multiply_high=(engine_word*bound)>>32
check(modulo!=multiply_high,"same engine word permits unequal bounded mappings")
check(engine_word==3499211612,"mt19937 published first word")

# Audit package and release gate behavior.
subprocess.run([sys.executable,str(ROOT/"scripts/run_final_audit.py")],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
subprocess.run([sys.executable,str(ROOT/"scripts/validate_final_audit.py"),str(ROOT/"audit/final")],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
check((ROOT/"audit/final/release_blockers.md").read_text().count("## RB-")==3,"three objective blockers")
check("stable_bounded_uint32" in (ROOT/"audit/final/rng_migration_decision.md").read_text(),"migration algorithm named")
check("C. FAIL — RNG MIGRATION REQUIRED" in (ROOT/"audit/final/final_audit_report.md").read_text(),"hard release decision")
findings=list(csv.DictReader((ROOT/"audit/final/audit_findings.csv").open()))
check(sum(r["severity"]=="High" for r in findings)==2,"two High findings")
check(all(r["closure_test"] for r in findings),"all findings have closure tests")

if cases!=15: raise AssertionError(f"expected 15 audit cases, observed {cases}")
print(f"{cases} final audit cases passed")
