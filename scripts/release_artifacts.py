#!/usr/bin/env python3
"""Build and validate v1.0.0 release archives, checksums, and SPDX metadata."""

from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

from validate_public_data_boundary import (
    FORBIDDEN_FILE_HASHES,
    FORBIDDEN_PATHS,
    FORBIDDEN_ROW_HASHES,
)


ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.0.0"
PROJECT = "cpp-event-driven-backtester"
TEXT_SUFFIXES = {
    ".c", ".cc", ".cpp", ".h", ".hpp", ".cmake", ".csv", ".json", ".md",
    ".py", ".txt", ".yml", ".yaml", ".cff",
}
LOCAL_PATH_PATTERNS = (
    re.compile(rb"/" + b"Users/"),
    re.compile(rb"/" + b"home/"),
    re.compile(rb"[A-Za-z]:\\\\" + b"Users\\\\"),
)
SECRET_PATTERNS = (
    re.compile(b"gh" + b"o_[A-Za-z0-9]{20,}"),
    re.compile(b"github_" + b"pat_[A-Za-z0-9_]{20,}"),
    re.compile(b"-----BEGIN " + b"[A-Z ]*PRIVATE KEY-----"),
    re.compile(b"AK" + b"IA[0-9A-Z]{16}"),
)


class ReleaseError(RuntimeError):
    pass


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def safe_component(value: str, label: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", value):
        raise ReleaseError(f"unsafe {label}: {value}")
    return value.lower()


def _archive_filter(info: tarfile.TarInfo) -> tarfile.TarInfo:
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    info.mtime = 0
    if info.isfile() and info.name.endswith("/bin/quant_cli"):
        info.mode = 0o755
    elif info.isfile():
        info.mode = 0o644
    elif info.isdir():
        info.mode = 0o755
    return info


def _write_tar_gz(source: Path, destination: Path, root_name: str) -> None:
    if destination.exists():
        raise ReleaseError(f"refusing to overwrite release asset: {destination.name}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w") as archive:
        archive.add(source, arcname=root_name, recursive=True, filter=_archive_filter)
    destination.write_bytes(gzip.compress(buffer.getvalue(), compresslevel=9, mtime=0))


def package_binary(cli: Path, platform_name: str, architecture: str, output: Path) -> Path:
    platform_name = safe_component(platform_name, "platform")
    architecture = safe_component(architecture, "architecture")
    if platform_name not in {"linux", "macos"}:
        raise ReleaseError("binary packages are supported only for validated Linux and macOS runners")
    if not cli.is_file():
        raise ReleaseError(f"missing quant_cli: {cli}")
    commit = git("rev-parse", "HEAD")
    name = f"{PROJECT}-v{VERSION}-{platform_name}-{architecture}"
    destination = output / f"{name}.tar.gz"
    with tempfile.TemporaryDirectory(prefix="quant-release-package-") as temporary:
        stage = Path(temporary) / name
        (stage / "bin").mkdir(parents=True)
        shutil.copy2(cli, stage / "bin/quant_cli")
        os.chmod(stage / "bin/quant_cli", 0o755)
        for relative in ("LICENSE", "NOTICE", "README.md", "CITATION.cff"):
            shutil.copy2(ROOT / relative, stage / relative)
        shutil.copytree(ROOT / "configs", stage / "configs")
        shutil.copytree(ROOT / "data/synthetic", stage / "data/synthetic")
        (stage / "scripts").mkdir()
        shutil.copy2(ROOT / "scripts/validate_results.py", stage / "scripts/validate_results.py")
        metadata = {
            "schema_version": 1,
            "project": PROJECT,
            "version": VERSION,
            "git_commit": commit,
            "platform": platform_name,
            "architecture": architecture,
            "stochastic_methodology_version": 2,
            "rng_mapping": "portable_bounded_v1",
            "public_data_classification": "project_owned_synthetic",
        }
        (stage / "RELEASE-METADATA.json").write_text(
            json.dumps(metadata, indent=2, sort_keys=True) + "\n"
        )
        _write_tar_gz(stage, destination, name)
    return destination


def package_source(commit: str, output: Path) -> Path:
    if git("status", "--porcelain", "--untracked-files=no"):
        raise ReleaseError("tracked working tree must be clean before source packaging")
    resolved = git("rev-parse", f"{commit}^{{commit}}")
    if resolved != commit:
        raise ReleaseError("source bundle requires a full exact commit SHA")
    name = f"{PROJECT}-v{VERSION}-reproducibility"
    destination = output / f"{name}.tar.gz"
    if destination.exists():
        raise ReleaseError(f"refusing to overwrite release asset: {destination.name}")
    archive = subprocess.check_output(
        ["git", "archive", "--format=tar", f"--prefix={name}/", commit], cwd=ROOT
    )
    output.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(gzip.compress(archive, compresslevel=9, mtime=0))
    return destination


def generate_sbom(inventory: Path, commit: str, output: Path) -> Path:
    dependencies = json.loads(inventory.read_text())
    packages = [{
        "name": PROJECT,
        "SPDXID": "SPDXRef-Package-cpp-event-driven-backtester",
        "versionInfo": VERSION,
        "downloadLocation": "https://github.com/MrithunjoyB/cpp-event-driven-backtester",
        "filesAnalyzed": False,
        "licenseConcluded": "Apache-2.0",
        "licenseDeclared": "Apache-2.0",
        "copyrightText": "Copyright 2026 Mrithunjoy Basumatary",
        "externalRefs": [{
            "referenceCategory": "PACKAGE-MANAGER",
            "referenceType": "purl",
            "referenceLocator": f"pkg:github/MrithunjoyB/cpp-event-driven-backtester@{VERSION}",
        }],
    }]
    relationships = []
    for dependency in dependencies["dependencies"]:
        identifier = "SPDXRef-Python-" + re.sub(r"[^A-Za-z0-9.-]", "-", dependency["name"])
        packages.append({
            "name": dependency["name"],
            "SPDXID": identifier,
            "versionInfo": dependency["version"],
            "downloadLocation": "NOASSERTION",
            "filesAnalyzed": False,
            "licenseConcluded": dependency["license"],
            "licenseDeclared": dependency["license"],
            "copyrightText": "NOASSERTION",
            "externalRefs": [{
                "referenceCategory": "PACKAGE-MANAGER",
                "referenceType": "purl",
                "referenceLocator": f"pkg:pypi/{dependency['name']}@{dependency['version']}",
            }],
            "comment": f"Not bundled; {', '.join(dependency['scopes'])} environment dependency.",
        })
        relationships.append({
            "spdxElementId": "SPDXRef-Package-cpp-event-driven-backtester",
            "relationshipType": "DEV_DEPENDENCY_OF",
            "relatedSpdxElement": identifier,
        })
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"{PROJECT}-v{VERSION}",
        "documentNamespace": (
            "https://github.com/MrithunjoyB/cpp-event-driven-backtester/"
            f"releases/v{VERSION}/spdx/{commit}"
        ),
        "creationInfo": {
            "created": "2026-07-14T00:00:00Z",
            "creators": ["Tool: scripts/release_artifacts.py", "Person: Mrithunjoy Basumatary"],
            "licenseListVersion": "3.27",
        },
        "documentDescribes": ["SPDXRef-Package-cpp-event-driven-backtester"],
        "packages": packages,
        "relationships": relationships,
        "annotations": [{
            "annotationDate": "2026-07-14T00:00:00Z",
            "annotationType": "OTHER",
            "annotator": "Person: Mrithunjoy Basumatary",
            "comment": (
                "Python packages are hash-locked validation or optional acquisition dependencies; "
                "they are not embedded in quant_cli binary archives."
            ),
        }],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
    return output


def _safe_member(name: str) -> PurePosixPath:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ReleaseError(f"unsafe archive member: {name}")
    return path


def _scan_content(name: str, data: bytes) -> None:
    digest = sha256_bytes(data)
    if digest in FORBIDDEN_FILE_HASHES:
        raise ReleaseError(f"forbidden provider-data hash in {name}")
    normalized = PurePosixPath(name).as_posix()
    if any(normalized == path or normalized.endswith("/" + path) for path in FORBIDDEN_PATHS):
        raise ReleaseError(f"forbidden provider-data path in {name}")
    if Path(name).suffix.lower() == ".csv":
        try:
            rows = csv.reader(io.StringIO(data.decode("utf-8-sig")))
            for row in rows:
                if sha256_bytes(",".join(row).encode()) in FORBIDDEN_ROW_HASHES:
                    raise ReleaseError(f"forbidden sampled provider row in {name}")
        except UnicodeDecodeError as error:
            raise ReleaseError(f"invalid UTF-8 CSV in {name}") from error
    if Path(name).suffix.lower() in TEXT_SUFFIXES:
        for pattern in LOCAL_PATH_PATTERNS + SECRET_PATTERNS:
            if pattern.search(data):
                raise ReleaseError(f"secret or developer-local path pattern in {name}")


def validate_archive(path: Path) -> dict:
    if not tarfile.is_tarfile(path):
        raise ReleaseError(f"unsupported or invalid release archive: {path.name}")
    files = 0
    with tarfile.open(path, "r:*") as archive:
        for member in archive.getmembers():
            _safe_member(member.name)
            if member.issym() or member.islnk() or member.isdev():
                raise ReleaseError(f"unsupported archive member type: {member.name}")
            if member.isfile():
                stream = archive.extractfile(member)
                if stream is None:
                    raise ReleaseError(f"cannot read archive member: {member.name}")
                _scan_content(member.name, stream.read())
                files += 1
    if files == 0:
        raise ReleaseError(f"empty release archive: {path.name}")
    return {"name": path.name, "sha256": sha256_file(path), "size_bytes": path.stat().st_size, "files": files}


def validate_sbom(path: Path) -> dict:
    value = json.loads(path.read_text())
    required = {"spdxVersion", "dataLicense", "SPDXID", "name", "documentNamespace", "creationInfo", "packages", "relationships"}
    if not required <= set(value) or value["spdxVersion"] != "SPDX-2.3" or value["dataLicense"] != "CC0-1.0":
        raise ReleaseError("invalid SPDX 2.3 document")
    identifiers = [item.get("SPDXID") for item in value["packages"]]
    if len(identifiers) != len(set(identifiers)) or any(not item for item in identifiers):
        raise ReleaseError("invalid or duplicate SPDX package identifiers")
    if any(item.get("licenseDeclared") in {None, "", "NOASSERTION"} for item in value["packages"]):
        raise ReleaseError("SBOM contains an unreviewed declared license")
    return {"packages": len(value["packages"]), "sha256": sha256_file(path)}


def write_checksums(directory: Path) -> Path:
    destination = directory / "SHA256SUMS"
    if destination.exists():
        raise ReleaseError("refusing to overwrite SHA256SUMS")
    files = sorted(path for path in directory.iterdir() if path.is_file() and path.name != destination.name)
    if not files:
        raise ReleaseError("no release assets to checksum")
    destination.write_text("".join(f"{sha256_file(path)}  {path.name}\n" for path in files))
    return destination


def validate_checksums(directory: Path) -> int:
    checksum_file = directory / "SHA256SUMS"
    seen = set()
    for line in checksum_file.read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_.-]+)", line)
        if not match:
            raise ReleaseError("malformed SHA256SUMS entry")
        expected, name = match.groups()
        path = directory / name
        if not path.is_file() or sha256_file(path) != expected:
            raise ReleaseError(f"checksum mismatch: {name}")
        seen.add(name)
    expected_names = {path.name for path in directory.iterdir() if path.is_file() and path.name != "SHA256SUMS"}
    if seen != expected_names:
        raise ReleaseError("SHA256SUMS inventory does not exactly match release assets")
    return len(seen)


