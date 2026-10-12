#!/usr/bin/env python3
"""Delivery regression checks for release, source admission, and build hygiene.

Run directly:

    PYTHONDONTWRITEBYTECODE=1 python3 tools/delivery_hygiene_test.py
"""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATE_RELEASE = ROOT / "tools" / "validate_release.ps1"
REQUIRED_PATHS = (
    "launcher/SoulMod.Launcher.exe",
    "launcher/SoulMod.Launcher.dll",
    "launcher/SoulMod.Launcher.deps.json",
    "launcher/SoulMod.Launcher.runtimeconfig.json",
    "launcher/SoulMod.ExecutionCopy.dll",
    "launcher/SoulMod.Package.dll",
    "runtime/SoulMod.Runtime.dll",
    "notices/LICENSE",
    "notices/NOTICE",
    "notices/THIRD_PARTY_NOTICES.md",
    "schemas/manifest.schema.json",
    "schemas/operations.schema.json",
    "bootstrap/build_windows.ps1",
    "docs/README.md",
    "docs/PROJECT_README.md",
    "docs/mod-format.md",
    "docs/mod-api.md",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def windows_path(path: Path) -> str:
    try:
        result = subprocess.run(
            ["wslpath", "-w", str(path)],
            check=True,
            capture_output=True,
            text=True,
        )
        converted = result.stdout.strip()
        if converted:
            return converted
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass
    return str(path)


def write_bundle(path: Path, license_text: str) -> Path:
    package_name = "SoulMod-0.1.0"
    payloads = {
        relative_path: f"fixture:{relative_path}\n".encode("utf-8")
        for relative_path in REQUIRED_PATHS
    }
    payloads["notices/LICENSE"] = license_text.encode("utf-8")
    payloads["notices/THIRD_PARTY_NOTICES.md"] = b"Aurie Framework\nYYToolkit\n"
    payloads["docs/README.md"] = b"An execution copy is required; game files are not included.\n"
    records = [
        {
            "path": relative_path,
            "sha256": hashlib.sha256(content).hexdigest(),
            "size": len(content),
        }
        for relative_path, content in sorted(payloads.items())
    ]
    manifest = {
        "schemaVersion": 1,
        "packageName": package_name,
        "version": "0.1.0",
        "executionCopyOnly": True,
        "files": records,
    }
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for relative_path, content in sorted(payloads.items()):
            archive.writestr(f"{package_name}/{relative_path}", content)
        archive.writestr(
            f"{package_name}/bundle.manifest.json",
            json.dumps(manifest, separators=(",", ":")).encode("utf-8"),
        )
    companion = path.with_suffix(".zip.sha256")
    companion.write_text(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n",
        encoding="utf-8",
    )
    return companion


def validate(bundle: Path, companion: Path, install: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            windows_path(VALIDATE_RELEASE),
            "-BundlePath",
            windows_path(bundle),
            "-ArchiveSha256Path",
            windows_path(companion),
            "-InstallDirectory",
            windows_path(install),
        ],
        capture_output=True,
        text=True,
        errors="replace",
    )


def test_release_validator_rejects_incomplete_license_and_accepts_complete_license() -> None:
    with tempfile.TemporaryDirectory(prefix="soulmod-delivery-") as directory:
        root = Path(directory)
        incomplete = root / "incomplete.zip"
        incomplete_companion = write_bundle(incomplete, "GNU GENERAL PUBLIC LICENSE\n[Full GPL-3.0 License Text]\n")
        incomplete_result = validate(incomplete, incomplete_companion, root / "incomplete-install")
        require(incomplete_result.returncode != 0, "incomplete project license must reject the release bundle")
        require("ERROR[LICENSE_INCOMPLETE]" in incomplete_result.stderr, incomplete_result.stderr)

        complete_license = (ROOT / "LICENSE").read_text(encoding="utf-8")
        require("END OF TERMS AND CONDITIONS" in complete_license, "repository LICENSE must contain the complete GPL terms")
        require("[Full GPL-3.0 License Text]" not in complete_license, "repository LICENSE must not contain a placeholder")
        complete = root / "complete.zip"
        complete_companion = write_bundle(complete, complete_license)
        complete_result = validate(complete, complete_companion, root / "complete-install")
        require(complete_result.returncode == 0, complete_result.stderr)
        require("RELEASE_VALID" in complete_result.stdout, complete_result.stdout)


def test_generated_runtime_build_trees_are_ignored_without_hiding_source() -> None:
    for build_tree in ("runtime/build-task8", "runtime/build-task23", "runtime/build-task26-red", "runtime/build-task27"):
        ignored = subprocess.run(
            ["git", "check-ignore", "-q", build_tree], cwd=ROOT, check=False
        )
        require(ignored.returncode == 0, f"{build_tree} must be ignored as generated build output")
    source = subprocess.run(
        ["git", "check-ignore", "-q", "runtime/SoulMod.Runtime/src/ModLoader.cpp"], cwd=ROOT, check=False
    )
    require(source.returncode == 1, "runtime source must remain visible to git")


def test_unsafe_legacy_host_and_solution_admission_are_absent() -> None:
    importer = (ROOT / "converter" / "SoulMod.Importer.Csx" / "CsxImporter.cs").read_text(encoding="utf-8")
    solution = (ROOT / "converter" / "SoulMod.Converter.sln").read_text(encoding="utf-8")
    require("CreateHostStartInfo" not in importer, "retired direct host launcher must be removed")
    require("cmd.exe" not in importer, "CSX importer must not retain a Windows command-processor launch path")
    require("IsolationUnavailable" in importer, "CSX importer must retain non-Linux fail-closed behavior")
    require('Project(' not in "\n".join(line for line in solution.splitlines() if "SoulMod.Injector" in line), "legacy injector must not be a solution member")


def main() -> int:
    tests = (
        test_release_validator_rejects_incomplete_license_and_accepts_complete_license,
        test_generated_runtime_build_trees_are_ignored_without_hiding_source,
        test_unsafe_legacy_host_and_solution_admission_are_absent,
    )
    failures: list[str] = []
    for test in tests:
        try:
            test()
            print(f"PASS {test.__name__}")
        except (AssertionError, subprocess.CalledProcessError, OSError) as error:
            failures.append(f"FAIL {test.__name__}: {error}")
            print(failures[-1])
    if failures:
        return 1
    print("RESULT: PASS (delivery hygiene)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
