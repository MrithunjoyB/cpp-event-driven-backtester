from __future__ import annotations

import csv
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "scripts/generate_synthetic_market_data.py"
VALIDATOR = ROOT / "scripts/validate_synthetic_market_data.py"


def run(*args: str, expected: int = 0) -> None:
    result = subprocess.run([sys.executable, *args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode != expected:
        raise AssertionError(result.stdout + result.stderr)


def main() -> int:
    run(str(VALIDATOR), "data/synthetic", "--regenerate-check")
    with tempfile.TemporaryDirectory(prefix="synthetic-fixture-test-") as temp:
        first = Path(temp) / "first"
        second = Path(temp) / "second"
        run(str(GENERATOR), "--output-directory", str(first))
        run(str(GENERATOR), "--output-directory", str(second))
        first_files = sorted(path.name for path in first.iterdir())
        assert first_files == sorted(path.name for path in second.iterdir())
        for name in first_files:
            assert (first / name).read_bytes() == (second / name).read_bytes()

        corrupted = Path(temp) / "corrupted"
        shutil.copytree(first, corrupted)
        path = corrupted / "SYN_EQ_A.csv"
        with path.open(newline="") as stream:
            rows = list(csv.reader(stream))
        rows[1][2] = "0.100000"
        with path.open("w", newline="") as stream:
            csv.writer(stream, lineterminator="\n").writerows(rows)
        metadata_path = corrupted / "metadata.json"
        metadata = json.loads(metadata_path.read_text())
        import hashlib
        metadata["assets"][0]["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
        run(str(VALIDATOR), str(corrupted), expected=1)
    print("Synthetic generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
