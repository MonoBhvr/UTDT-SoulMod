#!/usr/bin/env python3
"""Public API of the SoulMod execution-copy boundary oracle.

Faithful Python reference implementation of the contract shipped in
``converter/SoulMod.ExecutionCopy/`` (Types.cs + ExecutionCopyManager.cs). Exists so the
boundary contract can be exercised on Linux where no .NET SDK is installed; the C# manager is
the canonical artifact for the Windows build lane, and this oracle is its runnable proof
(mirrors the Wave 1 ``tools/build_windows_test.py`` pattern).

Contract invariants (identical in C# and here):
  - Source install is read-only input; only the managed execution root is mutable.
  - A single validation funnel re-checks every (possibly forged) GameInstall inside PrepareCopy;
    escaped/absolute child paths and source-root symlinks cannot reach the copy seam.
  - Source/target overlap is rejected (platform-aware). Symlinks/reparse points are rejected.
  - Copy is staged next to the managed root carrying an ownership marker bound to the target,
    then promoted via rollback-safe rename (prior owned root preserved on failure).
  - Staging cleanup deletes only staging siblings whose marker is bound to the target.
  - Pre/post SHA-256 (EXE, data.win, content manifest excluding SoulMod metadata) proves source
    immutability and that the recorded hash equals the persisted managed root.
"""

from __future__ import annotations

import dataclasses
import os
import shutil
from collections.abc import Callable
from enum import Enum
from typing import Optional

from execution_copy_internals import (
    MANIFEST_FILENAME,
    OWNERSHIP_MARKER_FILENAME,
    STAGING_PREFIX,
    SymlinkInTree,
    check_overlap,
    copy_tree,
    directory_manifest_sha256,
    normalize_child,
    normalize_root,
    promote_with_rollback,
    owns_manifest,
    remove_sibling_staging,
    sha256_file,
    try_delete,
    write_manifest,
    write_staging_marker,
)


class FailureReason(str, Enum):
    INVALID_INSTALL_ROOT = "InvalidInstallRoot"
    SOURCE_EXECUTABLE_MISSING = "SourceExecutableMissing"
    SOURCE_DATA_WIN_MISSING = "SourceDataWinMissing"
    INVALID_MANAGED_ROOT = "InvalidManagedRoot"
    SOURCE_TARGET_OVERLAP = "SourceTargetOverlap"
    SYMLINK_REJECTED = "SymlinkRejected"
    STALE_PARTIAL_UNOWNED = "StalePartialUnowned"
    COPY_CANCELLED = "CopyCancelled"
    COPY_FAILED = "CopyFailed"
    SOURCE_MUTATED_DURING_COPY = "SourceMutatedDuringCopy"
    INVALID_PATH = "InvalidPath"


@dataclasses.dataclass(frozen=True)
class Failure:
    reason: FailureReason
    message: str


@dataclasses.dataclass(frozen=True)
class FileIntegrity:
    path: str
    sha256: str


@dataclasses.dataclass(frozen=True)
class InstallIntegrity:
    executable: FileIntegrity
    data_win: FileIntegrity
    directory_manifest_sha256: str


@dataclasses.dataclass(frozen=True)
class GameInstall:
    root_path: str
    executable_relative: str
    data_win_relative: str

    @property
    def executable_path(self) -> str:
        return os.path.join(self.root_path, self.executable_relative)

    @property
    def data_win_path(self) -> str:
        return os.path.join(self.root_path, self.data_win_relative)


@dataclasses.dataclass(frozen=True)
class ExecutionCopy:
    managed_root: str
    executable_path: str
    data_win_path: str
    source_before: InstallIntegrity
    source_after: InstallIntegrity
    copy_integrity: InstallIntegrity
    persisted_directory_manifest_sha256: str
    run_id: str


@dataclasses.dataclass(frozen=True)
class CleanupReceipt:
    managed_root: str
    removed_managed_root: bool
    removed_staging_trees: int
    run_id: str


METADATA_RELATIVE_PATHS = (MANIFEST_FILENAME, OWNERSHIP_MARKER_FILENAME)


def _generate_run_id() -> str:
    import secrets
    return secrets.token_hex(8)


def _to_failure(reason_name: str, message: str) -> Failure:
    return Failure(FailureReason(reason_name), message)


def _snapshot(install: GameInstall) -> InstallIntegrity:
    exe = FileIntegrity(install.executable_path, sha256_file(install.executable_path))
    data = FileIntegrity(install.data_win_path, sha256_file(install.data_win_path))
    return InstallIntegrity(exe, data, directory_manifest_sha256(install.root_path, METADATA_RELATIVE_PATHS))


def _validate_install(install: GameInstall):
    """The single validation funnel shared by resolve_install and prepare_copy. Returns
    (validated_install, integrity, failure) with exactly one side set. A forged GameInstall with
    escaped/absolute children or a symlinked root cannot pass."""
    normalized, reason = normalize_root(install.root_path)
    if reason is not None:
        return None, None, _to_failure(reason, f"install root not usable: {install.root_path}")
    if not os.path.isdir(normalized):
        return None, None, _to_failure("InvalidInstallRoot", f"install root does not exist: {normalized}")
    for relative, label, missing in (
        (install.executable_relative, "executable", "SourceExecutableMissing"),
        (install.data_win_relative, "data.win", "SourceDataWinMissing"),
    ):
        absolute, child_reason = normalize_child(normalized, relative)
        if child_reason is not None:
            return None, None, _to_failure(child_reason, f"install {label} path not usable: {relative}")
        if not os.path.isfile(absolute):
            return None, None, _to_failure(missing, f"install {label} missing: {absolute}")
    validated = GameInstall(normalized, install.executable_relative, install.data_win_relative)
    try:
        return validated, _snapshot(validated), None
    except SymlinkInTree as exc:
        return None, None, _to_failure("SymlinkRejected", f"symlink inside install rejected: {exc.path}")
    except OSError as exc:
        return None, None, _to_failure("CopyFailed", f"could not read source install: {exc}")


