#!/usr/bin/env python3
import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

parser = argparse.ArgumentParser()
parser.add_argument("--directory", required=True, type=Path)
args = parser.parse_args()
out = args.directory / "figures"
out.mkdir(exist_ok=True)

def save(name, title, xlabel="", ylabel=""):
    plt.title(title); plt.xlabel(xlabel); plt.ylabel(ylabel); plt.tight_layout()
    plt.savefig(out / name, dpi=180); plt.close()

summary = pd.read_csv(args.directory / "multiple_testing_summary.csv")
if not summary.empty:
    labels = summary["ticker"] + " / " + summary["strategy_family"]
    plt.figure(figsize=(10, 5)); plt.barh(labels, summary["adjusted_p_value"]); plt.axvline(0.05, color="black", linestyle="--")
    save("family_corrected_p_values.png", "Family-wise corrected p-values", "Adjusted p-value")
    plt.figure(figsize=(10, 5)); plt.scatter(summary["observed_best_mean_active_return"], summary["adjusted_p_value"])
    for x, y, label in zip(summary["observed_best_mean_active_return"], summary["adjusted_p_value"], labels): plt.annotate(label, (x, y), fontsize=6)
    save("adjusted_vs_unadjusted_evidence.png", "Observed best active return versus corrected evidence", "Best mean active return", "Adjusted p-value")

frequency = pd.read_csv(args.directory / "candidate_parameter_frequency.csv")
top = frequency.sort_values("selection_frequency", ascending=False).head(25)
plt.figure(figsize=(10, 6)); plt.barh(top["ticker"] + " / " + top["parameter_serialization"], top["selection_frequency"])
save("parameter_selection_frequency.png", "Highest candidate selection frequencies", "Selection frequency")

selection = pd.read_csv(args.directory / "candidate_selection_history.csv")
if not selection.empty:
    selection["label"] = selection["ticker"] + " / " + selection["strategy_family"]
    plt.figure(figsize=(10, 5))
    for label, group in selection.groupby("label"): plt.plot(group["window_id"], group["training_rank"], marker="o", label=label)
    plt.legend(fontsize=6, ncol=2); save("selected_candidate_history.png", "Causal selected-candidate history", "Window", "Training rank")

degradation = pd.read_csv(args.directory / "is_oos_degradation.csv")
if not degradation.empty:
    plt.figure(figsize=(7, 6)); plt.scatter(degradation["mean_is_return"], degradation["mean_oos_active_return"], alpha=.6)
    save("is_oos_degradation.png", "In-sample return versus OOS active return", "Mean IS return", "Mean OOS active return")

ranks = pd.read_csv(args.directory / "candidate_rank_stability.csv")
if not ranks.empty:
    plt.figure(figsize=(7, 6)); plt.scatter(ranks["average_training_rank"], ranks["average_oos_active_return"], alpha=.6)
    save("is_oos_rank_scatter.png", "Training rank versus OOS active return", "Average training rank", "Average OOS active return")

neighbours = pd.read_csv(args.directory / "neighbourhood_sensitivity.csv")
if not neighbours.empty:
    plt.figure(figsize=(8, 5)); plt.hist(neighbours["oos_active_return_difference"], bins=20)
    save("neighbourhood_sensitivity.png", "Selected-minus-neighbour OOS active-return differences", "Difference", "Count")

bootstrap = pd.read_csv(args.directory / "multiple_testing_bootstrap_distribution.csv")
if not bootstrap.empty:
    first = bootstrap.groupby(["ticker", "strategy_family"]).head(1000)
    plt.figure(figsize=(8, 5)); plt.hist(first["max_centered_mean_active_return"], bins=35)
    save("best_candidate_bootstrap_distribution.png", "Centered max-statistic bootstrap distribution", "Maximum centered mean active return", "Count")

print(f"Selection-risk figures written to {out}")
