#!/usr/bin/env python3
"""Focused characterization tests for tools/repo_hygiene.py.

No third-party dependencies. Run directly:

    python3 tools/repo_hygiene_test.py

Each test follows Given / When / Then. The suite proves:
  1. path classification matches the boundary policy in checkbox 1, and
  2. the negative-control scenario (a seeded ignored generated artifact) is
     rejected from the source-only baseline and cleaned up.

Exit code is nonzero on the first failing assertion.
"""

from __future__ import annotations

import os
import sys

# Make the sibling module importable regardless of cwd.
_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(_HERE)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

import repo_hygiene  # noqa: E402  -- tools/ is the module root for this script
from repo_hygiene import (  # noqa: E402
    RETENTION_IGNORE,
    RETENTION_TRACK,
    build_manifest,
    build_report,
    classify,
    source_baseline,
)


_FAILURES: list[str] = []


def check(name: str, condition: bool, detail: str = "") -> None:
    if condition:
        print(f"  PASS  {name}")
    else:
        msg = f"  FAIL  {name}{(' :: ' + detail) if detail else ''}"
        print(msg)
        _FAILURES.append(msg)


def _rmdir_chain(start_dir: str, stop_dir: str) -> list[str]:
    """Remove ``start_dir`` and each now-empty ancestor up to (excluding) ``stop_dir``.

    Test-only equivalent of the production ``repo_hygiene._rmdir_chain``. Only
    empty directories are removed, and the walk stops at ``stop_dir`` (a known
    pre-existing product directory such as ``converter/``) so user/product data
    is never touched. Returns the removed directory paths for the cleanup log.
    """
    removed: list[str] = []
    stop_abs = os.path.abspath(stop_dir)
    cur = start_dir
    while cur and os.path.abspath(cur) != stop_abs and os.path.isdir(cur):
        try:
            if os.listdir(cur):
                break
            os.rmdir(cur)
        except OSError:
            break
        removed.append(cur)
        cur = os.path.dirname(cur)
    return removed


def expect_category(rel_path: str, expected_category: str) -> None:
    """Given a representative path, When classified, Then category matches."""
    got = classify(rel_path)
    check(
        f"classify({rel_path!r}) == {expected_category!r}",
        got.name == expected_category,
        f"got {got.name} (retention={got.retention})",
    )


