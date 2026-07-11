from __future__ import annotations

import csv
import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/fixtures/regression/stage0_architecture_baseline.csv"


def main() -> int:
    failures: list[str] = []
    with MANIFEST.open(newline="") as stream:
        snapshots = list(csv.DictReader(stream))
    for snapshot in snapshots:
        path = ROOT / snapshot["path"]
        if not path.exists():
            failures.append(f"{snapshot['scenario']}: missing {path.relative_to(ROOT)}")
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        row_count = sum(1 for _ in path.open())
        if digest != snapshot["sha256"]:
            failures.append(f"{snapshot['scenario']}: SHA-256 changed ({digest})")
        if row_count != int(snapshot["row_count"]):
            failures.append(f"{snapshot['scenario']}: row count {row_count} != {snapshot['row_count']}")
    if failures:
        print("Regression snapshot comparison failed:")
        print("\n".join(f"- {failure}" for failure in failures))
        return 1
    print(f"Regression snapshots matched: {len(snapshots)} scenarios")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
