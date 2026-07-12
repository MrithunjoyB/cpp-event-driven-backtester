#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

from reproducibility import ReproducibilityError, load_manifest, reconstruct, validate_suite

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description="Verify and reconstruct a versioned research manifest")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, default=ROOT / "reproduced")
    parser.add_argument("--build-directory", type=Path, default=ROOT / "build-reproduce")
    parser.add_argument("--execution-mode", choices=("serial", "parallel"), default="serial")
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--allow-compatible-environment", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--keep-failed-output", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--json-report", type=Path)
    args = parser.parse_args()
    args.build_directory = args.build_directory.resolve()
    if args.threads < 1 or args.threads > 64 or (args.execution_mode == "serial" and args.threads != 1):
        parser.error("threads must be 1-64 and serial mode requires one thread")
    try:
        manifest = json.loads(args.manifest.read_text())
        if manifest.get("package_type") == "suite":
            validate_suite(manifest, args.manifest)
            reports = []
            target = args.output_directory.resolve()
            target.parent.mkdir(parents=True, exist_ok=True)
            stage = None if args.verify_only else Path(tempfile.mkdtemp(prefix=".reproduce-suite-", dir=target.parent))
            try:
                for child in manifest.get("children", []):
                    child_path = ROOT / child["manifest"]
                    child_output = target / child["id"] if args.verify_only else stage / child["id"]
                    reports.append(reconstruct(ROOT, load_manifest(child_path), child_output,
                        args.build_directory, args.execution_mode, args.threads, args.verify_only,
                        args.allow_compatible_environment, args.allow_dirty, args.keep_failed_output,
                        build=not args.no_build and not reports))
                report = {"report_schema_version": 1, "status": "success", "suite": manifest.get("suite_id"), "packages": reports}
                if stage:
                    (stage / "reconstruction_suite_report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
                    lines = ["# Canonical Suite Reconstruction", "", "- Status: success",
                             f"- Suite: `{manifest.get('suite_id')}`", f"- Packages: {len(reports)}",
                             f"- Execution: {args.execution_mode}, {args.threads} thread(s)"]
                    (stage / "reconstruction_suite_report.md").write_text("\n".join(lines) + "\n")
                    backup = None
                    if target.exists():
                        backup = target.with_name(target.name + ".previous")
                        if backup.exists(): shutil.rmtree(backup)
                        target.rename(backup)
                    try:
                        stage.rename(target)
                    except BaseException:
                        if backup and backup.exists() and not target.exists(): backup.rename(target)
                        raise
                    if backup: shutil.rmtree(backup)
            except BaseException:
                if stage and stage.exists():
                    if args.keep_failed_output:
                        failed = target.with_name(target.name + ".failed")
                        if failed.exists(): shutil.rmtree(failed)
                        stage.rename(failed)
                    else:
                        shutil.rmtree(stage, ignore_errors=True)
                raise
        else:
            report = reconstruct(ROOT, load_manifest(args.manifest), args.output_directory, args.build_directory,
                args.execution_mode, args.threads, args.verify_only, args.allow_compatible_environment,
                args.allow_dirty, args.keep_failed_output, build=not args.no_build)
        if args.json_report:
            args.json_report.parent.mkdir(parents=True, exist_ok=True)
            args.json_report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        print(json.dumps({"status": "success", "manifest": str(args.manifest), "output": str(args.output_directory)}))
    except (OSError, json.JSONDecodeError, subprocess.CalledProcessError, ReproducibilityError) as error:
        print(f"reproduce: {error}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    import subprocess
    raise SystemExit(main())
