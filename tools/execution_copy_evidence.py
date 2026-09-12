#!/usr/bin/env python3
"""Manual QA driver for the execution-copy boundary oracle.

Creates a temporary fake install (EXE + data.win + extra files), exercises the copy-only happy
flow and the failure flows, proves source SHA-256 + directory integrity are unchanged across
each, demonstrates partial-copy cleanup, and writes the evidence report to
``.omo/evidence/wave-2/execution-copy-report.txt``. All temp artifacts are removed afterwards.

This is the runnable equivalent of the xunit manual-QA gate; the C# manager is the canonical
artifact but cannot execute on this Linux host (no .NET SDK).
"""

from __future__ import annotations

import datetime
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import execution_copy_contract as ecc  # noqa: E402
import execution_copy_internals as eci  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
EVIDENCE_DIR = REPO_ROOT / ".omo" / "evidence" / "wave-2"
EXE_NAME = "FakeGame.exe"
DATA_WIN_NAME = "data.win"


def _now_iso() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def _make_fake_install(root: str) -> ecc.GameInstall:
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, EXE_NAME), "wb") as handle:
        handle.write(bytes([0x4D, 0x5A, 0x90, 0x00]))
    with open(os.path.join(root, DATA_WIN_NAME), "w", encoding="utf-8") as handle:
        handle.write("FORM fake data.win payload")
    os.makedirs(os.path.join(root, "mus"), exist_ok=True)
    with open(os.path.join(root, "mus", "legend.ogg"), "w", encoding="utf-8") as handle:
        handle.write("audio")
    with open(os.path.join(root, "audiogroup1.dat"), "w", encoding="utf-8") as handle:
        handle.write("ag1")
    return ecc.GameInstall(root, EXE_NAME, DATA_WIN_NAME)


def _exe_sha(install: ecc.GameInstall) -> str:
    return eci.sha256_file(install.executable_path)


def _data_sha(install: ecc.GameInstall) -> str:
    return eci.sha256_file(install.data_win_path)


def _dir_manifest(install: ecc.GameInstall) -> str:
    return eci.directory_manifest_sha256(install.root_path)


