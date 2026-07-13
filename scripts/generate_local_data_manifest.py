#!/usr/bin/env python3
"""Create a local hash manifest for user-supplied market data."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from validate_market_data import DataValidationError, validate_file


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-directory", type=Path, default=Path("data/local"))
    parser.add_argument("--tickers", nargs="+", required=True)
    parser.add_argument("--output", type=Path, default=Path("data/local/local_data_manifest.json"))
    parser.add_argument("--provider", default="user_declared")
    args = parser.parse_args()
    try:
        inputs = [validate_file((args.data_directory / f"{ticker}.csv").resolve()) for ticker in args.tickers]
    except DataValidationError as error:
        print(f"Local manifest generation failed: {error}")
        return 1
    for item in inputs:
        item["path"] = str(Path(item["path"]).relative_to(Path.cwd())) if Path(item["path"]).is_relative_to(Path.cwd()) else item["path"]
        item["provider"] = args.provider
        item["redistribution_status"] = "user_responsibility_not_granted_by_project"
    manifest = {
        "manifest_type": "user_supplied_market_data_v1",
        "classification": "user_supplied",
        "canonical_release_input": False,
        "network_required_for_execution": False,
        "provider_responsibility": "The user is responsible for provider authorization and applicable terms.",
        "inputs": inputs,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"Wrote local manifest: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
