from __future__ import annotations

import copy
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from validate_public_data_boundary import BoundaryError, FORBIDDEN_FILE_HASHES, FORBIDDEN_ROW_HASHES, validate_public_manifest


def rejected(value: dict, name: str) -> None:
    try:
        validate_public_manifest(value, name)
    except BoundaryError:
        return
    raise AssertionError(f"boundary accepted {name}")


def main() -> int:
    manifest = json.loads((ROOT / "manifests/public_synthetic_single_ma.json").read_text())
    validate_public_manifest(manifest, "valid")
    bad = copy.deepcopy(manifest)
    bad["inputs"][0]["path"] = "data/AAPL.csv"
    rejected(bad, "removed path")
    bad = copy.deepcopy(manifest)
    bad["inputs"][0]["data_classification"] = "user_supplied"
    rejected(bad, "ambiguous classification")
    bad = copy.deepcopy(manifest)
    bad["inputs"][0]["path"] = "data/local/LOCAL.csv"
    rejected(bad, "local path")
    bad = copy.deepcopy(manifest)
    bad["commands"][0]["argv"].append("download_data.py")
    rejected(bad, "network acquisition")
    assert len(FORBIDDEN_FILE_HASHES) == 5
    assert len(FORBIDDEN_ROW_HASHES) == 15
    assert hashlib.sha256(b"not provider data").hexdigest() not in FORBIDDEN_FILE_HASHES
    print("Public data-boundary corruption tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
