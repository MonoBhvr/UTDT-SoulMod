#!/usr/bin/env python3
"""Small, dependency-free CI evidence and fixture gate.

How to run::

    python3 tools/ci_validation.py fixture-gate --root . --provisioned true
    python3 tools/ci_validation.py record-result --stdout out --stderr err \
        --exit-code 0 --report result.txt
    python3 tools/ci_validation.py require-artifacts --root . \
        --path report.txt --write-integrity integrity.txt

The commands inspect exit codes and files. They never treat a success message
as evidence that a command or test actually ran.
"""

# /// script
# requires-python = ">=3.9"
# ///

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path
from typing import NoReturn, Sequence


GAME_FIXTURE = Path("scratch/test_game/chapter1_windows/data.win")


def fail(message: str) -> NoReturn:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def display_path(path: Path) -> str:
    return path.name if path.is_absolute() else path.as_posix()


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        fail(f"MISSING_{label.upper()}: {display_path(path)}")
    if path.stat().st_size == 0:
        fail(f"EMPTY_{label.upper()}: {display_path(path)}")


def resolve_artifact_path(root: Path, requested: str) -> Path:
    """Resolve a repository-relative artifact without allowing root escapes."""
    resolved_root = root.resolve(strict=False)
    relative_path = Path(requested)
    if relative_path.is_absolute():
        fail("ARTIFACT_PATH_ESCAPE: absolute artifact paths are not allowed")
    candidate = (resolved_root / relative_path).resolve(strict=False)
    try:
        candidate.relative_to(resolved_root)
    except ValueError:
        fail("ARTIFACT_PATH_ESCAPE: artifact path escapes the approved root")
    return candidate


def fixture_gate(root: Path, provisioned: str) -> int:
    """Require an explicit fixture flag and the clean Chapter 1 fixture."""
    if provisioned.strip().lower() != "true":
        fail("GAME_FIXTURES_NOT_PROVISIONED: set the explicit fixture flag to true")
    require_file(root / GAME_FIXTURE, "GAME_FIXTURE")
    print("GAME_FIXTURE_GATE: PASS")
    return 0


def record_result(stdout: Path, stderr: Path, exit_code: int, report: Path) -> int:
    """Write machine-readable execution evidence from captured process streams."""
    require_file(stdout, "STDOUT_CAPTURE")
    require_file(stderr, "STDERR_CAPTURE")
    report.parent.mkdir(parents=True, exist_ok=True)
    status = "PASS" if exit_code == 0 else "FAIL"
    report.write_text(
        "\n".join(
            (
                "ci_result_version: 1",
                f"status: {status}",
                f"exit_code: {exit_code}",
                f"stdout_bytes: {stdout.stat().st_size}",
                f"stderr_bytes: {stderr.stat().st_size}",
                f"stdout_sha256: {sha256_file(stdout)}",
                f"stderr_sha256: {sha256_file(stderr)}",
                "",
            )
        ),
        encoding="utf-8",
    )
    print(f"RECORDED_RESULT: {status}")
    return 0


def require_artifacts(root: Path, paths: Sequence[str], integrity: Path | None) -> int:
    """Require nonempty evidence files and optionally write their digest manifest."""
    resolved = tuple((path, resolve_artifact_path(root, path)) for path in paths)
    for _, path in resolved:
        require_file(path, "ARTIFACT")
    if integrity is not None:
        integrity_path = resolve_artifact_path(root, str(integrity))
        integrity_path.parent.mkdir(parents=True, exist_ok=True)
        lines = ["ci_integrity_version: 1"]
        for requested, path in resolved:
            lines.append(f"{requested} sha256={sha256_file(path)} bytes={path.stat().st_size}")
        lines.append("")
        integrity_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"REQUIRED_ARTIFACTS: {len(resolved)}")
    return 0


def parser() -> argparse.ArgumentParser:
    command_parser = argparse.ArgumentParser(description=__doc__)
    commands = command_parser.add_subparsers(dest="command", required=True)

    gate = commands.add_parser("fixture-gate")
    gate.add_argument("--root", type=Path, required=True)
    gate.add_argument("--provisioned", required=True)

    result = commands.add_parser("record-result")
    result.add_argument("--stdout", type=Path, required=True)
    result.add_argument("--stderr", type=Path, required=True)
    result.add_argument("--exit-code", type=int, required=True)
    result.add_argument("--report", type=Path, required=True)

    artifacts = commands.add_parser("require-artifacts")
    artifacts.add_argument("--root", type=Path, required=True)
    artifacts.add_argument("--path", action="append", required=True)
    artifacts.add_argument("--write-integrity", type=Path)
    return command_parser


def main(arguments: Sequence[str] | None = None) -> int:
    options = parser().parse_args(arguments)
    if options.command == "fixture-gate":
        return fixture_gate(options.root, options.provisioned)
    if options.command == "record-result":
        return record_result(options.stdout, options.stderr, options.exit_code, options.report)
    if options.command == "require-artifacts":
        return require_artifacts(options.root, options.path, options.write_integrity)
    fail(f"UNKNOWN_COMMAND: {options.command}")


if __name__ == "__main__":
    raise SystemExit(main())
