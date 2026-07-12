from __future__ import annotations
import argparse
from pathlib import Path
import matplotlib;matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd
def main():
 p=argparse.ArgumentParser();p.add_argument("--directory",default="results/research_v3/portfolio_equal_weight/statistics");a=p.parse_args();root=Path(a.directory);figs=root/"figures";figs.mkdir(parents=True,exist_ok=True)
 d=pd.read_csv(root/"bootstrap_metric_distributions.csv")
 for col,title in [("terminal_wealth","Terminal Wealth"),("sharpe","Sharpe Ratio"),("max_drawdown","Maximum Drawdown"),("active_return","Active Return")]:
  fig,ax=plt.subplots(figsize=(7,4));ax.hist(d[col],bins=35,color="#287271",alpha=.85);ax.axvline(d[col].median(),color="black",ls="--");ax.set(title=f"Moving-block Bootstrap: {title}",xlabel=col,ylabel="Frequency");fig.tight_layout();fig.savefig(figs/f"{col}_distribution.png",dpi=160);plt.close(fig)
 paths=pd.read_csv(root/"bootstrap_paths_sample.csv");fig,ax=plt.subplots(figsize=(9,5))
 for _,g in paths.groupby("path"): ax.plot((1+g["return"]).cumprod().values,color="#3969ac",alpha=.18)
 ax.set(title="Sample Bootstrap Wealth Paths",xlabel="Observation",ylabel="Wealth multiplier");fig.tight_layout();fig.savefig(figs/"confidence_band_equity.png",dpi=160);plt.close(fig)
 print(f"Saved statistical figures to {figs.resolve()}");return 0
if __name__=="__main__":raise SystemExit(main())
