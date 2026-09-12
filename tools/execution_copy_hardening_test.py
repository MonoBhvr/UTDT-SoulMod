#!/usr/bin/env python3
"""Hardening regression tests for the execution-copy boundary oracle.

Mirrors ``converter/SoulMod.ExecutionCopy.Tests/ExecutionCopyHardeningTests.cs``. Each test
would fail against the pre-hardening manager: forged/escaped children, rollback-safe promotion,
ownership-bound staging cleanup, persisted-hash equality, directory symlinks, case/volume paths.
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
import execution_copy_internals as eci


def test_directory_symlink_in_install_rejected():
    if not hasattr(os, "symlink"):
        return
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"), with_extra=True)
        link_target = os.path.join(tree.path, "outside-dir")
        os.makedirs(link_target)
        os.symlink(link_target, os.path.join(install.root_path, "linked_dir"))
        outcome = ecc.prepare_copy(install, os.path.join(tree.path, "managed"))
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.SYMLINK_REJECTED, f"expected directory symlink rejection: {outcome}")
        check(not os.path.exists(os.path.join(tree.path, "managed")), "managed root created despite directory symlink")
    finally:
        tree.cleanup()


def test_forged_install_with_escaped_child_rejected():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        outside = os.path.join(tree.path, "outside.exe")
        with open(outside, "w", encoding="utf-8") as handle:
            handle.write("outside")
        forged = ecc.GameInstall(install.root_path, "../outside.exe", DATA_WIN_NAME)
        outcome = ecc.prepare_copy(forged, os.path.join(tree.path, "managed"))
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.INVALID_PATH, f"expected escaped-child rejection: {outcome}")
        check(not os.path.exists(os.path.join(tree.path, "managed")), "managed root created from forged install")
    finally:
        tree.cleanup()


def test_forged_install_with_absolute_child_rejected():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"))
        forged = ecc.GameInstall(install.root_path, os.path.join(install.root_path, EXE_NAME), DATA_WIN_NAME)
        outcome = ecc.prepare_copy(forged, os.path.join(tree.path, "managed"))
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.INVALID_PATH, f"expected absolute-child rejection: {outcome}")
    finally:
        tree.cleanup()


def test_promotion_failure_preserves_prior_owned_root():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"), with_extra=True)
        managed = os.path.join(tree.path, "managed")
        first = ecc.prepare_copy(install, managed)
        check(isinstance(first, ecc.ExecutionCopy), f"first copy failed: {first}")
        sentinel = os.path.join(managed, "SENTinel-from-first-run.txt")
        with open(sentinel, "w", encoding="utf-8") as handle:
            handle.write("must survive a failed promotion")

        def failing_promote(_src, _dst):
            raise OSError("injected promotion failure")

        second = ecc.prepare_copy(install, managed, promote=failing_promote)
        check(isinstance(second, ecc.Failure) and second.reason is ecc.FailureReason.COPY_FAILED, f"expected CopyFailed from injected promotion failure: {second}")
        check(os.path.isdir(managed), "prior owned managed root lost on promotion failure")
        check(os.path.isfile(sentinel), "prior owned root sentinel lost on promotion failure")
        staging = [d for d in os.listdir(tree.path) if d.startswith(".soulmod-staging-")]
        check(staging == [], f"orphan staging left after failed promotion: {staging}")
        rollback = [d for d in os.listdir(tree.path) if ".soulmod-rollback-" in d]
        check(rollback == [], f"rollback backup left after failed promotion: {rollback}")
    finally:
        tree.cleanup()


def test_unowned_staging_sibling_preserved_by_cleanup():
    tree = TempTree()
    try:
        managed = os.path.join(tree.path, "managed")
        os.makedirs(managed)
        user_staging = os.path.join(tree.path, ".soulmod-staging-user-owned")
        os.makedirs(user_staging)
        with open(os.path.join(user_staging, "keep.txt"), "w", encoding="utf-8") as handle:
            handle.write("user data")

        outcome = ecc.clean_partial(managed)
        check(isinstance(outcome, ecc.Failure) and outcome.reason is ecc.FailureReason.STALE_PARTIAL_UNOWNED, f"expected unowned refusal: {outcome}")
        check(os.path.isdir(user_staging), "user-created prefix-matching staging directory was deleted")
        check(os.path.isfile(os.path.join(user_staging, "keep.txt")), "user staging content lost")
    finally:
        tree.cleanup()


def test_persisted_directory_manifest_equals_recorded_after_promotion():
    tree = TempTree()
    try:
        install = make_fake_install(os.path.join(tree.path, "install"), with_extra=True)
        managed = os.path.join(tree.path, "managed")
        outcome = ecc.prepare_copy(install, managed)
        check(isinstance(outcome, ecc.ExecutionCopy), f"copy failed: {outcome}")
        check(outcome.persisted_directory_manifest_sha256 == outcome.copy_integrity.directory_manifest_sha256, "persisted hash != recorded copy hash")
        recomputed = eci.directory_manifest_sha256(managed, (".soulmod-copy-manifest.json", ".soulmod-ownership.json"))
        check(recomputed == outcome.copy_integrity.directory_manifest_sha256, "independently recomputed managed-root manifest != recorded")
        check(os.path.isfile(os.path.join(managed, ".soulmod-copy-manifest.json")), "ownership manifest missing from persisted root")
    finally:
        tree.cleanup()


def test_case_sensitive_containment_on_posix():
    if eci.PATH_CASE_INSENSITIVE:
        return
    tree = TempTree()
    try:
        upper = os.path.join(tree.path, "Install")
        lower = os.path.join(tree.path, "install")
        os.makedirs(upper)
        os.makedirs(lower)
        check(eci.check_overlap(upper, lower) is None, "case-sensitive platform flagged distinct-case roots as overlapping")
        check(eci.check_overlap(upper, upper) == "SourceTargetOverlap", "identical roots not flagged as overlapping")
    finally:
        tree.cleanup()


def test_volume_root_not_trimmed():
    normalized, reason = eci.normalize_root(os.path.abspath(os.sep))
    check(reason is None, f"volume root rejected: {reason}")
    check(normalized == os.path.abspath(os.sep), f"volume root trailing separator trimmed: {normalized!r}")


TESTS = [
    test_directory_symlink_in_install_rejected,
    test_forged_install_with_escaped_child_rejected,
    test_forged_install_with_absolute_child_rejected,
    test_promotion_failure_preserves_prior_owned_root,
    test_unowned_staging_sibling_preserved_by_cleanup,
    test_persisted_directory_manifest_equals_recorded_after_promotion,
    test_case_sensitive_containment_on_posix,
    test_volume_root_not_trimmed,
]


if __name__ == "__main__":
    raise SystemExit(run(TESTS))
