#!/usr/bin/env python3
"""Repository hygiene CLI: classification report, baseline manifest, negative control.

Classifies every file under the repository root into deterministic retention
categories and emits a sorted, repository-relative path-classification report.
Also computes the source-only baseline manifest (paths that should be tracked)
and a ``check`` mode that proves a seeded ignored generated artifact is rejected
from that baseline.

The category policy encodes the commit/ignore boundaries established in
``.omo/plans/soulmod-platform-roadmap.md`` (Wave 1, checkbox 1):

  track  : product source, schemas, mod declarative assets, test fixtures,
           docs, licenses, repo config, the hygiene tooling itself.
  ignore : generated build trees, published worker artifacts, generated
           packages, user-owned/licensed game material, scratch/POC area and
           vendored third-party clones, third-party binary tools, runtime logs,
           .omo orchestrator state, the local debug journal, editor/OS junk.

No third-party dependencies. Python 3.9+ standard library only.

Usage::

    python3 tools/repo_hygiene.py report   [--root PATH] [--out DIR]
    python3 tools/repo_hygiene.py manifest [--root PATH] [--out PATH]
    python3 tools/repo_hygiene.py check    [--root PATH] [--out DIR]

Exit codes: 0 on success, 2 on argument error, 1 otherwise.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import os
from typing import Iterable

# The pure policy and the filesystem walker live in sibling modules; this
# entrypoint re-exports the public API so callers (and the test suite) can
# import everything from ``repo_hygiene``.
from repo_hygiene_policy import (  # noqa: F401 -- re-exported public API
    RETENTION_IGNORE,
    RETENTION_TRACK,
    Category,
    PathRule,
    classify,
)
from repo_hygiene_walk import source_baseline, walk_classified  # noqa: F401

__all__ = [
    "Category",
    "PathRule",
    "RETENTION_IGNORE",
    "RETENTION_TRACK",
    "classify",
    "source_baseline",
    "walk_classified",
    "build_report",
    "run_negative_control",
    "main",
]


def _git_head(repo_root: str) -> str:
    head_path = os.path.join(repo_root, ".git", "HEAD")
    try:
        with open(head_path, "r", encoding="utf-8") as fh:
            return fh.read().strip()
    except OSError:
        return "unknown"


def _now_iso_utc() -> str:
    return _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _human_size(n: int) -> str:
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if n < 1024:
            return f"{n} {unit}"
        n = round(n / 1024, 1)
    return f"{n} PiB"


def build_report(repo_root: str) -> str:
    """Return the full deterministic classification report as a string.

    The report is byte-deterministic: it contains only path-derived data and
    stable repo metadata (root path, git HEAD ref, policy pointer). No wall-clock
    timestamp is embedded; the generation time is emitted to stdout only by the
    CLI wrapper, so repeated runs produce identical canonical bytes.
    """
    repo_root = os.path.abspath(repo_root)
    rules = list(walk_classified(repo_root))
    rules.sort(key=lambda r: (r.category.name, r.rel_path))

    tracked = [r for r in rules if r.category.retention == RETENTION_TRACK]
    ignored = [r for r in rules if r.category.retention == RETENTION_IGNORE]

    summary: dict[Category, list[int]] = {}
    for r in rules:
        agg = summary.setdefault(r.category, [0, 0])
        agg[0] += 1
        agg[1] += r.size

    lines: list[str] = []
    lines.append("# SoulMod Path Classification Report")
    lines.append(f"repo_root: {repo_root}")
    lines.append(f"git_head: {_git_head(repo_root)}")
    lines.append(
        "policy: source-only baseline; see .omo/plans/soulmod-platform-roadmap.md (Wave 1, checkbox 1)"
    )
    lines.append("")
    lines.append(f"total_entries: {len(rules)}")
    lines.append(f"tracked_entries: {len(tracked)}")
    lines.append(f"ignored_entries: {len(ignored)}")
    lines.append("")

    lines.append("## Summary by category")
    lines.append("")
    lines.append(f"{'category':<22} {'retention':<8} {'files':>8} {'bytes':>14}")
    for cat in Category:
        if cat not in summary:
            continue
        count, nbytes = summary[cat]
        lines.append(
            f"{cat.name_for_report:<22} {cat.retention:<8} {count:>8} {_human_size(nbytes):>14}"
        )
    lines.append("")

    lines.append("## Tracked source-only baseline (sorted)")
    lines.append("")
    lines.append(f"{'path':<72} {'category':<22} {'bytes':>14}")
    for r in sorted(tracked, key=lambda x: x.rel_path):
        lines.append(f"{r.rel_path:<72} {r.category.name_for_report:<22} {_human_size(r.size):>14}")
    lines.append("")

    lines.append("## Ignored subtrees and files (sorted)")
    lines.append("")
    lines.append(f"{'path':<72} {'category':<22} {'bytes':>14}")
    for r in sorted(ignored, key=lambda x: (x.category.name, x.rel_path)):
        lines.append(f"{r.rel_path:<72} {r.category.name_for_report:<22} {_human_size(r.size):>14}")
    lines.append("")

    lines.append("## Known source-coupling to ignored paths (for later waves)")
    lines.append("- converter/SoulMod.UmtWorker/SoulMod.UmtWorker.csproj references ../../scratch/lib/*.dll as HintPath -> Wave 2 build reproducibility.")
    lines.append("- converter/SoulMod.Converter.Tests/ResourceDeltaTests.cs reads scratch/test_game/chapter1_windows/data.win -> Wave 3 fixture gating.")
    lines.append("- runtime/SoulMod.Runtime/src/SoulModEngine.cpp hardcodes d:\\UTDT-SoulMod\\scratch dump path -> Wave 5 runtime promotion.")
    lines.append("- mods/test/dtkr_diff/data_dtkr_ch1.win (17 MiB) and mods/test/font.ttf (13 MiB) are large binary fixtures; consider Git LFS or external provisioning.")
    lines.append("")

    return "\n".join(lines)


def run_negative_control(repo_root: str) -> str:
    """Seed ignored generated artifacts, prove they are rejected, clean up.

    Removes every artifact it creates, even on failure paths. Seeds are chosen
    to hit specific ignore rules (not the catch-all UNCLASSIFIED bucket): a
    ``.soulmod`` file exercises the package rule, and a file under a real
    ``bin/`` segment in the converter tree exercises the generated-build rule.
    Asserts the exact category so a successful exit code cannot come from the
    wrong classification.
    """
    repo_root = os.path.abspath(repo_root)
    seeded_package = os.path.join(repo_root, "NEGATIVE_CONTROL_PACKAGE.soulmod")
    seeded_bin_dir = os.path.join(repo_root, "converter", "SoulModNegativeControl", "bin")
    seeded_bin = os.path.join(seeded_bin_dir, "seeded.dll")
    created: list[str] = []

    lines: list[str] = []
    lines.append("# SoulMod Hygiene Negative-Control Result")
    lines.append(f"repo_root: {repo_root}")
    lines.append("")

    try:
        os.makedirs(seeded_bin_dir, exist_ok=True)
        created.append(seeded_bin_dir)
        with open(seeded_package, "w", encoding="utf-8") as fh:
            fh.write("negative-control: generated package seed\n")
        created.append(seeded_package)
        with open(seeded_bin, "wb") as fh:
            fh.write(b"negative-control: generated build seed\n")
        created.append(seeded_bin)
        lines.append("seeded: NEGATIVE_CONTROL_PACKAGE.soulmod (must hit rule: *.soulmod -> PACKAGE)")
        lines.append("seeded: converter/SoulModNegativeControl/bin/seeded.dll (must hit rule: **/bin/ -> GENERATED_BUILD)")
        lines.append("")

        pkg_cat = classify("NEGATIVE_CONTROL_PACKAGE.soulmod")
        bin_cat = classify("converter/SoulModNegativeControl/bin/seeded.dll")
        baseline = source_baseline(repo_root)
        baseline_set = set(baseline)

        pkg_rule_ok = pkg_cat is Category.PACKAGE
        bin_rule_ok = bin_cat is Category.GENERATED_BUILD
        pkg_rejected = "NEGATIVE_CONTROL_PACKAGE.soulmod" not in baseline_set
        bin_rejected = "converter/SoulModNegativeControl/bin/seeded.dll" not in baseline_set
        genuine_ok = (
            "runtime/SoulMod.Runtime/src/SoulModEngine.cpp" in baseline_set
            and "converter/SoulMod.Converter.sln" in baseline_set
            and ".gitignore" in baseline_set
        )

        lines.append(f"classify(NEGATIVE_CONTROL_PACKAGE.soulmod) = {pkg_cat.name_for_report} (rule hit: {pkg_rule_ok})")
        lines.append(f"classify(converter/SoulModNegativeControl/bin/seeded.dll) = {bin_cat.name_for_report} (rule hit: {bin_rule_ok})")
        lines.append(f"package rejected from baseline: {pkg_rejected}")
        lines.append(f"bin artifact rejected from baseline: {bin_rejected}")
        lines.append(f"genuine product source still in baseline: {genuine_ok}")
        lines.append(f"baseline entries: {len(baseline)}")
        lines.append("")

        overall = pkg_rule_ok and bin_rule_ok and pkg_rejected and bin_rejected and genuine_ok
        lines.append(f"RESULT: {'PASS' if overall else 'FAIL'}")
    finally:
        # Remove files first, then walk up the created chain removing now-empty
        # dirs. Order is load-bearing: rmdir needs the files gone first.
        for path in created:
            try:
                if os.path.isfile(path):
                    os.remove(path)
            except OSError:
                pass
        _rmdir_chain(seeded_bin_dir, stop=repo_root)
        leftover = [p for p in created if os.path.exists(p)]
        lines.append("")
        lines.append("cleanup_receipt:")
        for p in created:
            present = os.path.exists(p)
            lines.append(f"  - {os.path.relpath(p, repo_root)}: {'STILL_PRESENT' if present else 'removed'}")
        chain_leftover = _empty_dir_chain_leftover(seeded_bin_dir, repo_root)
        if leftover or chain_leftover:
            lines.append(
                f"RESULT_OF_CLEANUP: FAIL ({len(leftover)} artifact(s), "
                f"{len(chain_leftover)} empty temp dir(s) remain)"
            )
        else:
            lines.append("RESULT_OF_CLEANUP: PASS (all seeded artifacts and temp dirs removed)")

    return "\n".join(lines)


def _rmdir_chain(start_dir: str, stop: str) -> None:
    """Remove ``start_dir`` and each now-empty ancestor up to (excluding) ``stop``."""
    cur = start_dir
    while cur and os.path.abspath(cur) != os.path.abspath(stop) and os.path.isdir(cur):
        try:
            if os.listdir(cur):
                break
            os.rmdir(cur)
        except OSError:
            break
        cur = os.path.dirname(cur)


def _empty_dir_chain_leftover(start_dir: str, repo_root: str) -> list[str]:
    """Return any empty temp dirs left in the chain from ``start_dir`` to root."""
    leftover: list[str] = []
    cur = start_dir
    while cur and os.path.abspath(cur) != os.path.abspath(repo_root):
        if os.path.isdir(cur) and not os.listdir(cur):
            leftover.append(cur)
            cur = os.path.dirname(cur)
            continue
        break
    return leftover


def build_manifest(repo_root: str) -> str:
    """Return the deterministic source-only baseline manifest as a string.

    Byte-deterministic: a stable header (purpose + repo root) followed by the
    sorted, repository-relative tracked paths. No wall-clock timestamp.
    """
    repo_root = os.path.abspath(repo_root)
    baseline = source_baseline(repo_root)
    lines = [
        "# SoulMod source-only baseline manifest (sorted, repository-relative)",
        f"# repo_root: {repo_root}",
    ]
    lines.extend(baseline)
    return "\n".join(lines) + "\n"


def _cmd_report(args: argparse.Namespace) -> int:
    repo_root = os.path.abspath(args.root)
    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)
    text = build_report(repo_root)
    out_path = os.path.join(out_dir, "path-classification.txt")
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write(text)
    # Timestamp is emitted to stdout only; the artifact stays byte-deterministic.
    print(f"[generated_utc={_now_iso_utc()}] wrote {out_path}")
    return 0


def _cmd_manifest(args: argparse.Namespace) -> int:
    repo_root = os.path.abspath(args.root)
    baseline = source_baseline(repo_root)
    out_path = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write(build_manifest(repo_root))
    print(f"[generated_utc={_now_iso_utc()}] wrote {out_path} ({len(baseline)} entries)")
    return 0


def _cmd_check(args: argparse.Namespace) -> int:
    repo_root = os.path.abspath(args.root)
    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)
    text = run_negative_control(repo_root)
    out_path = os.path.join(out_dir, "negative-control-result.txt")
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(text)
    print(f"[generated_utc={_now_iso_utc()}] wrote {out_path}")
    failed = "RESULT: FAIL" in text or "RESULT_OF_CLEANUP: FAIL" in text
    return 1 if failed else 0


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="repo_hygiene",
        description="SoulMod repository hygiene classifier and baseline manifest generator.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_report = sub.add_parser("report", help="write the full path-classification report")
    p_report.add_argument("--root", default=".", help="repository root (default: cwd)")
    p_report.add_argument("--out", default=".omo/evidence/wave-1", help="output directory")
    p_report.set_defaults(func=_cmd_report)

    p_manifest = sub.add_parser("manifest", help="write the source-only baseline manifest")
    p_manifest.add_argument("--root", default=".", help="repository root (default: cwd)")
    p_manifest.add_argument("--out", default=".omo/evidence/wave-1/source-baseline-manifest.txt", help="output file")
    p_manifest.set_defaults(func=_cmd_manifest)

    p_check = sub.add_parser("check", help="run the negative-control and write its result")
    p_check.add_argument("--root", default=".", help="repository root (default: cwd)")
    p_check.add_argument("--out", default=".omo/evidence/wave-1", help="output directory")
    p_check.set_defaults(func=_cmd_check)

    return parser


def main(argv: Iterable[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