def main() -> int:
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    report_path = EVIDENCE_DIR / "execution-copy-report.txt"
    lines: list[str] = []
    work = tempfile.mkdtemp(prefix="soulmod-exec-qa-")

    try:
        install = _make_fake_install(os.path.join(work, "install"))
        managed = os.path.join(work, "managed")

        lines.append("SoulMod execution-copy boundary - manual QA evidence")
        lines.append(f"generated_utc: {_now_iso()}")
        lines.append(f"host: {sys.platform}")
        lines.append(f"oracle: tools/execution_copy_contract.py (mirrors converter/SoulMod.ExecutionCopy/)")
        lines.append("")
        lines.append("=== Source install (immutable input) ===")
        lines.append(f"install_root: {install.root_path}")
        exe_before = _exe_sha(install)
        data_before = _data_sha(install)
        dir_before = _dir_manifest(install)
        lines.append(f"executable_sha256_before: {exe_before}")
        lines.append(f"data_win_sha256_before:    {data_before}")
        lines.append(f"directory_manifest_before: {dir_before}")
        lines.append("")

        lines.append("=== Copy-only flow (happy path) ===")
        outcome = ecc.prepare_copy(install, managed)
        if not isinstance(outcome, ecc.ExecutionCopy):
            lines.append(f"RESULT: UNEXPECTED FAILURE: {outcome}")
            _write(report_path, lines)
            return 1
        lines.append(f"managed_root: {outcome.managed_root}")
        lines.append(f"managed_executable: {outcome.executable_path}")
        lines.append(f"managed_data_win: {outcome.data_win_path}")
        lines.append(f"run_id: {outcome.run_id}")
        lines.append(f"source_before_dir_manifest: {outcome.source_before.directory_manifest_sha256}")
        lines.append(f"source_after_dir_manifest:  {outcome.source_after.directory_manifest_sha256}")
        lines.append(f"copy_dir_manifest:          {outcome.copy_integrity.directory_manifest_sha256}")
        happy_source_unchanged = outcome.source_before == outcome.source_after
        happy_copy_matches = (
            outcome.copy_integrity.executable.sha256 == outcome.source_before.executable.sha256
            and outcome.copy_integrity.data_win.sha256 == outcome.source_before.data_win.sha256
            and outcome.copy_integrity.directory_manifest_sha256 == outcome.source_before.directory_manifest_sha256
        )
        lines.append(f"source_before==source_after (immutability): {happy_source_unchanged}")
        lines.append(f"copy_integrity==source (faithful copy):     {happy_copy_matches}")
        lines.append("")

        lines.append("=== Source re-hash after copy (independent of manager output) ===")
        exe_after_happy = _exe_sha(install)
        data_after_happy = _data_sha(install)
        dir_after_happy = _dir_manifest(install)
        lines.append(f"executable_sha256_after: {exe_after_happy}")
        lines.append(f"data_win_sha256_after:    {data_after_happy}")
        lines.append(f"directory_manifest_after: {dir_after_happy}")
        exe_unchanged = exe_before == exe_after_happy
        data_unchanged = data_before == data_after_happy
        dir_unchanged = dir_before == dir_after_happy
        lines.append(f"executable_unchanged: {exe_unchanged}")
        lines.append(f"data_win_unchanged:    {data_unchanged}")
        lines.append(f"directory_unchanged:   {dir_unchanged}")
        lines.append("")

        lines.append("=== Failure flow 1: source==target overlap (source must remain unchanged) ===")
        overlap_outcome = ecc.prepare_copy(install, os.path.join(work, "install"))
        lines.append(f"outcome: {overlap_outcome.reason.value if isinstance(overlap_outcome, ecc.Failure) else 'UNEXPECTED SUCCESS'}")
        overlap_exe = _exe_sha(install)
        overlap_data = _data_sha(install)
        overlap_dir = _dir_manifest(install)
        lines.append(f"executable_unchanged_after_overlap_rejection: {exe_before == overlap_exe}")
        lines.append(f"data_win_unchanged_after_overlap_rejection:    {data_before == overlap_data}")
        lines.append(f"directory_unchanged_after_overlap_rejection:   {dir_before == overlap_dir}")
        overlap_ok = isinstance(overlap_outcome, ecc.Failure) and overlap_outcome.reason is ecc.FailureReason.SOURCE_TARGET_OVERLAP
        lines.append(f"typed_failure_reason_correct: {overlap_ok}")
        lines.append("")

        lines.append("=== Failure flow 2: unowned existing managed root (must be preserved) ===")
        foreign = os.path.join(work, "foreign-managed")
        os.makedirs(foreign)
        user_file = os.path.join(foreign, "user-owned.txt")
        with open(user_file, "w", encoding="utf-8") as handle:
            handle.write("do not touch")
        foreign_outcome = ecc.prepare_copy(install, foreign)
        lines.append(f"outcome: {foreign_outcome.reason.value if isinstance(foreign_outcome, ecc.Failure) else 'UNEXPECTED SUCCESS'}")
        lines.append(f"user_file_preserved: {os.path.isfile(user_file)}")
        foreign_ok = isinstance(foreign_outcome, ecc.Failure) and foreign_outcome.reason is ecc.FailureReason.STALE_PARTIAL_UNOWNED
        lines.append(f"typed_failure_reason_correct: {foreign_ok}")
        lines.append("")

        lines.append("=== Persisted managed-root hash equality (recorded vs actual on disk) ===")
        persisted_equals_recorded = outcome.persisted_directory_manifest_sha256 == outcome.copy_integrity.directory_manifest_sha256
        recomputed_persisted = eci.directory_manifest_sha256(managed, (".soulmod-copy-manifest.json", ".soulmod-ownership.json"))
        recomputed_equals_recorded = recomputed_persisted == outcome.copy_integrity.directory_manifest_sha256
        lines.append(f"recorded_persisted_sha256: {outcome.persisted_directory_manifest_sha256}")
        lines.append(f"copy_recorded_sha256:      {outcome.copy_integrity.directory_manifest_sha256}")
        lines.append(f"recomputed_managed_sha256: {recomputed_persisted}")
        lines.append(f"persisted==recorded: {persisted_equals_recorded}")
        lines.append(f"recomputed==recorded: {recomputed_equals_recorded}")
        lines.append("")

        lines.append("=== Rollback-safe promotion: prior owned root survives injected promotion failure ===")
        rollback_target = os.path.join(work, "rollback-managed")
        rollback_first = ecc.prepare_copy(install, rollback_target)
        rollback_first_ok = isinstance(rollback_first, ecc.ExecutionCopy)
        rollback_sentinel = os.path.join(rollback_target, "SENTinel.txt")
        if rollback_first_ok:
            with open(rollback_sentinel, "w", encoding="utf-8") as handle:
                handle.write("must survive")

            def _failing_promote(_src, _dst):
                raise OSError("injected promotion failure")

            rollback_second = ecc.prepare_copy(install, rollback_target, promote=_failing_promote)
            rollback_second_failed = isinstance(rollback_second, ecc.Failure) and rollback_second.reason is ecc.FailureReason.COPY_FAILED
        else:
            rollback_second = None
            rollback_second_failed = False
        rollback_sentinel_survives = rollback_first_ok and os.path.isfile(rollback_sentinel)
        rollback_orphan_staging = [d for d in os.listdir(work) if d.startswith(".soulmod-staging-")] if rollback_first_ok else []
        rollback_orphan_backup = [d for d in os.listdir(work) if ".soulmod-rollback-" in d] if rollback_first_ok else []
        lines.append(f"first_copy_ok: {rollback_first_ok}")
        lines.append(f"second_copy_failed_with_CopyFailed: {rollback_second_failed}")
        lines.append(f"prior_sentinel_survives: {rollback_sentinel_survives}")
        lines.append(f"orphan_staging_left: {rollback_orphan_staging}")
        lines.append(f"orphan_rollback_backup_left: {rollback_orphan_backup}")
        rollback_ok = rollback_first_ok and rollback_second_failed and rollback_sentinel_survives and rollback_orphan_staging == [] and rollback_orphan_backup == []
        lines.append(f"rollback_contract_holds: {rollback_ok}")
        lines.append("")

        lines.append("=== Staging ownership: unowned prefix-matching sibling preserved by cleanup ===")
        staging_parent = os.path.join(work, "staging-parent")
        os.makedirs(staging_parent)
        staging_target = os.path.join(staging_parent, "target")
        os.makedirs(staging_target)
        user_staging = os.path.join(staging_parent, ".soulmod-staging-user-owned")
        os.makedirs(user_staging)
        with open(os.path.join(user_staging, "keep.txt"), "w", encoding="utf-8") as handle:
            handle.write("user data")
        staging_cleanup = ecc.clean_partial(staging_target)
        staging_user_preserved = os.path.isdir(user_staging) and os.path.isfile(os.path.join(user_staging, "keep.txt"))
        staging_target_refused = isinstance(staging_cleanup, ecc.Failure) and staging_cleanup.reason is ecc.FailureReason.STALE_PARTIAL_UNOWNED
        lines.append(f"cleanup_outcome: {staging_cleanup.reason.value if isinstance(staging_cleanup, ecc.Failure) else staging_cleanup}")
        lines.append(f"unowned_target_refused: {staging_target_refused}")
        lines.append(f"user_staging_sibling_preserved: {staging_user_preserved}")
        staging_ok = staging_target_refused and staging_user_preserved
        lines.append(f"staging_ownership_contract_holds: {staging_ok}")
        lines.append("")

        lines.append("=== Forged install: escaped child rejected at the copy seam ===")
        forged = ecc.GameInstall(install.root_path, "../outside.exe", DATA_WIN_NAME)
        forged_outcome = ecc.prepare_copy(forged, os.path.join(work, "forged-managed"))
        forged_ok = isinstance(forged_outcome, ecc.Failure) and forged_outcome.reason is ecc.FailureReason.INVALID_PATH
        lines.append(f"outcome: {forged_outcome.reason.value if isinstance(forged_outcome, ecc.Failure) else 'UNEXPECTED SUCCESS'}")
        lines.append(f"escaped_child_rejected: {forged_ok}")
        lines.append("")

        lines.append("=== Partial-copy cleanup (interruption then clean_partial) ===")
        cancel_outcome = ecc.prepare_copy(install, os.path.join(work, "cancelled-managed"), cancel_check=lambda: True)
        lines.append(f"cancelled_outcome: {cancel_outcome.reason.value if isinstance(cancel_outcome, ecc.Failure) else 'UNEXPECTED SUCCESS'}")
        cancelled_staging = [d for d in os.listdir(work) if d.startswith(".soulmod-staging-")]
        lines.append(f"staging_tree_left_after_cancel: {cancelled_staging}")
        cleanup = ecc.clean_partial(managed)
        lines.append(f"cleanup_receipt: removed_managed_root={cleanup.removed_managed_root}, removed_staging_trees={cleanup.removed_staging_trees}" if isinstance(cleanup, ecc.CleanupReceipt) else f"cleanup_outcome: {cleanup}")
        lines.append(f"managed_root_exists_after_cleanup: {os.path.isdir(managed)}")
        final_exe = _exe_sha(install)
        final_data = _data_sha(install)
        final_dir = _dir_manifest(install)
        lines.append(f"executable_unchanged_after_all_flows: {exe_before == final_exe}")
        lines.append(f"data_win_unchanged_after_all_flows:    {data_before == final_data}")
        lines.append(f"directory_unchanged_after_all_flows:   {dir_before == final_dir}")
        cleanup_ok = (
            isinstance(cancel_outcome, ecc.Failure) and cancel_outcome.reason is ecc.FailureReason.COPY_CANCELLED
            and cancelled_staging == []
            and isinstance(cleanup, ecc.CleanupReceipt) and cleanup.removed_managed_root
            and not os.path.isdir(managed)
        )
        lines.append(f"cleanup_contract_holds: {cleanup_ok}")
        lines.append("")

        verdict = (
            happy_source_unchanged and happy_copy_matches and exe_unchanged and data_unchanged and dir_unchanged
            and overlap_ok and foreign_ok and cleanup_ok
            and persisted_equals_recorded and recomputed_equals_recorded
            and rollback_ok and staging_ok and forged_ok
        )
        lines.append(f"OVERALL VERDICT: {'PASS' if verdict else 'FAIL'}")

        _write(report_path, lines)
        print(f"wrote {report_path}")
        print(f"OVERALL VERDICT: {'PASS' if verdict else 'FAIL'}")
        return 0 if verdict else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


def _write(path: Path, lines: list[str]) -> None:
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
