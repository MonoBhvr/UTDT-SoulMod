"""Filesystem walking and source-baseline computation for the hygiene tool.

Walks the repository, emits :class:`PathRule` decisions (collapsing fully
ignored subtrees into summary entries so a 1 GB game copy does not flood the
report), and computes the source-only baseline (the sorted paths to track).
"""

from __future__ import annotations

import os
from typing import Iterator

from repo_hygiene_policy import (
    Category,
    PathRule,
    RETENTION_TRACK,
    classify,
)


def _fully_ignored_dir_categories() -> frozenset[Category]:
    """Directory-level categories that mark a whole subtree as ignored.

    Note: ``UNCLASSIFIED`` is intentionally absent. A bare container directory
    name like ``runtime/`` or ``converter/`` classifies as ``UNCLASSIFIED`` --
    that means "mixed contents, descend", not "ignore the subtree". Pruning on
    broad retention would silently drop product source contained in such dirs.
    """
    return frozenset({
        Category.VCS_STATE,
        Category.OMO_STATE,
        Category.GENERATED_PUBLISH,
        Category.GAME_MATERIAL,
        Category.SCRATCH,
        Category.GENERATED_BUILD,
        Category.THIRD_PARTY_SCRIPT,
        Category.IDE,
        Category.OS_JUNK,
    })


def _is_oracle_container(rel_path: str) -> bool:
    return rel_path in {
        "scratch",
        "scratch/AurieFramework",
        "scratch/AurieFramework/SoulModPlugin",
        "scratch/AurieFramework/SoulModPlugin/source",
        "scratch/AurieFramework/SoulModPlugin/tests",
    }


def _top_collapse_categories() -> frozenset[Category]:
    """Top-level dir categories collapsed to a single summary line.

    ``scratch/`` is intentionally excluded: it contains meaningfully distinct
    ignored subtrees (the copyrighted game copy under ``test_game/``, vendored
    framework clones, third-party libs) that the boundary report must call out
    on their own lines. Descending into ``scratch/`` lets ``_walk_dir`` collapse
    each child subtree separately while enumerating the loose scratch files.
    """
    return frozenset({
        Category.VCS_STATE,
        Category.OMO_STATE,
        Category.GENERATED_PUBLISH,
    })


def walk_classified(repo_root: str) -> Iterator[PathRule]:
    """Yield :class:`PathRule` for every file under ``repo_root``.

    Uniform fully-ignored subtrees (``.git/``, ``.omo/``, ``artifacts/``) are
    emitted as a single summary :class:`PathRule` whose ``rel_path`` is the
    subtree root (with a trailing ``/``) and whose ``size`` is the total byte
    count inside. ``scratch/`` is descended into one level so each distinct
    ignored child (game copy, vendored clones, lib) gets its own summary line
    and the copyrighted game material is named explicitly in the report.
    """
    repo_root = os.path.abspath(repo_root)
    top_collapse = _top_collapse_categories()

    for entry in sorted(os.listdir(repo_root)):
        full = os.path.join(repo_root, entry)
        if not os.path.isdir(full):
            continue
        cat = classify(entry)
        if cat in top_collapse:
            yield PathRule(rel_path=entry + "/", category=cat, size=_dir_size(full))
            continue
        yield from _walk_dir(repo_root, full)

    for entry in sorted(os.listdir(repo_root)):
        full = os.path.join(repo_root, entry)
        if os.path.isdir(full):
            continue
        try:
            size = os.path.getsize(full)
        except OSError:
            size = 0
        yield PathRule(rel_path=entry, category=classify(entry), size=size)


def _walk_dir(repo_root: str, dir_full: str) -> Iterator[PathRule]:
    """Walk a mixed-content directory; collapse fully-ignored subtrees into one summary rule each."""
    ignored_dir_cats = _fully_ignored_dir_categories()
    for root, dirs, files in os.walk(dir_full):
        dirs.sort()
        files.sort()
        rel_root = os.path.relpath(root, repo_root).replace(os.sep, "/")

        kept_dirs: list[str] = []
        for d in dirs:
            d_rel = (rel_root + "/" + d) if rel_root != "." else d
            d_cat = classify(d_rel)
            if d_cat in ignored_dir_cats and not _is_oracle_container(d_rel):
                yield PathRule(
                    rel_path=d_rel + "/",
                    category=d_cat,
                    size=_dir_size(os.path.join(root, d)),
                )
            else:
                kept_dirs.append(d)
        dirs[:] = kept_dirs

        for fname in files:
            f_rel = (rel_root + "/" + fname) if rel_root != "." else fname
            try:
                size = os.path.getsize(os.path.join(root, fname))
            except OSError:
                size = 0
            yield PathRule(rel_path=f_rel, category=classify(f_rel), size=size)


def _dir_size(path: str) -> int:
    total = 0
    for root, _dirs, files in os.walk(path):
        for fname in files:
            try:
                total += os.path.getsize(os.path.join(root, fname))
            except OSError:
                pass
    return total


def source_baseline(repo_root: str) -> list[str]:
    """Return the sorted list of repository-relative paths to be tracked.

    A path is in the baseline iff its :func:`classify` retention is ``track``
    AND it is a real file. Collapsed summary entries for ignored subtrees are
    excluded.
    """
    repo_root = os.path.abspath(repo_root)
    ignored_dir_cats = _fully_ignored_dir_categories()
    tracked: list[str] = []
    for root, dirs, files in os.walk(repo_root):
        rel_root = os.path.relpath(root, repo_root).replace(os.sep, "/")
        kept: list[str] = []
        for d in dirs:
            d_rel = (rel_root + "/" + d) if rel_root != "." else d
            if classify(d_rel) in ignored_dir_cats and not _is_oracle_container(d_rel):
                continue
            kept.append(d)
        dirs[:] = kept
        dirs.sort()
        for fname in sorted(files):
            f_rel = (rel_root + "/" + fname) if rel_root != "." else fname
            if classify(f_rel).retention == RETENTION_TRACK:
                tracked.append(f_rel)
    return sorted(tracked)