def validate_directory(
    directory: Path, require_checksums: bool, required_platforms: tuple[str, ...] = ()
) -> dict:
    assets = sorted(path for path in directory.iterdir() if path.is_file())
    if not assets:
        raise ReleaseError("release directory is empty")
    archives = [validate_archive(path) for path in assets if path.name.endswith(".tar.gz")]
    sbom_path = directory / f"{PROJECT}-v{VERSION}-sbom.spdx.json"
    if not sbom_path.is_file():
        raise ReleaseError("release SBOM is missing")
    sbom = validate_sbom(sbom_path)
    required = {
        f"{PROJECT}-v{VERSION}-reproducibility.tar.gz",
        f"{PROJECT}-v{VERSION}-sbom.spdx.json",
        f"{PROJECT}-v{VERSION}-release-notes.md",
    }
    missing = required - {path.name for path in assets}
    if missing:
        raise ReleaseError("missing required assets: " + ", ".join(sorted(missing)))
    names = {path.name for path in assets}
    for platform_name in required_platforms:
        prefix = f"{PROJECT}-v{VERSION}-{safe_component(platform_name, 'platform')}-"
        matches = [name for name in names if name.startswith(prefix) and name.endswith(".tar.gz")]
        if len(matches) != 1:
            raise ReleaseError(
                f"expected exactly one {platform_name} binary package, found {len(matches)}"
            )
    if require_checksums and f"{PROJECT}-v{VERSION}-release-validation-report.json" not in names:
        raise ReleaseError("release validation report is missing")
    checksum_count = validate_checksums(directory) if require_checksums else 0
    return {
        "schema_version": 1,
        "release": f"v{VERSION}",
        "status": "passed",
        "archives": archives,
        "sbom": sbom,
        "checksummed_assets": checksum_count,
        "public_data_boundary": "passed",
        "secret_and_local_path_scan": "passed",
    }


