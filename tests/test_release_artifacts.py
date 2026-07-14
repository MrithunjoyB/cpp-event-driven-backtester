#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import platform
import sys
import tarfile
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from release_artifacts import (
    PROJECT,
    VERSION,
    ReleaseError,
    generate_sbom,
    git,
    package_binary,
    package_source,
    smoke_package,
    validate_archive,
    validate_directory,
    write_checksums,
)


def reject(function, label: str) -> None:
    try:
        function()
    except ReleaseError:
        return
    raise AssertionError(f"accepted invalid release artifact: {label}")


parser = argparse.ArgumentParser()
parser.add_argument("--cli", type=Path, required=True)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix="quant-release-artifact-test-") as temporary:
    output = Path(temporary) / "dist"
    system = platform.system().lower()
    platform_name = "macos" if system == "darwin" else "linux"
    architecture = platform.machine().lower()
    binary = package_binary(args.cli.resolve(), platform_name, architecture, output)
    smoke = smoke_package(binary, output / f"{platform_name}-smoke.json")
    assert smoke["status"] == "passed"

    commit = git("rev-parse", "HEAD")
    package_source(commit, output)
    generate_sbom(
        ROOT / "audit/release_v1/dependency_inventory.json",
        commit,
        output / f"{PROJECT}-v{VERSION}-sbom.spdx.json",
    )
    (output / f"{PROJECT}-v{VERSION}-release-notes.md").write_bytes(
        (ROOT / "docs/RELEASE_NOTES_v1.0.0.md").read_bytes()
    )
    report = validate_directory(output, require_checksums=False)
    (output / f"{PROJECT}-v{VERSION}-release-validation-report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n"
    )
    write_checksums(output)
    final = validate_directory(output, require_checksums=True)
    assert final["status"] == "passed" and final["checksummed_assets"] >= 6

    traversal = Path(temporary) / "traversal.tar.gz"
    with tarfile.open(traversal, "w:gz") as archive:
        info = tarfile.TarInfo("../escape")
        info.size = 0
        archive.addfile(info)
    reject(lambda: validate_archive(traversal), "archive traversal")

print("Release artifact generation, smoke, SBOM, checksum, and corruption tests passed")
