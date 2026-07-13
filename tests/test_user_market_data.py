from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "scripts/validate_market_data.py"
MANIFEST = ROOT / "scripts/generate_local_data_manifest.py"


def call(*args: str, expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([sys.executable, *args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode != expected:
        raise AssertionError(result.stdout + result.stderr)
    return result


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="local-data-test-") as temp:
        root = Path(temp)
        valid = root / "LOCAL.csv"
        valid.write_text("Date,Open,High,Low,Close,Volume\n2024-01-01,10,12,9,11,100\n2024-01-02,11,13,10,12,120\n")
        call(str(VALIDATOR), str(valid))
        output = root / "manifest.json"
        call(str(MANIFEST), "--data-directory", str(root), "--tickers", "LOCAL", "--output", str(output))
        manifest = json.loads(output.read_text())
        assert manifest["classification"] == "user_supplied"
        assert len(manifest["inputs"][0]["sha256"]) == 64

        malformed = root / "BAD.csv"
        malformed.write_text("Date,Open,High,Low,Close,Volume\n2024-01-01,10,8,9,11,100\n")
        call(str(VALIDATOR), str(malformed), expected=1)
        call(str(MANIFEST), "--data-directory", str(root), "--tickers", "MISSING", "--output", str(output), expected=1)
    print("User-supplied data tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
