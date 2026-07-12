from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        attribution = Path(temporary) / "attribution"
        attribution.mkdir()
        (attribution / "attribution_reconciliation.csv").write_text(
            "schema_version,experiment_id,policy,benchmark,adjustment_basis,calendar_mode,attribution_methodology,contribution_units,residual_tolerance,start_date,end_date,beginning_value,ending_value,external_cash_flow,market_pnl,dividend_income,corporate_action_effect,cash_return,commission,spread_cost,slippage_cost,residual\n"
            "3,bad,equal_weight,SPY,raw_price,union,trade_aware_accounting,pnl_currency,1e-8,2024-01-01,2024-01-02,100,110,0,0,0,0,0,0,0,0,10\n"
        )
        completed = subprocess.run(
            ["python3", str(ROOT / "scripts/validate_results.py"), temporary],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode == 0 or "residual exceeds" not in completed.stdout or "missing required attribution files" not in completed.stdout:
            raise AssertionError("corrupted attribution package was not rejected correctly")
    print("attribution validator rejection test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
