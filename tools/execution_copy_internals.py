"""Internal helpers for the execution-copy boundary oracle.

Holds path containment, integrity hashing, recursive tree copy, staging ownership markers, and
rollback-safe promotion. Mirrors ``PathContainment.cs`` + ``IntegrityHashing.cs`` +
``ManagedRootIO.cs`` from the C# boundary. Imported only by ``execution_copy_contract.py``.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
from collections.abc import Callable, Iterable
from typing import Optional

STAGING_PREFIX = ".soulmod-staging-"
MANIFEST_FILENAME = ".soulmod-copy-manifest.json"
OWNERSHIP_MARKER_FILENAME = ".soulmod-ownership.json"
MANIFEST_OWNER = "SoulMod.ExecutionCopyManager"

# Platform-aware path comparison: Windows filesystems are case-insensitive, POSIX is case-sensitive.
# Using the wrong comparison would let a case-variant root evade overlap/containment on Windows.
PATH_CASE_INSENSITIVE = sys.platform == "win32"


class SymlinkInTree(Exception):
    """Raised when a symlink/reparse point is encountered inside a tree the boundary must hash or
    copy. Boundary callers translate this to a typed SymlinkRejected failure so the rejection
    happens before any copy attempt, not as a misleading CopyFailed mid-walk."""

    def __init__(self, path: str):
        super().__init__(path)
        self.path = path


def _path_equal(left: str, right: str) -> bool:
    return left.lower() == right.lower() if PATH_CASE_INSENSITIVE else left == right


def _path_startswith(candidate: str, prefix: str) -> bool:
    return candidate.lower().startswith(prefix.lower()) if PATH_CASE_INSENSITIVE else candidate.startswith(prefix)


def normalize_root(root_path: str) -> tuple[str, Optional[str]]:
    if not root_path or not root_path.strip():
        return "", "InvalidPath"
    try:
        normalized = os.path.normpath(os.path.abspath(root_path))
    except (OSError, ValueError):
        return "", "InvalidPath"
    # Preserve volume/root trailing separators; trimming "C:\" to "C:" changes its meaning and
    # would break containment/parent derivation on Windows.
    is_volume_root = _path_equal(normalized, os.path.abspath(os.sep)) or _path_equal(normalized, os.path.splitdrive(normalized)[0] + os.sep)
    if not is_volume_root:
        normalized = normalized.rstrip(os.sep) or normalized
    if os.path.islink(normalized):
        return normalized, "SymlinkRejected"
    return normalized, None


def normalize_child(normalized_root: str, relative_path: str) -> tuple[str, Optional[str]]:
    if not relative_path or not relative_path.strip() or os.path.isabs(relative_path):
        return "", "InvalidPath"
    try:
        absolute = os.path.normpath(os.path.abspath(os.path.join(normalized_root, relative_path)))
    except (OSError, ValueError):
        return "", "InvalidPath"
    if not is_within(normalized_root, absolute):
        return absolute, "InvalidPath"
    if os.path.islink(absolute):
        return absolute, "SymlinkRejected"
    return absolute, None


def check_overlap(normalized_source: str, normalized_target: str) -> Optional[str]:
    if _path_equal(normalized_source, normalized_target):
        return "SourceTargetOverlap"
    if is_within(normalized_source, normalized_target) or is_within(normalized_target, normalized_source):
        return "SourceTargetOverlap"
    return None


def is_within(root: str, candidate: str) -> bool:
    root_sep = root if root.endswith(os.sep) else root + os.sep
    return _path_equal(candidate, root) or _path_startswith(candidate, root_sep)


def sha256_file(absolute_path: str) -> str:
    digest = hashlib.sha256()
    with open(absolute_path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def directory_manifest_sha256(root_path: str, excluded_relative: Optional[Iterable[str]] = None) -> str:
    """Walks top-level only and recurses explicitly so directory reparse points are caught before
    descent. Excludes the given root-relative forward-slash metadata paths so the manifest measures
    source content and equals the persisted managed-root hash."""
    excluded = set(excluded_relative or ())
    entries: list[tuple[str, str]] = []

    def walk(current: str) -> None:
        for name in sorted(os.listdir(current)):
            full = os.path.join(current, name)
            if os.path.islink(full):
                raise SymlinkInTree(full)
            if os.path.isdir(full):
                walk(full)
            elif os.path.isfile(full):
                rel = os.path.relpath(full, root_path).replace(os.sep, "/")
                if rel in excluded:
                    continue
                entries.append((rel, sha256_file(full)))

    walk(root_path)
    entries.sort(key=lambda item: item[0])
    digest = hashlib.sha256()
    for rel, sha in entries:
        digest.update(rel.encode("utf-8"))
        digest.update(b"\0")
        digest.update(sha.encode("utf-8"))
        digest.update(b"\n")
    return digest.hexdigest()


def write_staging_marker(staging_root: str, intended_managed_root: str, run_id: str) -> None:
    document = {"ownedBy": MANIFEST_OWNER, "kind": "staging", "intendedManagedRoot": intended_managed_root, "runId": run_id}
    with open(os.path.join(staging_root, OWNERSHIP_MARKER_FILENAME), "w", encoding="utf-8") as handle:
        json.dump(document, handle)


def staging_belongs_to_target(staging_root: str, normalized_target: str) -> bool:
    marker_path = os.path.join(staging_root, OWNERSHIP_MARKER_FILENAME)
    if not os.path.isfile(marker_path):
        return False
    try:
        with open(marker_path, "r", encoding="utf-8") as handle:
            document = json.load(handle)
    except (json.JSONDecodeError, OSError):
        return False
    intended = document.get("intendedManagedRoot")
    return document.get("ownedBy") == MANIFEST_OWNER and isinstance(intended, str) and _path_equal(intended, normalized_target)


def remove_sibling_staging(normalized_target: str) -> int:
    """Removes sibling staging directories whose marker is a valid SoulMod staging marker bound to
    <normalized_target>. Siblings without a valid bound marker are preserved."""
    parent = os.path.dirname(normalized_target)
    if not parent or not os.path.isdir(parent):
        return 0
    removed = 0
    for entry in os.listdir(parent):
        if not entry.startswith(STAGING_PREFIX):
            continue
        candidate = os.path.join(parent, entry)
        if not os.path.isdir(candidate) or not staging_belongs_to_target(candidate, normalized_target):
            continue
        try:
            shutil.rmtree(candidate)
            removed += 1
        except OSError:
            pass
    return removed


def owns_manifest(managed_root: str) -> bool:
    manifest_path = os.path.join(managed_root, MANIFEST_FILENAME)
    if not os.path.isfile(manifest_path):
        return False
    try:
        with open(manifest_path, "r", encoding="utf-8") as handle:
            document = json.load(handle)
        return document.get("ownedBy") == MANIFEST_OWNER
    except (json.JSONDecodeError, OSError):
        return False


def promote_with_rollback(staging_root: str, normalized_target: str, run_id: str, promote: Callable[[str, str], None]) -> None:
    backup = f"{normalized_target}.soulmod-rollback-{run_id}"
    moved_to_backup = False
    if os.path.exists(normalized_target):
        os.rename(normalized_target, backup)
        moved_to_backup = True
    try:
        promote(staging_root, normalized_target)
    except Exception:
        if moved_to_backup and os.path.isdir(backup):
            try:
                os.rename(backup, normalized_target)
            except OSError:
                pass
        raise
    if moved_to_backup and os.path.isdir(backup):
        shutil.rmtree(backup, ignore_errors=True)


def copy_tree(source_root: str, dest_root: str, cancel_check: Optional[Callable[[], bool]]) -> Optional[str]:
    """Returns a FailureReason name on failure/cancel, None on success. Raises SymlinkInTree for any
    file or directory reparse point so the boundary reports SymlinkRejected, not CopyFailed."""
    for dirpath, dirnames, filenames in os.walk(source_root):
        if cancel_check and cancel_check():
            return "CopyCancelled"
        rel_dir = os.path.relpath(dirpath, source_root)
        dest_dir = dest_root if rel_dir == "." else os.path.join(dest_root, rel_dir)
        os.makedirs(dest_dir, exist_ok=True)
        for name in dirnames + filenames:
            full = os.path.join(dirpath, name)
            if os.path.islink(full):
                raise SymlinkInTree(full)
        for name in filenames:
            if cancel_check and cancel_check():
                return "CopyCancelled"
            shutil.copy2(os.path.join(dirpath, name), os.path.join(dest_dir, name))
    return None


def write_manifest(managed_root: str, install, before, after, copy_integrity, persisted_manifest_sha256: str, run_id: str) -> None:
    def serialize(integrity) -> dict:
        return {
            "executable": {"path": integrity.executable.path, "sha256": integrity.executable.sha256},
            "dataWin": {"path": integrity.data_win.path, "sha256": integrity.data_win.sha256},
            "directoryManifestSha256": integrity.directory_manifest_sha256,
        }
    document = {
        "ownedBy": MANIFEST_OWNER,
        "runId": run_id,
        "formatVersion": 2,
        "source": {
            "root": install.root_path,
            "executable": install.executable_relative,
            "dataWin": install.data_win_relative,
        },
        "sourceIntegrityBefore": serialize(before),
        "sourceIntegrityAfter": serialize(after),
        "copyIntegrity": serialize(copy_integrity),
        "persistedDirectoryManifestSha256": persisted_manifest_sha256,
    }
    with open(os.path.join(managed_root, MANIFEST_FILENAME), "w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=2)


def try_delete(path: str) -> None:
    try:
        if os.path.isdir(path):
            shutil.rmtree(path)
    except OSError:
        pass