def smoke_package(archive_path: Path, report_path: Path | None) -> dict:
    archive_result = validate_archive(archive_path)
    with tempfile.TemporaryDirectory(prefix="quant-release-smoke-") as temporary:
        root = Path(temporary)
        with tarfile.open(archive_path, "r:*") as archive:
            archive.extractall(root)
        package_roots = [path for path in root.iterdir() if path.is_dir()]
        if len(package_roots) != 1:
            raise ReleaseError("binary package must have exactly one root directory")
        package = package_roots[0]
        cli = package / "bin/quant_cli"
        version = subprocess.check_output([str(cli), "--version"], cwd=package, text=True)
        if version.splitlines() != [
            f"{PROJECT} {VERSION}",
            "stochastic_methodology_version=2",
            "rng_mapping=portable_bounded_v1",
        ]:
            raise ReleaseError("packaged CLI version metadata mismatch")
        subprocess.run([str(cli), "--help"], cwd=package, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(
            [str(cli), "validate-config", "--config", "configs/ma_walk_forward.json"],
            cwd=package, check=True, stdout=subprocess.DEVNULL,
        )
        subprocess.run(
            [str(cli), "--mode", "single", "--ticker", "SYN_EQ_A", "--strategy", "ma_cross"],
            cwd=package, check=True, stdout=subprocess.DEVNULL,
        )
        subprocess.run(
            ["python3", "scripts/validate_results.py", "results"],
            cwd=package, check=True, stdout=subprocess.DEVNULL,
        )
        generated = sorted(str(path.relative_to(package)) for path in (package / "results").rglob("*.csv"))
        if not generated:
            raise ReleaseError("packaged representative experiment generated no CSV outputs")
    result = {
        "schema_version": 1,
        "status": "passed",
        "archive": archive_result,
        "version": VERSION,
        "help": "passed",
        "config_validation": "passed",
        "representative_synthetic_experiment": "passed",
        "result_validation": "passed",
        "generated_csv_count": len(generated),
    }
    if report_path:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    binary = subparsers.add_parser("package-binary")
    binary.add_argument("--cli", type=Path, required=True)
    binary.add_argument("--platform", required=True)
    binary.add_argument("--architecture", required=True)
    binary.add_argument("--output-directory", type=Path, required=True)

    source = subparsers.add_parser("package-source")
    source.add_argument("--commit", required=True)
    source.add_argument("--output-directory", type=Path, required=True)

    sbom = subparsers.add_parser("sbom")
    sbom.add_argument("--inventory", type=Path, default=ROOT / "audit/release_v1/dependency_inventory.json")
    sbom.add_argument("--commit", required=True)
    sbom.add_argument("--output", type=Path, required=True)

    checksums = subparsers.add_parser("checksums")
    checksums.add_argument("--directory", type=Path, required=True)

    validate = subparsers.add_parser("validate")
    validate.add_argument("--directory", type=Path, required=True)
    validate.add_argument("--require-checksums", action="store_true")
    validate.add_argument("--required-platforms", default="")
    validate.add_argument("--report", type=Path)

    smoke = subparsers.add_parser("smoke-package")
    smoke.add_argument("--archive", type=Path, required=True)
    smoke.add_argument("--report", type=Path)

    args = parser.parse_args()
    try:
        if args.command == "package-binary":
            result = package_binary(args.cli.resolve(), args.platform, args.architecture, args.output_directory.resolve())
        elif args.command == "package-source":
            result = package_source(args.commit, args.output_directory.resolve())
        elif args.command == "sbom":
            result = generate_sbom(args.inventory.resolve(), args.commit, args.output.resolve())
        elif args.command == "checksums":
            result = write_checksums(args.directory.resolve())
        elif args.command == "validate":
            platforms = tuple(value for value in args.required_platforms.split(",") if value)
            report = validate_directory(args.directory.resolve(), args.require_checksums, platforms)
            if args.report:
                args.report.parent.mkdir(parents=True, exist_ok=True)
                args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
            result = report
        else:
            result = smoke_package(args.archive.resolve(), args.report)
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError, ReleaseError) as error:
        print(f"Release artifact operation failed: {error}")
        return 1
    print(json.dumps(result, sort_keys=True) if isinstance(result, dict) else result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
