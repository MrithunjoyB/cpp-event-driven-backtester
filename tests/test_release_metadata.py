#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.0.0"


def check(value: bool, label: str) -> None:
    if not value:
        raise AssertionError(label)


def lock_packages(path: Path) -> dict[str, str]:
    text = path.read_text()
    matches = list(re.finditer(r"(?m)^([A-Za-z0-9_.-]+)==([^\s\\]+)\s*\\?$", text))
    packages = {}
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        check("--hash=sha256:" in text[match.end():end], f"missing distribution hash for {match.group(1)}")
        packages[match.group(1).lower().replace("_", "-")] = match.group(2)
    check(bool(packages), f"empty dependency lock: {path.name}")
    return packages


def validate_links(path: Path) -> int:
    count = 0
    for target in re.findall(r"\[[^]]+\]\(([^)]+)\)", path.read_text()):
        target = target.strip().strip("<>")
        if target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        target = target.split("#", 1)[0]
        if not target:
            continue
        check((path.parent / target).resolve().exists(), f"broken documentation link in {path}: {target}")
        count += 1
    return count


parser = argparse.ArgumentParser()
parser.add_argument("--cli", type=Path, required=True)
args = parser.parse_args()

cmake = (ROOT / "CMakeLists.txt").read_text()
check("project(CppEventDrivenBacktester VERSION 1.0.0 LANGUAGES CXX)" in cmake, "CMake version")
version_lines = subprocess.check_output([str(args.cli), "--version"], text=True).splitlines()
check(version_lines == [
    "cpp-event-driven-backtester 1.0.0",
    "stochastic_methodology_version=2",
    "rng_mapping=portable_bounded_v1",
], "CLI release identity")

citation = (ROOT / "CITATION.cff").read_text()
check(re.search(r"(?m)^version: 1\.0\.0$", citation) is not None, "citation version")
check(re.search(r"(?m)^date-released: 2026-07-14$", citation) is not None, "citation date")
check("Mrithunjoy" in citation and "Basumatary" in citation, "citation authorship")
check("## [1.0.0] - 2026-07-14" in (ROOT / "CHANGELOG.md").read_text(), "changelog release")
release_notes = (ROOT / "docs/RELEASE_NOTES_v1.0.0.md").read_text()
check("Draft" not in release_notes and "# v1.0.0 Release Notes" in release_notes, "release notes status")

validation = lock_packages(ROOT / "requirements-validation.lock")
acquisition = lock_packages(ROOT / "requirements-acquisition.lock")
inventory = json.loads((ROOT / "audit/release_v1/dependency_inventory.json").read_text())
reviewed = {
    item["name"].lower().replace("_", "-"): item["version"]
    for item in inventory["dependencies"]
}
check(reviewed == validation | acquisition, "dependency inventory does not match both locks")
check(all(item["license"] and item["license"] != "NOASSERTION" for item in inventory["dependencies"]), "unreviewed dependency license")
lock_metadata = json.loads((ROOT / "audit/release_v1/dependency_lock_metadata.json").read_text())
check(lock_metadata["generator"] == "pip-tools" and lock_metadata["generator_version"] == "7.5.3", "lock generator metadata")
for item in lock_metadata["locks"]:
    digest = hashlib.sha256((ROOT / item["path"]).read_bytes()).hexdigest()
    check(digest == item["sha256"] and item["verified_install"] is True, f"lock metadata mismatch: {item['path']}")

workflow_uses = []
for workflow in sorted((ROOT / ".github/workflows").glob("*.yml")):
    for line in workflow.read_text().splitlines():
        if re.match(r"^\s*-?\s*uses:", line):
            workflow_uses.append((workflow, line))
            check(
                re.search(r"uses:\s+[^\s@]+@[0-9a-f]{40}\s+#\s+.+$", line) is not None,
                f"mutable or undocumented GitHub Action in {workflow}: {line.strip()}",
            )
check(bool(workflow_uses), "no GitHub Actions reviewed")

link_count = 0
documentation = [ROOT / name for name in ("README.md", "CHANGELOG.md", "SECURITY.md", "CONTRIBUTING.md")]
documentation += sorted((ROOT / "docs").glob("*.md"))
for document in documentation:
    link_count += validate_links(document)
check(link_count > 20, "documentation link coverage unexpectedly small")

license_text = (ROOT / "LICENSE").read_text()
check("Apache License" in license_text and "END OF TERMS AND CONDITIONS" in license_text and "APPENDIX" in license_text, "Apache-2.0 text incomplete")
notice = (ROOT / "NOTICE").read_text()
check("Copyright 2026 Mrithunjoy Basumatary" in notice, "NOTICE copyright")
check("AI-assisted" in notice and "data/synthetic/" in notice, "NOTICE scope")

print(
    f"Release metadata tests passed: {len(validation)} validation dependencies, "
    f"{len(acquisition)} acquisition dependencies, {link_count} local links"
)
