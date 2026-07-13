#!/usr/bin/env python3
"""Reject unverified provider data and ambiguous provenance from the public tree."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

from validate_synthetic_market_data import ValidationError as SyntheticValidationError
from validate_synthetic_market_data import validate as validate_synthetic


FORBIDDEN_PATHS = {
    "data/AAPL.csv", "data/MSFT.csv", "data/SPY.csv", "data/TSLA.csv", "data/BTC-USD.csv",
}
FORBIDDEN_FILE_HASHES = {
    "e49cd4203cf2ef6fe0d5e24279721ffe05b11c4821980042892248f5c4559ecb",
    "fac9fbcee758363bba73a7b02acf2b75529c6c4df50f1a3109896076bb971d60",
    "acd37f190cac9757cffa56970cd631cc6caed5f62d902567cfae9b62a5df46b8",
    "96638f4d7acc232c410ab909fd2449720cd6c30c6c7fc76676d8478dcc54c9ca",
    "d8855157e58a7f60826f73285587e4b336a2d430235601ecb7b064d8584f33f0",
}
FORBIDDEN_ROW_HASHES = {
    "8f8bb0f4a73f34c6bc9e07a259090151c61eaf0b03bd50acb314b12c4fda73fa", "5447bd68a8a2038e90fd14415c1a48558433c5166a7605eb8fd3f8047b82026c", "89af99bb013c71a5dbd2254d2674f6c7ac46533b188a7f9bff017f93930a55ec",
    "2dee787c3e70bd0668649b7be729f10d7aff5c62c3632e315aaec06aaa2975f4", "1793a18e2b7b54fd8878f8ce1c5e795c06c19d78aa5ad59e7e798bbab6e28a4b", "e30dc63d859de8c58a3d40f2cd5b1bbbe031ed0cbd0750dd61888c1a83d51f07",
    "c97317796d5608b1c73422652896b099393cf4d6a5f335abf027d3f3c5a29bdf", "65a738b965cf9f3d2774b5763e22392dbec37f4649a0063766b0dfc6b2498a2b", "1a13ff7b238dd46ff0227a5bdb04a7ff2bd867a88fc9ff79f95fc012c2f960c2",
    "055d4ad22ef9b6fe9af4b1b4d49fe25afae10d25ed9ac80010ae85159314d2c2", "23b4d27a25dff0b4fb6fed9047897aa43f355836c93c83342ad8cd3ccec72120", "bfe4ff9262b49093b7a565d8f8decc87de064d65353beeea6f7a4aa9c8f9342d",
    "8954fd003f93203c4e809ba1c69a742a53ca63c41d0015c2776fbb922cb06edb", "82f54df690fdad23c6bb381172dcb65c1e547cb7b30d453f40fe036007f0f6c6", "1282bdd0886fbdcce1ce79979ddeb392213e3ff32eb0e45d9fec54b04001f587",
}
REAL_TICKER_TOKENS = {"AAPL", "MSFT", "SPY", "TSLA", "BTC-USD"}
PUBLIC_ASSETS = {"SYN_EQ_A", "SYN_EQ_B", "SYN_EQ_C", "SYN_BENCH", "SYN_CRYPTO"}


class BoundaryError(RuntimeError):
    pass


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def validate_public_manifest(value: dict, path: str) -> None:
    serialized = json.dumps(value, sort_keys=True)
    if any(token in serialized for token in FORBIDDEN_PATHS):
        raise BoundaryError(f"removed provider path referenced by {path}")
    if "data/local" in serialized or "__USER_" in serialized or "__RESOLVED_LOCALLY__" in serialized:
        raise BoundaryError(f"local or unresolved input leaked into public manifest {path}")
    if any(token in serialized for token in ("download_data.py", "yfinance", "curl ", "wget ")):
        raise BoundaryError(f"network acquisition leaked into public manifest {path}")
    if value.get("package_type") == "suite":
        if not value.get("suite_id", "").startswith("public_"):
            raise BoundaryError(f"ambiguous suite identity in {path}")
        return
    inputs = value.get("inputs", [])
    if not inputs:
        raise BoundaryError(f"public package has no inputs: {path}")
    for item in inputs:
        if item.get("data_classification") != "synthetic":
            raise BoundaryError(f"ambiguous input classification in {path}")
        if not str(item.get("path", "")).startswith("data/synthetic/"):
            raise BoundaryError(f"non-synthetic public input path in {path}")
        if item.get("redistribution_status") != "project_owned_apache_2_0":
            raise BoundaryError(f"missing synthetic redistribution status in {path}")
    if "synthetic" not in value.get("description", "").lower():
        raise BoundaryError(f"public package description omits synthetic status: {path}")


def _check_csv_rows(path: Path) -> None:
    try:
        with path.open(newline="", encoding="utf-8-sig") as stream:
            for row in csv.reader(stream):
                if _sha256_bytes(",".join(row).encode()) in FORBIDDEN_ROW_HASHES:
                    raise BoundaryError(f"sampled removed-provider row found in {path}")
    except UnicodeDecodeError as error:
        raise BoundaryError(f"unreadable tracked CSV {path}") from error


def _archive_members(path: Path) -> list[str]:
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            return archive.namelist()
    if tarfile.is_tarfile(path):
        with tarfile.open(path) as archive:
            return archive.getnames()
    return []


def validate(root: Path) -> tuple[int, int]:
    tracked = subprocess.check_output(["git", "ls-files"], cwd=root, text=True).splitlines()
    failures: list[str] = []
    if FORBIDDEN_PATHS.intersection(tracked):
        failures.append("tracked removed-provider paths: " + ", ".join(sorted(FORBIDDEN_PATHS.intersection(tracked))))
    if any(path.startswith("results/") for path in tracked):
        failures.append("generated result artifacts remain tracked under results/")

    synthetic_csvs = {f"data/synthetic/{asset}.csv" for asset in PUBLIC_ASSETS}
    required = synthetic_csvs | {"data/synthetic/metadata.json"}
    missing = required - set(tracked)
    if missing:
        failures.append("missing tracked synthetic fixtures: " + ", ".join(sorted(missing)))

    for relative in tracked:
        path = root / relative
        if not path.is_file():
            continue
        try:
            data = path.read_bytes()
        except OSError as error:
            failures.append(f"cannot inspect {relative}: {error}")
            continue
        if _sha256_bytes(data) in FORBIDDEN_FILE_HASHES:
            failures.append(f"removed-provider file hash remains tracked: {relative}")
        if path.suffix.lower() == ".csv":
            try:
                _check_csv_rows(path)
            except BoundaryError as error:
                failures.append(str(error))
        if relative.startswith("release/"):
            members = _archive_members(path)
            if any(PurePosixPath(member).as_posix() in FORBIDDEN_PATHS for member in members):
                failures.append(f"release archive contains removed provider path: {relative}")

    manifests = sorted((root / "manifests").glob("*.json"))
    if not manifests:
        failures.append("no public manifests found")
    for path in manifests:
        try:
            validate_public_manifest(json.loads(path.read_text()), str(path.relative_to(root)))
        except (BoundaryError, json.JSONDecodeError) as error:
            failures.append(str(error))

    try:
        metadata = validate_synthetic(root / "data/synthetic", regenerate_check=True)
        if {item["asset"] for item in metadata["assets"]} != PUBLIC_ASSETS:
            failures.append("synthetic metadata asset identity mismatch")
    except (SyntheticValidationError, ValueError, json.JSONDecodeError) as error:
        failures.append(f"synthetic fixture validation failed: {error}")

    if failures:
        raise BoundaryError("; ".join(failures))
    return len(tracked), len(manifests)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        tracked, manifests = validate(args.root.resolve())
    except (BoundaryError, subprocess.CalledProcessError) as error:
        print(f"Public data-boundary validation failed: {error}")
        return 1
    print(f"Public data-boundary validation passed: {tracked} tracked files, {manifests} public manifests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
