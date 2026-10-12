#!/usr/bin/env python3
"""Contract tests for the separate managed and Windows CI lanes.

Run directly from the repository root::

    python3 tools/ci_contract_test.py

The tests inspect machine-consumed workflow structure and exercise the
missing-artifact failure path. They do not assert prose or generated success
logs as proof of execution.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "contract-and-windows.yml"
VALIDATOR = ROOT / "tools" / "ci_validation.py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_validator(root: Path, arguments: Sequence[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(VALIDATOR), *arguments],
        check=False,
        capture_output=True,
        text=True,
        cwd=root,
        env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"},
    )


def test_workflow_contract() -> None:
    workflow = WORKFLOW.read_text(encoding="utf-8")
    required_fragments = (
        "managed-contract:",
        "windows-native:",
        "runs-on: ubuntu-latest",
        "runs-on: [self-hosted, windows, x64, soulmod-native]",
        "actions/checkout@v4",
        "actions/setup-python@v5",
        "actions/setup-dotnet@v4",
        "actions/upload-artifact@v4",
        "if: always()",
        "tools/build_windows.ps1",
        "-UmtDependencyDirectory",
        "-AurieRoot",
        "SOULMOD_GAME_FIXTURES_PROVISIONED",
        "fixture-gate",
        "require-artifacts",
        "integrity",
        ".omo/evidence/wave-1/ci-managed",
        ".omo/evidence/wave-1/ci-windows",
    )
    for fragment in required_fragments:
        require(fragment in workflow, f"workflow missing required contract: {fragment}")

    require(
        workflow.index("managed-contract:") < workflow.index("windows-native:"),
        "managed and Windows jobs must remain distinct workflow jobs",
    )
    require(
        workflow.count("actions/upload-artifact@v4") >= 2,
        "each lane must upload its evidence independently",
    )
    require(
        "dotnet test" not in workflow,
        "Windows lane must invoke the stable entrypoint rather than duplicate its build logic",
    )
    require(
        "github.event_name == 'workflow_dispatch'" in workflow,
        "Windows lane must remain explicitly dispatchable with provisioned inputs",
    )
    require(
        "vars.SOULMOD_WINDOWS_NATIVE_AVAILABLE == 'true'" in workflow,
        "Windows lane must require an explicit runner-availability variable",
    )
    require(
        "contains(github.event.pull_request.labels.*.name, 'windows-native')" in workflow,
        "pull requests must opt into the Windows lane with the windows-native label",
    )
    require(
        "github.event_name == 'pull_request'" in workflow,
        "Windows lane must support explicitly labeled pull requests",
    )
    require(
        "github.event_name == 'workflow_dispatch' ||" in workflow,
        "manual dispatch must remain independently available",
    )
    require(
        "needs:" not in workflow[workflow.index("windows-native:"):],
        "managed contracts must not require the optional native job",
    )
    require(
        '--path "$stdout_file"' not in workflow and '--path "$stderr_file"' not in workflow,
        "workflow artifact validation must pass root-relative paths",
    )
    require(
        '--path ".omo/evidence/wave-1/ci-managed/managed-stdout.txt"' in workflow,
        "managed stdout artifact must be root-relative",
    )
    require(
        '--path ".omo/evidence/wave-1/ci-windows/windows-stdout.txt"' in workflow,
        "Windows stdout artifact must be root-relative",
    )
    game_cleanup = workflow.rindex("Remove-Item -LiteralPath $gameRoot")
    stale_game_clear = workflow.index("Remove-Item -LiteralPath $gameRoot")
    fixture_copy = workflow.index("Copy-Item -Path (Join-Path $env:SOULMOD_GAME_FIXTURE_SOURCE '*')")
    finally_block = workflow.index("finally {")
    require(stale_game_clear < fixture_copy, "stale game fixture root must be cleared before copy")
    require(fixture_copy < finally_block < game_cleanup, "Windows game fixture cleanup must run in finally")
    require(workflow.count("Remove-Item -LiteralPath $gameRoot") >= 2, "stale and final game roots must both be removed")


def test_missing_artifact_is_nonzero() -> None:
    with tempfile.TemporaryDirectory(prefix="soulmod-ci-contract-") as temporary:
        root = Path(temporary)
        existing = root / "present.txt"
        existing.write_text("real evidence\n", encoding="utf-8")
        missing = root / "missing.txt"
        completed = run_validator(
            root,
            ["require-artifacts", "--root", ".", "--path", "present.txt", "--path", "missing.txt"],
        )
        require(completed.returncode != 0, "missing artifact must fail the validation command")
        require("MISSING_ARTIFACT" in completed.stderr, "missing artifact failure must be explicit")


def test_validator_without_a_subcommand_is_a_successful_usage_request() -> None:
    completed = run_validator(ROOT, [])
    require(completed.returncode == 0, "validator usage request must not fail")
    require("{fixture-gate,record-result,require-artifacts}" in completed.stdout, "usage must list validation commands")


def test_artifact_paths_are_root_contained() -> None:
    with tempfile.TemporaryDirectory(prefix="soulmod-ci-containment-") as temporary:
        root = Path(temporary) / "root"
        root.mkdir()
        outside = Path(temporary) / "outside.txt"
        outside.write_text("outside evidence\n", encoding="utf-8")
        evidence = root / "evidence"
        evidence.mkdir()
        (evidence / "present.txt").write_text("valid evidence\n", encoding="utf-8")
        link = evidence / "outside-link.txt"
        try:
            link.symlink_to(outside)
        except OSError:
            print("SKIP test_artifact_paths_are_root_contained: symlink creation not permitted on this host")
            return

        for candidate in (str(outside), "../outside.txt", "evidence/outside-link.txt"):
            completed = run_validator(
                root,
                ["require-artifacts", "--root", ".", "--path", candidate],
            )
            require(completed.returncode != 0, f"outside artifact path must fail: {candidate}")
            require("ARTIFACT_PATH_ESCAPE" in completed.stderr, f"escape must be typed: {candidate}")
        integrity_escape = run_validator(
            root,
            [
                "require-artifacts",
                "--root",
                ".",
                "--path",
                "evidence/present.txt",
                "--write-integrity",
                str(outside),
            ],
        )
        require(integrity_escape.returncode != 0, "outside integrity output must fail")
        require("ARTIFACT_PATH_ESCAPE" in integrity_escape.stderr, "integrity escape must be typed")


def test_valid_relative_integrity_is_complete() -> None:
    with tempfile.TemporaryDirectory(prefix="soulmod-ci-integrity-") as temporary:
        root = Path(temporary)
        evidence = root / "evidence"
        evidence.mkdir()
        (evidence / "stdout.txt").write_text("real stdout\n", encoding="utf-8")
        (evidence / "stderr.txt").write_text("[no stderr]\n", encoding="utf-8")
        report = evidence / "result.txt"
        recorded = run_validator(
            root,
            [
                "record-result",
                "--stdout",
                "evidence/stdout.txt",
                "--stderr",
                "evidence/stderr.txt",
                "--exit-code",
                "0",
                "--report",
                "evidence/result.txt",
            ],
        )
        require(recorded.returncode == 0, "valid relative result capture must pass")
        integrity = evidence / "integrity.txt"
        required = ["evidence/stdout.txt", "evidence/stderr.txt", "evidence/result.txt"]
        artifact_arguments = [item for path in required for item in ("--path", path)]
        generated = run_validator(
            root,
            ["require-artifacts", "--root", ".", *artifact_arguments, "--write-integrity", "evidence/integrity.txt"],
        )
        require(generated.returncode == 0, "valid relative artifacts must pass")
        checked = run_validator(
            root,
            ["require-artifacts", "--root", ".", *artifact_arguments, "--path", "evidence/integrity.txt"],
        )
        require(checked.returncode == 0, "complete integrity bundle must pass")
        require(report.is_file() and integrity.is_file(), "result and integrity files must exist")
        for line in integrity.read_text(encoding="utf-8").splitlines()[1:]:
            path_text, hash_text, bytes_text = line.split()
            artifact = root / path_text
            require(artifact.is_file(), f"integrity references missing artifact: {path_text}")
            require(hash_text.startswith("sha256="), f"integrity hash missing: {path_text}")
            require(bytes_text == f"bytes={artifact.stat().st_size}", f"integrity size mismatch: {path_text}")


def main() -> int:
    test_workflow_contract()
    test_missing_artifact_is_nonzero()
    test_validator_without_a_subcommand_is_a_successful_usage_request()
    test_artifact_paths_are_root_contained()
    test_valid_relative_integrity_is_complete()
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
