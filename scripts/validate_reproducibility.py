#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path

from reproducibility import ReproducibilityError, load_manifest, validate_suite, verify_inputs

ROOT = Path(__file__).resolve().parents[1]

def validate_report(path):
    report = json.loads(Path(path).read_text())
    required = {"report_schema_version", "status"}
    if not required <= set(report) or report["report_schema_version"] != 1:
        raise ReproducibilityError("malformed reconstruction report")
    if report["status"] == "success":
        packages = report.get("packages", [report])
        for package in packages:
            if any(item.get("status") == "failed_match" for item in package.get("outputs", [])):
                raise ReproducibilityError("successful report contains a failed artifact")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path, nargs="?", default=ROOT / "manifests")
    parser.add_argument("--verify-inputs", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        manifests = [args.path] if args.path.is_file() else sorted(args.path.glob("*.json"))
        if not manifests:
            raise ReproducibilityError("no manifests found")
        package_count = 0
        for path in manifests:
            value = json.loads(path.read_text())
            if value.get("package_type") == "suite":
                validate_suite(value, path)
                for child in value["children"]:
                    if not (ROOT / child["manifest"]).is_file():
                        raise ReproducibilityError(f"missing child manifest: {child['manifest']}")
            else:
                manifest = load_manifest(path)
                package_count += 1
                if args.verify_inputs:
                    verify_inputs(ROOT, manifest)
        if args.report:
            validate_report(args.report)
        print(f"Reproducibility validation passed: {package_count} package manifests, {len(manifests)-package_count} suite plans")
    except (OSError, json.JSONDecodeError, ReproducibilityError) as error:
        print(f"Reproducibility validation failed: {error}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
