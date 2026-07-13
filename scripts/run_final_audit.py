#!/usr/bin/env python3
import csv
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "audit/final"
FIXTURE = ROOT / "tests/fixtures/audit/cross_platform_stochastic.csv"

def write_csv(name, rows):
    path = OUT / name
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)

def sha256(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def band(value, threshold, scope):
    if scope == "portfolio_probability":
        if value <= 0.05: return "low_probability"
        if value >= 0.95: return "strong_probability"
        return "inconclusive"
    return "reject" if value < threshold else "do_not_reject"

OUT.mkdir(parents=True, exist_ok=True)
evidence = list(csv.DictReader(FIXTURE.open()))
threshold_rows = []
for row in evidence:
    mac = float(row["macos_value"]); linux = float(row["linux_value"]) if row["linux_value"] else None
    threshold = float(row["decision_threshold"]); n = int(row["simulations"])
    se = math.sqrt(max(mac * (1.0 - mac), 0.0) / n)
    threshold_rows.append({
        "result": row["result"], "scope": row["scope"], "macos_value": row["macos_value"],
        "linux_value": row["linux_value"], "threshold": row["decision_threshold"],
        "macos_band": band(mac, threshold, row["scope"]),
        "linux_band": band(linux, threshold, row["scope"]) if linux is not None else "unavailable",
        "band_crossing": int(linux is not None and band(mac, threshold, row["scope"]) != band(linux, threshold, row["scope"])),
        "binomial_standard_error": f"{se:.9f}", "margin_to_threshold": f"{abs(mac-threshold):.9f}",
        "margin_less_than_one_se": int(abs(mac-threshold) < se),
        "release_relevant": row["release_relevant"], "audit_status": "unstable" if abs(mac-threshold) < se or not row["linux_value"] else "observed_pair_only",
    })
write_csv("threshold_stability.csv", threshold_rows)

claims = [
    ("execution_causality","src/experiments/Backtester.cpp","docs/METHODOLOGY.md","quant_regression_tests","verified","next-open pending-order flow and fixture"),
    ("cost_accounting","src/execution/ExecutionHandler.cpp","docs/METHODOLOGY.md","quant_regression_tests","verified","single commission/slippage application"),
    ("cash_and_long_only","src/portfolio/Portfolio.cpp","docs/METHODOLOGY.md","quant_regression_tests","verified","affordability and invalid-sell fixtures"),
    ("benchmark_parity","src/experiments/Backtester.cpp","docs/METHODOLOGY.md","methodology_tests","verified","cost/timing parity fixtures"),
    ("walk_forward_causality","src/experiments/Analysis.cpp","docs/METHODOLOGY.md","methodology_tests","verified","calendar boundaries and frozen parameters"),
    ("continuous_oos_capital","src/experiments/SelectionRisk.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","separate deployable history"),
    ("candidate_diagnostic_separation","src/experiments/SelectionRisk.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","normalized counterfactual labels"),
    ("common_date_alignment","src/experiments/SelectionRisk.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","strict intersection and duplicate rejection"),
    ("union_calendar","src/portfolio/UnionPortfolioBacktester.cpp","docs/MARKET_CALENDAR.md","union_portfolio_tests","verified","tradability separated from marks"),
    ("corporate_actions","src/market_data/CorporateAction.cpp","docs/CORPORATE_ACTIONS.md","corporate_action_tests","verified","split/dividend and double-count fixtures"),
    ("attribution_reconciliation","src/analytics/PortfolioAttribution.cpp","docs/ATTRIBUTION.md","attribution_tests","verified","independent Python identity validator"),
    ("moving_block_bootstrap","src/analytics/StatisticalAnalysis.cpp","docs/STATISTICAL_METHODOLOGY.md","statistical_tests","verified","circular block/path-length fixtures"),
    ("reality_check_formula","src/analytics/StatisticalAnalysis.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","centered max mean and +1 correction"),
    ("grid_wide_selection_risk","src/experiments/SelectionRisk.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","family and combined panels"),
    ("ma_candidate_grid","src/strategies/Strategy.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","16 MA candidates per ticker; 80 combined definitions"),
    ("rsi_candidate_grid","src/strategies/Strategy.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","12 RSI candidates per ticker; 60 combined definitions"),
    ("macd_candidate_grid","src/strategies/Strategy.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","4 MACD candidates per ticker; 20 combined definitions"),
    ("volatility_candidate_grid","src/strategies/Strategy.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","9 breakout candidates per ticker; 45 combined definitions"),
    ("candidate_retention","src/experiments/SelectionRisk.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","205 definitions and 1,025 candidate-window rows retained"),
    ("one_selection_per_family_window","src/experiments/SelectionRisk.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","100 selected rows; maximum key multiplicity one"),
    ("deployable_date_uniqueness","src/experiments/SelectionRisk.cpp","docs/STATISTICAL_METHODOLOGY.md","selection_risk_tests","verified","13,548 rows with no duplicate ticker/family/date keys"),
    ("regime_causality","src/methodology/ResearchMethodology.cpp","docs/METHODOLOGY.md","methodology_tests","verified","execution uses strictly prior regime; return interval uses start cutoff"),
    ("boundary_liquidation_costs","src/experiments/Backtester.cpp","docs/METHODOLOGY.md","methodology_tests","verified","end-close liquidation applies execution costs and is exported separately"),
    ("stale_marks_not_tradable","src/portfolio/UnionPortfolioBacktester.cpp","docs/MARKET_CALENDAR.md","union_portfolio_tests","verified","valuation marks are distinct from tradability"),
    ("parallel_determinism","include/quant/performance/DeterministicExecutor.h","docs/PERFORMANCE.md","performance_tests","verified","indexed collection and TSan"),
    ("performance_speedup","scripts/benchmark_performance.py","docs/PERFORMANCE.md","performance artifact validator","partially_verified","machine-specific repeated measurements; external profiler unavailable"),
    ("manifest_reconstruction","scripts/reproduce.py","docs/REPRODUCIBILITY.md","reproducibility_integration_tests","verified","clean local and Linux suite reconstruction"),
    ("input_provenance","manifests/*.json","docs/REPRODUCIBILITY.md","reproducibility_tests","partially_verified","hashes exact; original acquisition/corporate-action provenance incomplete"),
    ("cross_platform_financial_outputs","manifests/*.json","docs/REPRODUCIBILITY.md","remote reconstruction","verified","zero-tolerance deterministic artifacts"),
    ("cross_platform_stochastic_conclusions","scripts/reproducibility.py","docs/REPRODUCIBILITY.md","audit threshold fixture","contradicted","tolerances overlap release thresholds"),
    ("release_readiness","audit/final/release_blockers.md","README.md","final_audit_tests","contradicted","platform-stable RNG migration required"),
]
write_csv("audit_claim_inventory.csv", [{
    "claim": a,
    "source_file": b,
    "documentation": c,
    "supporting_test": d,
    "supporting_artifact": "audit/final/" + (
        "threshold_stability.csv" if "stochastic" in a or "release" in a
        else "audit_claim_inventory.csv"
    ),
    "independent_audit_method": f,
    "status": e,
} for a,b,c,d,e,f in claims])

findings = [
    {"id":"AUD-H-001","title":"Platform-dependent integer mapping leaves inference thresholds unstable","severity":"High","component":"StatisticalAnalysis/BootstrapAnalyzer","evidence":"std::uniform_int_distribution is implementation-defined; TSLA MACD margins are below one Monte Carlo SE","impact":"A release-relevant adjusted p-value can cross 0.05 after platform or seed changes","reproduction":"Compare audit threshold fixture and source RNG mapping","remediation":"Migrate to repository-owned unbiased bounded sampler and regenerate methodology-v4 baselines","release_blocker":"yes","closure_test":"Cross-platform reference vectors and all threshold-sensitive conclusions identical under stable sampler"},
    {"id":"AUD-H-002","title":"Current tolerance policy can approve conclusion-changing values","severity":"High","component":"Reproducibility comparator/manifests","evidence":"family p-value tolerance 0.05 exceeds TSLA MACD margin 0.0069431 and zero-cost margin 0.0019481","impact":"A successful reconstruction need not preserve reject/do-not-reject classification","reproduction":"test_final_audit threshold/tolerance misuse cases","remediation":"Remove inferential p-value tolerances after RNG migration; require exact stable-sampler values","release_blocker":"yes","closure_test":"Validator rejects every threshold-band crossing and release manifests use zero p-value tolerance"},
    {"id":"AUD-M-001","title":"Release manifests do not identify their containing commit","severity":"Medium","component":"Reproducibility manifests","evidence":"source_commit is ba31b17 while audited HEAD is later and compatible-descendant override is required","impact":"Release lineage is weaker than exact-commit reconstruction","reproduction":"Inspect manifest source_commit and run strict reproduce","remediation":"Regenerate release manifests at final post-migration audited commit","release_blocker":"yes","closure_test":"Strict reconstruction succeeds without compatible-descendant override"},
    {"id":"AUD-M-002","title":"Python locks pin versions but not distribution hashes","severity":"Medium","component":"Python supply chain","evidence":"requirements files use == without --hash records","impact":"Package-index compromise or replaced distribution is not cryptographically constrained","reproduction":"Inspect requirements-validation.txt","remediation":"Generate reviewed hash-locked requirements for release CI","release_blocker":"no","closure_test":"Clean CI install succeeds with --require-hashes"},
    {"id":"AUD-L-001","title":"GitHub Actions use mutable major-version tags","severity":"Low","component":"CI supply chain","evidence":"actions/checkout@v5 and setup-python@v6 are tags rather than immutable SHAs","impact":"Upstream tag movement changes CI implementation","reproduction":"Inspect .github/workflows","remediation":"Pin release workflows to reviewed action commit SHAs","release_blocker":"no","closure_test":"Workflow actions reference full commit SHAs"},
]
write_csv("audit_findings.csv", findings)

rng = [
    {"evidence_id":"RNG-1","component":"engine","observation":"std::mt19937 is seeded explicitly","classification":"portable_engine_sequence","release_impact":"none alone"},
    {"evidence_id":"RNG-2","component":"mapping","observation":"std::uniform_int_distribution<size_t> mapping is not standardized","classification":"platform_mapping_variation","release_impact":"raw sampled indices differ"},
    {"evidence_id":"RNG-3","component":"threads","observation":"bootstrap remains serial and candidate task collection is indexed","classification":"not_thread_scheduling","release_impact":"thread count does not explain stochastic difference"},
    {"evidence_id":"RNG-4","component":"floating_point","observation":"Spearman diagnostic differs by about 1.83e-17","classification":"separate_compiler_roundoff","release_impact":"non-inferential"},
    {"evidence_id":"RNG-5","component":"serialization","observation":"stable ordering and formatting verified independently","classification":"not_serialization","release_impact":"none"},
]
write_csv("rng_portability_evidence.csv", rng)

comparison = [
    {"artifact_family":"financial_simulation","mac_linux_result":"zero_tolerance_match","difference_source":"none observed","release_status":"pass"},
    {"artifact_family":"portfolio_attribution","mac_linux_result":"zero_tolerance_match","difference_source":"none observed","release_status":"pass"},
    {"artifact_family":"bootstrap_raw_paths","mac_linux_result":"different","difference_source":"uniform integer mapping","release_status":"blocked"},
    {"artifact_family":"bootstrap_summaries","mac_linux_result":"bounded differences","difference_source":"different sampled paths","release_status":"blocked near thresholds"},
    {"artifact_family":"selection_risk_p_values","mac_linux_result":"bounded differences","difference_source":"different max-statistic samples","release_status":"blocked near 0.05"},
    {"artifact_family":"rank_correlation","mac_linux_result":"1.83e-17 difference","difference_source":"floating-point rounding","release_status":"acceptable diagnostic tolerance"},
    {"artifact_family":"figures","mac_linux_result":"not byte-compared","difference_source":"rendering stack","release_status":"presentation-only"},
]
write_csv("cross_platform_comparison.csv", comparison)

tolerances = [
    ("selection_risk/family_selection_risk.csv","adjusted_p_value",0.05,0.026973,"unsupported","overlaps 0.05 decision threshold"),
    ("selection_risk/cross_family_selection_risk.csv","adjusted_p_value",0.05,0.034965,"unsupported","can overlap 0.05 threshold"),
    ("selection_risk/regime_selection_risk.csv","adjusted_p_value",0.06,0.052947,"justified_noninferential","regime slices explicitly exploratory"),
    ("statistics/sharpe_inference.csv","probability fields",0.04,0.026,"unsupported_near_boundary","Momentum/Equal Weight values approach 0.95"),
    ("statistics/bootstrap_summary.csv","Sharpe fields",0.10,0.091634,"too_wide_for_release_claim","bound nearly exhausted by one platform pair"),
    ("statistics/bootstrap_summary.csv","return_upper",3.0,2.57885,"too_wide_for_release_claim","bound empirically fitted to one pair"),
    ("selection_risk/candidate_rank_stability.csv","is_oos_spearman_rank_correlation",1e-15,1.82682e-17,"justified","far below ranking-changing scale"),
]
write_csv("tolerance_review.csv", [{"file":a,"field":b,"current_tolerance":c,"observed_max_difference":d,"audit_assessment":e,"conclusion_sensitivity":f} for a,b,c,d,e,f in tolerances])

validator_rows = [
    ("missing_manifest","rejected"),("unsupported_schema","rejected"),("altered_input_hash","rejected"),
    ("missing_output","rejected"),("extra_output","rejected"),("failed_command","rejected"),
    ("failed_validator","rejected"),("dirty_tree","rejected"),("wrong_commit","rejected"),
    ("false_success_report","rejected"),("p_value_out_of_bounds","rejected"),("diagnostic_deployable_confusion","covered_by_labels_and_tests"),
    ("threshold_crossing_within_tolerance","accepted_currently_release_blocker"),("malformed_audit_report","rejected_by_audit_validator"),
]
write_csv("validator_corruption_results.csv", [{"case":a,"result":b} for a,b in validator_rows])

quality = [
    ("C++ deterministic fixtures","strong","Broad causality/accounting/calendar coverage"),
    ("Python statistical reference","moderate","Formula-independent but does not solve cross-library mapping"),
    ("Regression snapshots","moderate","Useful change detector; not proof of correctness"),
    ("Reproducibility tests","moderate","Strong corruption coverage; tolerance tests did not enforce conclusion bands"),
    ("Cross-platform CI","strong detector","Exposed platform differences but policy converted inferential fields to broad tolerances"),
    ("Threshold tests before audit","weak","No release gate for tolerance-over-threshold overlap"),
]
write_csv("test_quality_review.csv", [{"surface":a,"assessment":b,"evidence":c} for a,b,c in quality])

acceptance = """# v1.0.0 Acceptance Criteria

- [ ] Implement a repository-owned unbiased bounded sampler with published reference vectors.
- [ ] Increment statistical methodology version and record sampler identifier in every statistical manifest.
- [ ] Regenerate all stochastic canonical baselines; retain old baselines as explicitly versioned historical fixtures.
- [ ] Prove identical sampled indices and stochastic CSVs on libc++ and libstdc++ for at least two seeds and serial/parallel modes.
- [ ] Prove TSLA MACD, zero-cost, portfolio probability, and all other release conclusions occupy identical decision bands.
- [ ] Remove inferential p-value/probability tolerances from release manifests; validator must reject band crossings.
- [ ] Regenerate manifests at the final audited commit and require exact commit matching without compatible-descendant override.
- [ ] Pass clean-checkout complete-suite reconstruction on Linux and macOS.
- [ ] Pass Release, ASan, UBSan, TSan, all validators, audit tests, and 8/8 regression snapshots.
- [ ] Resolve every Critical/High finding and record its closure-test evidence.
- [ ] Pin release Python distributions with hashes and review action SHA pinning.
- [ ] Align all documentation with the migrated methodology; retain no unsupported portability or profitability claim.
- [ ] Curate release artifacts and notes; create the tag only after final CI is green.
"""
(OUT / "release_acceptance_criteria.md").write_text(acceptance)

migration = """# RNG Migration Decision

## Decision

**C. FAIL — RNG MIGRATION REQUIRED**

The current `std::mt19937` engine is portable, but `std::uniform_int_distribution` does not specify one cross-library mapping. Current inferential tolerances overlap release decision thresholds. v1.0.0 must wait.

## Required Migration

1. Retain `std::mt19937` as the 32-bit engine or adopt an explicitly versioned engine with published state-transition vectors.
2. Add a repository-owned `stable_bounded_uint32(engine, bound)` using the Lemire multiply-high method with rejection: compute the 64-bit product of one engine word and `bound`; reject low words below `(-bound) % bound`; return the upper 32 bits. Reject zero bounds and bounds above the supported 32-bit domain.
3. Document unbiasedness: each accepted source interval has equal cardinality; rejection removes the incomplete range.
4. Define exact engine-word consumption, including rejection draws, and publish vectors for multiple bounds, seeds, rejection cases, IID samples, and circular block starts.
5. Replace both production mappings in `StatisticalAnalysis.cpp` and `BootstrapAnalyzer.cpp` through one shared implementation.
6. Increment the statistical methodology version (recommended `stochastic_sampling_v4`) and add engine/mapping identifiers to manifests and CSV metadata.
7. Regenerate every bootstrap, portfolio-policy, family, combined, zero-cost, high-cost, regime, figure, report, and reproducibility baseline affected by resampling.
8. Preserve current artifacts as historical v3 fixtures; do not present them as the new canonical baseline.
9. Add cross-platform reference-vector CI, two-seed threshold tests, repeated-run equality, and serial/2/4/8-thread equality.
10. Close the blocker only when Linux/libstdc++ and macOS/libc++ produce identical sampled indices, distributions, summaries, p-values, and conclusion bands with zero inferential tolerance.
"""
(OUT / "rng_migration_decision.md").write_text(migration)

blockers = """# Release Blockers

## RB-1 — Platform-Stable Stochastic Sampling

Closure: implement and version the repository-owned bounded sampler; pass cross-platform reference vectors and exact stochastic reconstruction.

## RB-2 — Threshold-Safe Inference Validation

Closure: remove broad inferential tolerances and prove TSLA MACD, zero-cost, Momentum Top-N, and all release-relevant results have identical bands across platforms and audited seeds.

## RB-3 — Final Commit Manifests

Closure: regenerate manifests after migration at the final audited commit; strict reconstruction must pass without `--allow-compatible-environment`.
"""
(OUT / "release_blockers.md").write_text(blockers)

report = """# Final Independent Audit Report

## Release Gate

**C. FAIL — RNG MIGRATION REQUIRED**

No core execution, accounting, calendar, attribution, candidate-selection, concurrency, or deterministic financial-output defect was found. The release is blocked because stochastic index mapping is standard-library-dependent and current inferential tolerances overlap decision thresholds.

## Principal Evidence

- Production uses `std::mt19937` with `std::uniform_int_distribution<std::size_t>` in both bootstrap paths.
- TSLA MACD adjusted p-values are close to 0.05; the canonical margin and zero-cost margin are smaller than one binomial Monte Carlo standard error at 1,000 simulations.
- Current family/cross-family tolerance is 0.05, which can approve values on opposite sides of the 0.05 decision boundary.
- An exploratory TSLA regime slice already crosses 0.05 between observed platforms.
- Momentum probability diagnostics vary enough to cross 0.5 and approach the 0.95 evidence boundary.
- Deterministic financial, portfolio, attribution, candidate, selected OOS, and concurrency outputs remain stable.

## Finding Summary

- Critical: 0
- High: 2 unresolved release blockers
- Medium: 2
- Low: 1

Detailed evidence is in the accompanying CSV files. Closure requires the migration specified in `rng_migration_decision.md`; this audit does not authorize or implement it.

## Methodology and Engineering Results

| Surface | Result | Evidence boundary |
| --- | --- | --- |
| Execution causality | Verified | Signals are produced after close information and pending orders fill at the next eligible open; end-boundary liquidation is separately labelled at close. |
| Cash, long-only, and costs | Verified | Affordability and oversell checks reject invalid fills; slippage enters fill price once and attribution records its explicit cost once. |
| Walk-forward and calendars | Verified | Civil dates, leap/month clamping, strict train/test boundaries, union calendars, stale-mark expiry, and causal regime cutoffs have deterministic fixtures. |
| Strategy selection | Verified | 41 candidates per ticker, 205 combined definitions, 1,025 candidate-window rows, one family/window selection, and no deployable duplicate dates. |
| Attribution | Verified | Period identity rejects residuals above configured scale; split/dividend/cost fixtures and independent Python corruption checks pass. |
| Bootstrap formulas | Verified | Circular blocks, path lengths, percentile intervals, probability counts, Sharpe inputs, and finite-sample `(1+e)/(B+1)` correction match independent references. |
| Reality check | Verified except portability | Centering, joint date sampling, max-mean statistic, candidate universes, and common-date alignment are supported; bounded-index mapping is platform-dependent. |
| Concurrency/performance | Verified within scope | Indexed task collection, immutable data reuse, serial causal paths, TSan, and thread equivalence preserve deterministic non-stochastic outputs. |
| Reproducibility | Partially verified | Input/config/hash/staging/rollback controls work, but Level 3 inferential tolerances do not preserve threshold classifications by construction. |

## Monte Carlo and Threshold Assessment

At 1,000 simulations, TSLA MACD's canonical distance from 0.05 is less than one binomial standard error. The zero-cost distance is smaller still. A fixed seed makes one run repeatable but does not establish a stable inferential conclusion under another valid index mapping or seed. Observed platform differences are consistent with different Monte Carlo samples, while the current tolerances are empirically fitted to too few platform/seed pairs and overlap decision boundaries.

## Validation, Security, and Hygiene

Validators strongly reject malformed schemas, hashes, duplicate/missing rows, invalid bounds, failed commands, dirty/wrong commits, and attribution residuals. The audit adds the missing threshold/tolerance gate. Supply-chain review found exact Python versions without distribution hashes and mutable GitHub Action tags. No secrets or personal paths were found in tracked manifests or audit artifacts. Generated builds/results remain ignored; historical schemas are clearly compatibility-labelled.

## Expert Recommendations

| Priority | Timing | Recommendation | Rationale and expected value | Risk | Implemented |
| --- | --- | --- | --- | --- | --- |
| Important | now | Treat the stable bounded-sampler migration and methodology-v4 baseline as the next controlled stage. | Removes the only observed source of conclusion-changing platform variation and permits exact stochastic reconstruction. | Baseline migration must be reviewed rather than presented as continuity. | No; outside audit authority. |
| Important | before release | Regenerate strict manifests at the final audited commit and disallow compatible-descendant release reconstruction. | Makes release lineage exact and closes the current manifest-source ambiguity. | New hashes must not be accepted without clean Linux/macOS reconstruction. | No; release-stage action. |
| Recommended | before release | Hash-pin Python distributions and GitHub Actions. | Narrows supply-chain variability in validation and CI. | Maintenance overhead when dependencies update. | No; outside audit scope. |
| Optional | after v1.0.0 | Add authoritative exchange calendars when source licensing and provenance are settled. | Improves closure/holiday semantics beyond data-availability inference. | Provider coupling and historical-calendar provenance complexity. | No. |
"""
(OUT / "final_audit_report.md").write_text(report)

files = sorted(path for path in OUT.iterdir() if path.name != "audit_manifest.json")
manifest = {"audit_schema_version":1,"audit_id":"final_audit_rng_gate_v1","baseline_commit":"7275f2c4445dc5a6c875d071c9a75c21f5e0c171",
            "decision":"C. FAIL — RNG MIGRATION REQUIRED","files":[{"path":path.name,"sha256":sha256(path),"size_bytes":path.stat().st_size} for path in files]}
(OUT / "audit_manifest.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")
print(f"Generated {len(files)+1} audit artifacts in {OUT}")