def test_classification_policy() -> None:
    print("[test_classification_policy]")
    # --- product source (track) ---
    expect_category("runtime/SoulMod.Runtime/src/SoulModEngine.cpp", "SOURCE")
    expect_category("runtime/SoulMod.Runtime/include/SoulMod/HookRegistry.hpp", "SOURCE")
    expect_category("runtime/CMakeLists.txt", "SOURCE")
    expect_category("runtime/SoulMod.Runtime/CMakeLists.txt", "SOURCE")
    expect_category("converter/SoulMod.Converter.Cli/Program.cs", "SOURCE")
    expect_category("converter/SoulMod.Converter.sln", "SOURCE")
    expect_category("converter/SoulMod.Package/SoulMod.Package.csproj", "SOURCE")
    expect_category("shared/schema/manifest.schema.json", "SCHEMA")
    expect_category("shared/schema/operations.schema.json", "SCHEMA")

    # --- fixtures (track) ---
    expect_category("tests/fixtures/.gitkeep", "FIXTURE")
    expect_category("tests/fixtures/package-minimal/manifest.json", "FIXTURE")
    expect_category("tests/golden/.gitkeep", "FIXTURE")
    expect_category("tests/integration/.gitkeep", "FIXTURE")

    # --- mod declarative source (track) ---
    expect_category("mods/dtkr.ch1/manifest.json", "MOD_SOURCE")
    expect_category("mods/dtkr.ch1/operations.json", "MOD_SOURCE")
    expect_category("mods/dtkr.ch1/patch.xdelta", "MOD_SOURCE")
    expect_category("mods/runtime-proof/assets/fonts/Galmuri11.ttf", "MOD_SOURCE")
    expect_category("mods/runtime-proof/assets/fonts/LICENSE.txt", "MOD_SOURCE")
    expect_category("mods/runtime-proof/assets/lang/lang_ja.json", "MOD_SOURCE")
    expect_category("mods/test/dtkr_diff/atlases/atlas_000.png", "MOD_SOURCE")
    expect_category("mods/test/fonts/fnt_main/manifest.json", "MOD_SOURCE")
    expect_category("mods/test/dtkr_diff/data_dtkr_ch1.win", "MOD_SOURCE")

    # --- docs / license / config (track) ---
    expect_category("README.md", "DOC")
    expect_category("LICENSE", "LICENSE")
    expect_category("NOTICE", "LICENSE")
    expect_category("THIRD_PARTY_NOTICES.md", "LICENSE")
    expect_category("docs/architecture.md", "DOC")
    expect_category("docs/mod-format.md", "DOC")
    expect_category("analysis/language-flow.md", "DOC")
    expect_category(".gitattributes", "CONFIG")
    expect_category(".gitignore", "CONFIG")

    # --- tooling that ships with the repo (track) ---
    expect_category("tools/repo_hygiene.py", "TOOL_SCRIPT")
    expect_category("tools/repo_hygiene_test.py", "TOOL_SCRIPT")
    expect_category("tools/repo_hygiene_policy.py", "TOOL_SCRIPT")
    expect_category("tools/repo_hygiene_walk.py", "TOOL_SCRIPT")

    # --- generated build outputs (ignore) ---
    expect_category("runtime/build/CMakeCache.txt", "GENERATED_BUILD")
    expect_category("runtime/build/x64/Debug/SoulMod.Runtime.dll", "GENERATED_BUILD")
    expect_category("converter/SoulMod.Package/bin/Release/net8.0/SoulMod.Package.dll", "GENERATED_BUILD")
    expect_category("converter/SoulMod.Package/obj/Debug/net8.0/SoulMod.Package.dll", "GENERATED_BUILD")
    expect_category("tools/__pycache__/repo_hygiene.cpython-312.pyc", "GENERATED_BUILD")
    expect_category("any/deep/path/__pycache__/mod.cpython-312.pyc", "GENERATED_BUILD")
    expect_category("tools/repo_hygiene.cpython-312.pyc", "GENERATED_BUILD")

    # --- generated publish artifacts (ignore) ---
    expect_category("artifacts/umtdiff/win-x64/SoulMod.UmtWorker.exe", "GENERATED_PUBLISH")
    expect_category("artifacts/umtdiff/win-x64/SoulMod.UmtWorker.dll", "GENERATED_PUBLISH")

    # --- game material (ignore, never commit) ---
    expect_category("scratch/test_game/DELTARUNE.exe", "GAME_MATERIAL")
    expect_category("scratch/test_game/data.win", "GAME_MATERIAL")
    expect_category("scratch/test_game/chapter1_windows/data.win", "GAME_MATERIAL")

    # --- scratch / POC / third-party vendored clones (ignore) ---
    expect_category("scratch/chapter1_patched_data.win", "SCRATCH")
    expect_category("scratch/CompileGlyphs.exe", "SCRATCH")
    expect_category("scratch/CompileGlyphs.cs", "SCRATCH")
    expect_category("scratch/AurieFramework/SoulModPlugin/source/ModuleMain.cpp", "ORACLE")
    expect_category("scratch/AurieFramework/SoulModPlugin/tests/RuntimeOperationsTests.cpp", "ORACLE")
    expect_category("scratch/AurieFramework/SoulModPlugin/tests/x64/Release/RuntimeOperationsTests.exe", "GENERATED_BUILD")
    expect_category("scratch/AurieFramework/Aurie/Aurie/AurieCore.vcxproj", "SCRATCH")
    expect_category("scratch/UMT_Scripts/CommunityScripts/FontEditor.csx", "SCRATCH")
    expect_category("scratch/lib/UndertaleModLib.dll", "SCRATCH")

    # --- generated packages (ignore) ---
    expect_category("scratch/test_game/mods/dtkr.ch1.soulmod", "PACKAGE")
    expect_category("build/whatever.soulmod", "PACKAGE")

    # --- third-party binary tools (ignore) ---
    expect_category("tools/xdelta3-3.0.11-x86_64.exe", "THIRD_PARTY_TOOL")
    expect_category("tools/xdelta3.zip", "THIRD_PARTY_TOOL")

    # --- logs (ignore) ---
    expect_category("SoulMod.log", "LOG")
    expect_category("scratch/test_game/YYToolkit.log", "LOG")

    # --- omo state (ignore) ---
    expect_category(".omo/boulder.json", "OMO_STATE")
    expect_category(".omo/evidence/wave-1/path-classification.txt", "OMO_STATE")
    expect_category(".omo/plans/soulmod-platform-roadmap.md", "OMO_STATE")
    expect_category(".omo/run-continuation/ses_abc.json", "OMO_STATE")

    # --- debug journal (ignore) ---
    expect_category(".debug-journal.md", "DEBUG_JOURNAL")

    # --- unattributed third-party / scratch data (ignore) ---
    expect_category("CommunityScripts/ReloadDeltaruneJSON.csx", "THIRD_PARTY_SCRIPT")
    expect_category("tools/ko_ch2.txt", "SCRATCH_DATA")

    # --- accidental empty stub (ignore) ---
    expect_category("dotnet", "EMPTY_STUB")


