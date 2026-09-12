#!/usr/bin/env python3
"""Contract tests for the execution-copy boundary oracle.

Mirrors the xunit suite in ``converter/SoulMod.ExecutionCopy.Tests/ExecutionCopyManagerTests.cs``.
Run on Linux to prove the core boundary contract holds without needing the .NET SDK.
"""

from __future__ import annotations

import os

from execution_copy_test_support import (
    DATA_WIN_NAME,
    EXE_NAME,
    TempTree,
    check,
    make_fake_install,
    run,
)

import execution_copy_contract as ecc


def test_happy_copy_proves_source_unchanged_and_copy_matches():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"), with_extra=True)
        managed = os.path.join(tree.path, "managed")
        outcome = ecc.prepare_copy(install, managed)
        check(isinstance(outcome, ecc.ExecutionCopy), f"happy copy failed: {outcome}")
        check(os.path.isfile(outcome.executable_path), "managed EXE missing")
        check(os.path.isfile(outcome.data_win_path), "managed data.win missing")
        check(os.path.isfile(os.path.join(outcome.managed_root, ".soulmod-copy-manifest.json")), "manifest missing")
        check(outcome.source_before == outcome.source_after, "source integrity changed")
        check(outcome.source_before.executable.sha256 == outcome.copy_integrity.executable.sha256, "exe hash mismatch")
        check(outcome.source_before.data_win.sha256 == outcome.copy_integrity.data_win.sha256, "data.win hash mismatch")
        check(outcome.source_before.directory_manifest_sha256 == outcome.copy_integrity.directory_manifest_sha256, "dir manifest mismatch")
    finally:
        tree.cleanup()


def test_overlap_target_equals_source_rejected():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        outcome = ecc.prepare_copy(install, os.path.join(tree.path, "install"))
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.SOURCE_TARGET_OVERLAP, f"expected overlap: {outcome}")
        check(os.path.isfile(install.executable_path), "source exe touched on rejected copy")
    finally:
        tree.cleanup()


def test_overlap_target_inside_source_rejected():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        outcome = ecc.prepare_copy(install, os.path.join(tree.path, "install", "managed"))
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.SOURCE_TARGET_OVERLAP, f"expected overlap: {outcome}")
    finally:
        tree.cleanup()


def test_overlap_source_inside_target_rejected():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "container", "install"))
        outcome = ecc.prepare_copy(install, os.path.join(tree.path, "container"))
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.SOURCE_TARGET_OVERLAP, f"expected overlap: {outcome}")
    finally:
        tree.cleanup()


def test_missing_executable_rejected():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        os.remove(install.executable_path)
        _install, _integrity, failure = ecc.resolve_install(install.root_path, EXE_NAME, DATA_WIN_NAME)
        check(isinstance(failure, ecc.Failure) and failure.reason is ecc.FailureReason.SOURCE_EXECUTABLE_MISSING, f"expected missing exe: {failure}")
    finally:
        tree.cleanup()


def test_missing_data_win_rejected():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        os.remove(install.data_win_path)
        _install, _integrity, failure = ecc.resolve_install(install.root_path, EXE_NAME, DATA_WIN_NAME)
        check(isinstance(failure, ecc.Failure) and failure.reason is ecc.FailureReason.SOURCE_DATA_WIN_MISSING, f"expected missing data.win: {failure}")
    finally:
        tree.cleanup()


def test_nonexistent_install_root_rejected():
    import tempfile
    _install, _integrity, failure = ecc.resolve_install(os.path.join(tempfile.gettempdir(), "soulmod-nope-" + os.urandom(4).hex()), EXE_NAME, DATA_WIN_NAME)
    check(isinstance(failure, ecc.Failure) and failure.reason is ecc.FailureReason.INVALID_INSTALL_ROOT, f"expected invalid root: {failure}")


def test_unowned_existing_managed_root_refused_and_preserved():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        managed = os.path.join(tree.path, "managed")
        os.makedirs(managed)
        user_file = os.path.join(managed, "user-owned.txt")
        with open(user_file, "w", encoding="utf-8") as handle:
            handle.write("do not touch")
        outcome = ecc.prepare_copy(install, managed)
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.STALE_PARTIAL_UNOWNED, f"expected unowned refusal: {outcome}")
        check(os.path.isfile(user_file), "unowned managed root was deleted")
    finally:
        tree.cleanup()


