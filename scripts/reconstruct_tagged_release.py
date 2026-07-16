#!/usr/bin/env python3
"""Reconstruct v1.0.0 from an isolated worktree at its exact annotated-tag target."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

from validate_release_provenance import ProvenanceError, validate_tagged_release


ROOT = Path(__file__).resolve().parents[1]


def _git(root: Path, *args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args], cwd=root, text=True, stderr=subprocess.STDOUT
        ).strip()
    except subprocess.CalledProcessError as error:
        raise ProvenanceError(error.output.strip() or "Git worktree operation failed") from error


def _run(argv: list[str], cwd: Path) -> None:
    subprocess.run(argv, cwd=cwd, check=True)


@contextmanager
def detached_release_worktree(
    root: Path, target_commit: str, temporary_root: Path
) -> Iterator[Path]:
    """Create and safely remove a clean detached worktree for one verified commit."""
    root = root.resolve()
    temporary_root = temporary_root.resolve()
    temporary_root.mkdir(parents=True, exist_ok=True)
    outer_head = _git(root, "rev-parse", "HEAD")
    outer_status = _git(root, "status", "--porcelain=v1", "--untracked-files=all")
    parent = tempfile.TemporaryDirectory(
        prefix="v1.0.0-release-worktree-", dir=temporary_root
    )
    worktree = Path(parent.name) / "source"
    added = False
    try:
        _run(
            ["git", "worktree", "add", "--detach", str(worktree), target_commit],
            root,
        )
        added = True
        if _git(worktree, "rev-parse", "HEAD") != target_commit:
            raise ProvenanceError("detached release worktree resolved to the wrong commit")
        if _git(worktree, "status", "--porcelain=v1", "--untracked-files=all"):
            raise ProvenanceError("detached release worktree is not clean")
        yield worktree
    finally:
        if added:
            subprocess.run(
                ["git", "worktree", "remove", "--force", str(worktree)],
                cwd=root,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
        subprocess.run(
            ["git", "worktree", "prune", "--expire=now"],
            cwd=root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        parent.cleanup()
        if _git(root, "rev-parse", "HEAD") != outer_head:
            raise ProvenanceError("current worktree HEAD changed during release reconstruction")
        if _git(root, "status", "--porcelain=v1", "--untracked-files=all") != outer_status:
            raise ProvenanceError("current worktree changed during release reconstruction")


def reconstruction_commands(
    worktree: Path,
    target_commit: str,
    output_directory: Path,
    build_directory: Path,
    report_path: Path,
    threads: int,
) -> list[list[str]]:
    python = sys.executable
    return [
        [
            python,
            str(worktree / "scripts/validate_release_provenance.py"),
            "--candidate",
            target_commit,
        ],
        [
            python,
            str(worktree / "scripts/validate_synthetic_market_data.py"),
            str(worktree / "data/synthetic"),
            "--regenerate-check",
        ],
        [python, str(worktree / "scripts/validate_public_data_boundary.py")],
        [
            python,
            str(worktree / "scripts/validate_reproducibility.py"),
            str(worktree / "manifests"),
            "--verify-inputs",
        ],
        [
            python,
            str(worktree / "scripts/reproduce.py"),
            "--manifest",
            str(worktree / "manifests/public_reproducibility_suite.json"),
            "--output-directory",
            str(output_directory),
            "--build-directory",
            str(build_directory),
            "--execution-mode",
            "parallel",
            "--threads",
            str(threads),
            "--allow-compatible-environment",
            "--keep-failed-output",
            "--json-report",
            str(report_path),
        ],
        [
            python,
            str(worktree / "scripts/validate_reproducibility.py"),
            str(worktree / "manifests"),
            "--verify-inputs",
            "--report",
            str(report_path),
        ],
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--temporary-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--json-report", type=Path, required=True)
    parser.add_argument("--threads", type=int, default=4, choices=(2, 4, 8))
    parser.add_argument("--install-dependencies", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    output_directory = args.output_directory.resolve()
    build_directory = args.build_directory.resolve()
    report_path = args.json_report.resolve()
    try:
        provenance = validate_tagged_release(root)
        target = provenance["tag_target"]
        with detached_release_worktree(root, target, args.temporary_directory) as worktree:
            if args.install_dependencies:
                _run(
                    [
                        sys.executable,
                        "-m",
                        "pip",
                        "install",
                        "--require-hashes",
                        "-r",
                        str(worktree / "requirements-validation.lock"),
                    ],
                    worktree,
                )
            for command in reconstruction_commands(
                worktree,
                target,
                output_directory,
                build_directory,
                report_path,
                args.threads,
            ):
                _run(command, worktree)
    except (OSError, ProvenanceError, subprocess.CalledProcessError) as error:
        print(f"tagged release reconstruction failed: {error}", file=sys.stderr)
        return 1
    print(
        json.dumps(
            {
                "status": "success",
                "release": provenance["release"],
                "tag_object": provenance["tag_object"],
                "tag_target": provenance["tag_target"],
                "release_candidate_commit": provenance["release_candidate_commit"],
                "current_descendant": provenance["current_commit"],
                "output_directory": str(output_directory),
                "json_report": str(report_path),
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