def test_retention_invariants() -> None:
    print("[test_retention_invariants]")
    track_categories = {
        "SOURCE", "SCHEMA", "FIXTURE", "MOD_SOURCE", "ORACLE",
        "DOC", "LICENSE", "CONFIG", "TOOL_SCRIPT",
    }
    ignore_categories = {
        "GENERATED_BUILD", "GENERATED_PUBLISH", "GAME_MATERIAL", "SCRATCH",
        "PACKAGE", "THIRD_PARTY_TOOL", "LOG", "OMO_STATE", "DEBUG_JOURNAL",
        "THIRD_PARTY_SCRIPT", "SCRATCH_DATA", "EMPTY_STUB", "OS_JUNK", "IDE",
        "UNCLASSIFIED",
    }
    for name in track_categories:
        c = repo_hygiene.Category[name]
        check(f"retention({name})==track", c.retention == RETENTION_TRACK, f"got {c.retention}")
    for name in ignore_categories:
        c = repo_hygiene.Category[name]
        check(f"retention({name})==ignore", c.retention == RETENTION_IGNORE, f"got {c.retention}")


def test_source_baseline_rejects_seeded_ignored_artifacts() -> None:
    """Negative control: seeded ignored generated artifacts must NOT appear in
    the source-only baseline, and must be cleaned up after the run."""
    print("[test_source_baseline_rejects_seeded_ignored_artifacts]")
    repo_root = _REPO_ROOT

    # Given: two seeded artifacts chosen to hit specific ignore rules (not the
    # UNCLASSIFIED catch-all). The .soulmod hits the package rule; the file
    # under a real bin/ segment in the converter tree hits the generated-build
    # rule.
    seeded_package = os.path.join(repo_root, "NEGATIVE_CONTROL_PACKAGE.soulmod")
    seeded_bin = os.path.join(repo_root, "converter", "SoulModNegativeControl", "bin", "seeded.dll")
    os.makedirs(os.path.dirname(seeded_bin), exist_ok=True)
    with open(seeded_package, "w", encoding="utf-8") as fh:
        fh.write("negative-control: generated package seed\n")
    with open(seeded_bin, "wb") as fh:
        fh.write(b"negative-control: generated build seed\n")

    try:
        # When: the source baseline is computed.
        baseline = source_baseline(repo_root)
        baseline_set = set(baseline)

        # Then: each seeded path hits its intended rule (exact category) and is
        # excluded from the baseline. Asserting the exact category -- not just
        # retention=ignore -- prevents misleading success via UNCLASSIFIED.
        pkg_cat = classify("NEGATIVE_CONTROL_PACKAGE.soulmod")
        bin_cat = classify("converter/SoulModNegativeControl/bin/seeded.dll")
        check("seeded .soulmod hits PACKAGE rule", pkg_cat is repo_hygiene.Category.PACKAGE, pkg_cat.name)
        check("seeded bin/ artifact hits GENERATED_BUILD rule", bin_cat is repo_hygiene.Category.GENERATED_BUILD, bin_cat.name)
        check(
            "seeded .soulmod NOT in baseline",
            "NEGATIVE_CONTROL_PACKAGE.soulmod" not in baseline_set,
        )
        check(
            "seeded bin/ artifact NOT in baseline",
            "converter/SoulModNegativeControl/bin/seeded.dll" not in baseline_set,
        )

        # And: genuine product source IS in the baseline (no false negatives).
        check(
            "runtime/SoulMod.Runtime/src/SoulModEngine.cpp in baseline",
            "runtime/SoulMod.Runtime/src/SoulModEngine.cpp" in baseline_set,
        )
        check(
            "converter/SoulMod.Converter.sln in baseline",
            "converter/SoulMod.Converter.sln" in baseline_set,
        )
        check(
            ".gitignore in baseline",
            ".gitignore" in baseline_set,
        )
        check(
            "scratch oracle source in baseline",
            "scratch/AurieFramework/SoulModPlugin/source/ModuleMain.cpp" in baseline_set,
        )

        # And: baseline is deterministic (sorted, repository-relative, no absolute paths).
        check("baseline is sorted", baseline == sorted(baseline))
        check("baseline has no absolute paths", all(not os.path.isabs(p) for p in baseline))
    finally:
        # Cleanup: remove only the seeded artifacts (never user data).
        removed: list[str] = []
        for seeded in (seeded_package, seeded_bin):
            if os.path.isfile(seeded):
                os.remove(seeded)
                removed.append(seeded)
        removed.extend(_rmdir_chain(os.path.dirname(seeded_bin), os.path.join(repo_root, "converter")))
        check("seeded artifacts cleaned up", all(not os.path.exists(p) for p in removed), "; ".join(removed))
        check(
            "no test-created parent dir remains",
            not os.path.exists(os.path.join(repo_root, "converter", "SoulModNegativeControl")),
            "converter/SoulModNegativeControl must be removed",
        )