def resolve_install(root_path: str, executable_relative: str, data_win_relative: str):
    return _validate_install(GameInstall(root_path, executable_relative, data_win_relative))


def prepare_copy(install: GameInstall, managed_root_path: str, cancel_check: Optional[Callable[[], bool]] = None, promote: Optional[Callable[[str, str], None]] = None) -> ExecutionCopy | Failure:
    validated, before, failure = _validate_install(install)
    if failure is not None:
        return failure

    promote_fn = promote if promote is not None else os.rename
    normalized_target, target_reason = normalize_root(managed_root_path)
    if target_reason is not None:
        return _to_failure(target_reason, f"managed root not usable: {managed_root_path}")
    overlap = check_overlap(validated.root_path, normalized_target)
    if overlap is not None:
        return _to_failure(overlap, f"source/target overlap: {validated.root_path} <-> {normalized_target}")
    if os.path.exists(normalized_target) and not owns_manifest(normalized_target):
        return _to_failure("StalePartialUnowned", f"unowned managed root exists: {normalized_target}")
    remove_sibling_staging(normalized_target)

    run_id = _generate_run_id()
    parent = os.path.dirname(normalized_target)
    if not parent:
        return _to_failure("CopyFailed", f"managed root has no parent directory: {normalized_target}")
    staging_root = os.path.join(parent, STAGING_PREFIX + run_id)
    try:
        os.makedirs(staging_root, exist_ok=True)
        write_staging_marker(staging_root, normalized_target, run_id)
        copy_failure = copy_tree(validated.root_path, staging_root, cancel_check)
        if isinstance(copy_failure, str):
            try_delete(staging_root)
            return _to_failure(copy_failure, f"copy aborted ({copy_failure}); staging removed: {staging_root}")
        if cancel_check and cancel_check():
            try_delete(staging_root)
            return _to_failure("CopyCancelled", f"copy cancelled; partial staging removed: {staging_root}")
        try:
            after = _snapshot(validated)
        except SymlinkInTree as exc:
            try_delete(staging_root)
            return _to_failure("SymlinkRejected", f"symlink inside install rejected: {exc.path}")
        except OSError as exc:
            try_delete(staging_root)
            return _to_failure("CopyFailed", f"could not re-snapshot source: {exc}")
        if after != before:
            try_delete(staging_root)
            return _to_failure("SourceMutatedDuringCopy", "source changed during copy")
        try:
            copy_exe = FileIntegrity(os.path.join(staging_root, validated.executable_relative), sha256_file(os.path.join(staging_root, validated.executable_relative)))
            copy_data = FileIntegrity(os.path.join(staging_root, validated.data_win_relative), sha256_file(os.path.join(staging_root, validated.data_win_relative)))
            copy_integrity = InstallIntegrity(copy_exe, copy_data, directory_manifest_sha256(staging_root, METADATA_RELATIVE_PATHS))
        except SymlinkInTree as exc:
            try_delete(staging_root)
            return _to_failure("SymlinkRejected", f"symlink inside copy rejected: {exc.path}")
        except OSError as exc:
            try_delete(staging_root)
            return _to_failure("CopyFailed", f"could not hash the copy: {exc}")
        if copy_integrity.executable.sha256.lower() != before.executable.sha256.lower() or copy_integrity.data_win.sha256.lower() != before.data_win.sha256.lower():
            try_delete(staging_root)
            return _to_failure("CopyFailed", "copy hashes do not match source")
        write_manifest(staging_root, validated, before, after, copy_integrity, copy_integrity.directory_manifest_sha256, run_id)
        promote_with_rollback(staging_root, normalized_target, run_id, promote_fn)
        persisted_manifest = directory_manifest_sha256(normalized_target, METADATA_RELATIVE_PATHS)
        if persisted_manifest.lower() != copy_integrity.directory_manifest_sha256.lower():
            return _to_failure("CopyFailed", f"persisted managed root manifest != recorded ({persisted_manifest[:8]} vs {copy_integrity.directory_manifest_sha256[:8]})")
        return ExecutionCopy(normalized_target, os.path.join(normalized_target, validated.executable_relative), os.path.join(normalized_target, validated.data_win_relative), before, after, copy_integrity, persisted_manifest, run_id)
    except OSError as exc:
        try_delete(staging_root)
        return _to_failure("CopyFailed", f"copy failed: {exc}")


def clean_partial(managed_root_path: str) -> CleanupReceipt | Failure:
    normalized, reason = normalize_root(managed_root_path)
    if reason is not None:
        return _to_failure(reason, f"managed root not usable: {managed_root_path}")
    removed_staging = remove_sibling_staging(normalized)
    if not os.path.exists(normalized):
        return CleanupReceipt(normalized, False, removed_staging, _generate_run_id())
    if not owns_manifest(normalized):
        return _to_failure("StalePartialUnowned", f"unowned managed root; refusing deletion: {normalized}")
    try:
        shutil.rmtree(normalized)
    except OSError as exc:
        return _to_failure("CopyFailed", f"could not remove owned root: {exc}")
    return CleanupReceipt(normalized, True, removed_staging, _generate_run_id())