def test_owned_managed_root_idempotently_replaced():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        managed = os.path.join(tree.path, "managed")
        first = ecc.prepare_copy(install, managed)
        check(isinstance(first, ecc.ExecutionCopy), f"first copy failed: {first}")
        second = ecc.prepare_copy(install, managed)
        check(isinstance(second, ecc.ExecutionCopy), f"second copy failed: {second}")
        check(first.run_id != second.run_id, "run ids collided")
        check(os.path.isfile(second.executable_path), "managed exe missing after replace")
    finally:
        tree.cleanup()


def test_cancellation_removes_partial_staging():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"), with_extra=True)
        managed = os.path.join(tree.path, "managed")
        outcome = ecc.prepare_copy(install, managed, cancel_check=lambda: True)
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.COPY_CANCELLED, f"expected cancelled: {outcome}")
        check(not os.path.isdir(managed), "managed root created despite cancellation")
        staging = [d for d in os.listdir(tree.path) if d.startswith(".soulmod-staging-")]
        check(staging == [], f"staging tree leaked: {staging}")
    finally:
        tree.cleanup()


def test_clean_partial_idempotent_and_removes_owned_root():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        managed = os.path.join(tree.path, "managed")
        prepared = ecc.prepare_copy(install, managed)
        check(isinstance(prepared, ecc.ExecutionCopy), "setup copy failed")
        first = ecc.clean_partial(managed)
        check(isinstance(first, ecc.CleanupReceipt) and first.removed_managed_root, f"first cleanup failed: {first}")
        second = ecc.clean_partial(managed)
        check(isinstance(second, ecc.CleanupReceipt) and not second.removed_managed_root, f"second cleanup should be no-op: {second}")
        check(not os.path.isdir(managed), "managed root remains after cleanup")
    finally:
        tree.cleanup()


def test_clean_partial_refuses_unowned_root():
    tree = TempTree()
    try:
        managed = os.path.join(tree.path, "managed")
        os.makedirs(managed)
        user_file = os.path.join(managed, "user.txt")
        with open(user_file, "w", encoding="utf-8") as handle:
            handle.write("mine")
        outcome = ecc.clean_partial(managed)
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.STALE_PARTIAL_UNOWNED, f"expected unowned refusal: {outcome}")
        check(os.path.isfile(user_file), "unowned root deleted by cleanup")
    finally:
        tree.cleanup()


def test_directory_manifest_byte_stable_across_runs():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"), with_extra=True)
        _inst_a, integrity_a, failure_a = ecc.resolve_install(install.root_path, EXE_NAME, DATA_WIN_NAME)
        _inst_b, integrity_b, failure_b = ecc.resolve_install(install.root_path, EXE_NAME, DATA_WIN_NAME)
        check(failure_a is None and failure_b is None, "resolve failed")
        check(integrity_a.directory_manifest_sha256 == integrity_b.directory_manifest_sha256, "dir manifest not stable")
        check(integrity_a.executable.sha256 == integrity_b.executable.sha256, "exe hash not stable")
    finally:
        tree.cleanup()


def test_symlink_in_install_rejected():
    if not hasattr(os, "symlink"):
        return
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        os.symlink(os.path.join(tree.path, "outside-target"), os.path.join(install.root_path, "link.exe"))
        outcome = ecc.prepare_copy(install, os.path.join(tree.path, "managed"))
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.SYMLINK_REJECTED, f"expected symlink rejection: {outcome}")
    finally:
        tree.cleanup()


TESTS = [
    test_happy_copy_proves_source_unchanged_and_copy_matches,
    test_overlap_target_equals_source_rejected,
    test_overlap_target_inside_source_rejected,
    test_overlap_source_inside_target_rejected,
    test_missing_executable_rejected,
    test_missing_data_win_rejected,
    test_nonexistent_install_root_rejected,
    test_unowned_existing_managed_root_refused_and_preserved,
    test_owned_managed_root_idempotently_replaced,
    test_cancellation_removes_partial_staging,
    test_clean_partial_idempotent_and_removes_owned_root,
    test_clean_partial_refuses_unowned_root,
    test_directory_manifest_byte_stable_across_runs,
    test_symlink_in_install_rejected,
]


if __name__ == "__main__":
    raise SystemExit(run(TESTS))