def test_canonical_report_and_manifest_are_byte_deterministic() -> None:
    """Regression: canonical report and manifest must be byte-identical across
    runs regardless of wall-clock time.

    Given: a hygiene tool whose canonical output previously embedded
    ``generated_utc`` via ``_now_iso_utc()``. When: the clock function is
    remapped to two distinct values across two generations of the report and
    the manifest. Then: the complete output bytes are identical (the old code
    would fail because the embedded timestamp differed) and neither output
    contains a ``generated_utc`` line.

    The assertion compares FULL output bytes, not selected lines, so a
    timestamp re-added anywhere in the canonical text would be caught.
    """
    print("[test_canonical_report_and_manifest_are_byte_deterministic]")
    repo_root = _REPO_ROOT
    original_now = repo_hygiene._now_iso_utc
    try:
        repo_hygiene._now_iso_utc = lambda: "2000-01-01T00:00:00Z"
        report_a = build_report(repo_root)
        manifest_a = build_manifest(repo_root)
        repo_hygiene._now_iso_utc = lambda: "2099-12-31T23:59:59Z"
        report_b = build_report(repo_root)
        manifest_b = build_manifest(repo_root)
    finally:
        repo_hygiene._now_iso_utc = original_now

    check("report byte-identical across distinct clocks", report_a == report_b)
    check("manifest byte-identical across distinct clocks", manifest_a == manifest_b)
    check(
        "report has no generated_utc line",
        "generated_utc" not in report_a,
        "canonical report must not embed wall-clock time",
    )
    check(
        "manifest has no generated_utc line",
        "generated_utc" not in manifest_a,
        "canonical manifest must not embed wall-clock time",
    )


def main() -> int:
    test_classification_policy()
    test_retention_invariants()
    test_source_baseline_rejects_seeded_ignored_artifacts()
    test_canonical_report_and_manifest_are_byte_deterministic()
    print()
    if _FAILURES:
        print(f"RESULT: FAIL ({len(_FAILURES)} assertion(s) failed)")
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
