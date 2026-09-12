#!/usr/bin/env python3
"""Deterministic source-map validator for the promoted SoulMod.Runtime target.

Validates that the production CMake target compiles the explicit promoted
module graph and nothing else, accounting for CMake source mutations that a
naive first-match parser would miss:

  * repeated ``set(SOULMOD_RUNTIME_PROMOTED_SOURCES ...)`` (last wins)
  * ``list(APPEND ...)`` / ``list(REMOVE_ITEM ...)`` on that variable
  * ``target_sources(SoulMod.Runtime ...)`` after ``add_library``

It also guards the stale generated ``runtime/build/`` tree: if a generated
project file compiles the excluded POC sources (SoulModEngine/HookRegistry/
PocHook), the guard reports the build tree as stale so the stale DLL is never
trusted as the promoted runtime.

This is the deterministic Linux proof. Native CMake/MSVC compilation is the
Windows lane's responsibility; this validator does not claim native success.
"""
from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_CMAKE = ROOT / "runtime" / "SoulMod.Runtime" / "CMakeLists.txt"
RUNTIME_BUILD = ROOT / "runtime" / "build"

EXPECTED_PROMOTED_SOURCES = frozenset({
    "src/dllmain.cpp",
    "src/ModLoader.cpp",
    "src/PackageValidation.cpp",
    "src/JsonValidator.cpp",
    "src/GlobalJsonEpoch.cpp",
    "src/OperationsResolver.cpp",
    "src/DependencyGraph.cpp",
    "src/YYToolkitAdapter.cpp",
    "src/GameHookEngine.cpp",
    "src/HookManager.cpp",
    "src/Logger.cpp",
    "src/StringManager.cpp",
    "src/FontManager.cpp",
})

EXPECTED_EXCLUDED_SOURCES = frozenset({
    "src/PocHook.cpp",
    "src/SoulModEngine.cpp",
    "src/HookRegistry.cpp",
})

ABSOLUTE_PATH_PATTERNS = (
    re.compile(r"[A-Za-z]:[\\/][Uu][Tt][Dd][Tt]"),
    re.compile(r"/mnt/[a-z]/", re.IGNORECASE),
    re.compile(r"/home/[^/\s]"),
    re.compile(r"[Dd]:\\\\"),
)

PROMOTED_VAR = "SOULMOD_RUNTIME_PROMOTED_SOURCES"
CORE_TARGET = "SoulMod.Runtime.Core"
PRODUCT_TARGET = "SoulMod.Runtime"
TARGET_SOURCE_SCOPES = ("PRIVATE", "PUBLIC", "INTERFACE")


@dataclass
class ValidationResult:
    passed: bool = True
    checks: list = field(default_factory=list)

    def record(self, name: str, ok: bool, detail: str = "") -> None:
        self.checks.append((name, "PASS" if ok else "FAIL", detail))
        if not ok:
            self.passed = False


def _strip_line_comments(text: str) -> str:
    return "\n".join(
        line[: line.find("#")] if "#" in line else line
        for line in text.splitlines()
    )


def _command_bodies(cleaned_text: str, command_name: str) -> list[tuple[int, str]]:
    """Yield (position, body) for each ``command_name(...)`` invocation."""
    pattern = re.compile(
        r"\b" + re.escape(command_name) + r"\s*\(",
    )
    results: list[tuple[int, str]] = []
    for match in pattern.finditer(cleaned_text):
        depth = 1
        start = match.end()
        i = start
        while i < len(cleaned_text) and depth > 0:
            if cleaned_text[i] == "(":
                depth += 1
            elif cleaned_text[i] == ")":
                depth -= 1
            i += 1
        results.append((match.start(), cleaned_text[start:i - 1]))
    return results


def _target_source_additions(cleaned_text: str, target: str) -> set[str]:
    additions: set[str] = set()
    for _, body in _command_bodies(cleaned_text, "target_sources"):
        tokens = body.split()
        if not tokens or tokens[0] != target:
            continue
        collecting = False
        for token in tokens[1:]:
            if token in TARGET_SOURCE_SCOPES:
                collecting = True
            elif collecting and not token.startswith("$"):
                additions.add(token)
    return additions


def compute_effective_sources(cleaned_text: str) -> set[str]:
    """Simulate CMake variable and target_sources mutations in source order.

    Returns the final set of source files compiled into the product target,
    accounting for set reassignment, list(APPEND)/list(REMOVE_ITEM), and
    target_sources additions.
    """
    var_value: list[str] = []
    events: list[tuple[int, str, str]] = []
    for pos, body in _command_bodies(cleaned_text, "set"):
        tokens = body.split()
        if tokens and tokens[0] == PROMOTED_VAR:
            events.append((pos, "set", " ".join(tokens[1:])))
    for pos, body in _command_bodies(cleaned_text, "list"):
        tokens = body.split()
        if len(tokens) >= 2 and tokens[1] == PROMOTED_VAR:
            events.append((pos, "list", " ".join(tokens)))
    events.sort(key=lambda e: e[0])
    for _, kind, payload in events:
        tokens = payload.split()
        if kind == "set":
            var_value = [t for t in tokens if not t.startswith("$")]
        elif kind == "list":
            op = tokens[0]
            if op == "APPEND":
                var_value.extend(t for t in tokens[2:] if not t.startswith("$"))
            elif op == "REMOVE_ITEM":
                remove = set(tokens[2:])
                var_value = [t for t in var_value if t not in remove]
    return set(var_value)


def _stale_build_sources(build_dir: Path) -> set[str]:
    """Return source basenames compiled by generated runtime project files."""
    compiled: set[str] = set()
    if not build_dir.is_dir():
        return compiled
    for vcxproj in build_dir.rglob("*.vcxproj"):
        text = vcxproj.read_text(encoding="utf-8", errors="replace")
        for match in re.finditer(r'<ClCompile\s+Include="([^"]*)"', text):
            compiled.add(match.group(1).replace("\\", "/").split("/")[-1])
    return compiled


def validate(cmake_path: Path = RUNTIME_CMAKE,
             build_dir: Path = RUNTIME_BUILD) -> ValidationResult:
    result = ValidationResult()
    if not cmake_path.is_file():
        result.record("cmake_file_exists", False, f"missing {cmake_path}")
        return result

    cmake_text = cmake_path.read_text(encoding="utf-8")
    cleaned = _strip_line_comments(cmake_text)

    promoted_declared = False
    for _, body in _command_bodies(cleaned, "set"):
        tokens = body.split()
        if tokens and tokens[0] == PROMOTED_VAR:
            promoted_declared = True
    result.record("promoted_variable_declared", promoted_declared)

    effective = compute_effective_sources(cleaned)
    core_effective = (effective - {"src/dllmain.cpp"}) | _target_source_additions(cleaned, CORE_TARGET)
    product_injections = _target_source_additions(cleaned, PRODUCT_TARGET)

    missing = EXPECTED_PROMOTED_SOURCES - effective
    result.record("all_required_promoted_sources_effective",
                  not missing, f"missing: {sorted(missing)}" if missing else "")

    extra = effective - EXPECTED_PROMOTED_SOURCES
    result.record("no_unexpected_effective_sources",
                  not extra, f"unexpected: {sorted(extra)}" if extra else "")

    leaked = effective & EXPECTED_EXCLUDED_SOURCES
    result.record("excluded_sources_absent_from_effective_target",
                  not leaked, f"leaked: {sorted(leaked)}" if leaked else "")

    expected_core = EXPECTED_PROMOTED_SOURCES - {"src/dllmain.cpp"}
    missing_core = expected_core - core_effective
    unexpected_core = core_effective - expected_core
    result.record("core_target_sources_effective",
                  not missing_core and not unexpected_core,
                  f"missing: {sorted(missing_core)}; unexpected: {sorted(unexpected_core)}")

    result.record("product_target_has_no_literal_source_injection",
                  not product_injections,
                  f"unexpected: {sorted(product_injections)}" if product_injections else "")

    excluded_declared = any(
        body.split() and body.split()[0] == "SOULMOD_RUNTIME_EXCLUDED_SOURCES"
        for _, body in _command_bodies(cleaned, "set")
    )
    result.record("excluded_variable_declared", excluded_declared)

    absent_on_disk = sorted(
        src for src in EXPECTED_PROMOTED_SOURCES
        if not (cmake_path.parent / src).is_file()
    )
    result.record("promoted_sources_exist_on_disk",
                  not absent_on_disk, f"absent: {absent_on_disk}" if absent_on_disk else "")

    core_uses_promoted_graph = bool(re.search(
        r"add_library\(\s*" + re.escape(CORE_TARGET) + r"\b[^)]*\$\{SOULMOD_RUNTIME_CORE_SOURCES\}", cleaned))
    product_uses_core_objects = "$<TARGET_OBJECTS:SoulMod.Runtime.Core>" in cleaned
    result.record("add_library_consumes_promoted_graph", core_uses_promoted_graph and product_uses_core_objects)

    include_blocks = re.findall(
        r"target_include_directories\((.*?)\)", cleaned, re.DOTALL)
    leaked_paths: list[str] = []
    for pattern in ABSOLUTE_PATH_PATTERNS:
        for match in pattern.finditer("\n".join(include_blocks)):
            leaked_paths.append(match.group(0))
    result.record("no_absolute_workstation_include_paths",
                  not leaked_paths, f"leaked: {leaked_paths}" if leaked_paths else "")

    roots_required = ("${SOULMOD_AURIE_ROOT}/Aurie/shared.hpp" in cleaned
                      and "${SOULMOD_YYTOOLKIT_ROOT}/YYToolkit/YYTK_Shared.hpp" in cleaned
                      and "SOULMOD_AURIE_ROOT" in "\n".join(include_blocks)
                      and "SOULMOD_YYTOOLKIT_ROOT" in "\n".join(include_blocks)
                      and "FATAL_ERROR" in cleaned)
    result.record("framework_roots_required_not_optional", roots_required)

    adapter_path = cmake_path.parent / "src" / "YYToolkitAdapter.cpp"
    adapter_text = adapter_path.read_text(encoding="utf-8") if adapter_path.is_file() else ""
    adapter_binds_framework = all(token in adapter_text for token in (
        "Aurie::Internal::g_PpGetFrameworkRoutine",
        'Aurie::ObGetInterface(',
        '"YYTK_Main"',
        "m_yytoolkitBound = true",
        "YYToolkitBindingStatus::AurieDispatcherUnavailable",
    ))
    result.record("adapter_uses_typed_aurie_yytk_binding",
                  adapter_binds_framework,
                  "Aurie dispatcher guard, YYTK_Main lookup, and bound state are required")

    identity_manifest = ("file(GENERATE" in cleaned
                         and "SoulMod.Runtime.build-identity-$<CONFIG>.txt" in cleaned
                         and "$<TARGET_FILE:SoulMod.Runtime>" in cleaned
                         and "$<TARGET_OBJECTS:SoulMod.Runtime.Core>" in cleaned)
    result.record("cmake_build_identity_manifest", identity_manifest,
                  "target binary and Core object graph must be emitted together")

    product_links_system_libraries = bool(re.search(
        r"target_link_libraries\(\s*SoulMod\.Runtime\s+PRIVATE\s+psapi\s+bcrypt\s*\)", cleaned))
    result.record("product_links_core_system_libraries", product_links_system_libraries,
                  "the DLL must explicitly link psapi and bcrypt used by Core objects")

    epoch_path = cmake_path.parent / "src" / "GlobalJsonEpoch.cpp"
    epoch_text = epoch_path.read_text(encoding="utf-8") if epoch_path.is_file() else ""
    epoch_is_atomic = all(token in epoch_text for token in (
        "compare_exchange_strong",
        "JsonApplyResult::NotReady",
        "host.Restore(*it",
        "JsonEpochState::Failed",
    ))
    result.record("global_json_epoch_has_atomic_rollback", epoch_is_atomic,
                  "pending retry, terminal failure, and reverse rollback are required")

    stale_compiled = _stale_build_sources(build_dir)
    stale_poc = stale_compiled & {Path(s).name for s in EXPECTED_EXCLUDED_SOURCES}
    if not build_dir.is_dir():
        result.record("stale_build_tree_clean", True, "no runtime/build directory")
    else:
        result.record("stale_build_tree_clean",
                      not stale_poc,
                      f"stale POC sources compiled by generated project: {sorted(stale_poc)}; "
                      "reconfigure on Windows to regenerate" if stale_poc else
                      "build tree present; no POC sources detected in generated projects")

    return result


def format_report(result: ValidationResult) -> str:
    lines = ["SoulMod.Runtime promoted source-map validation", "=" * 58]
    for name, status, detail in result.checks:
        line = f"[{status}] {name}"
        if detail:
            line += f" -- {detail}"
        lines.append(line)
    lines.append("=" * 58)
    lines.append(f"VERDICT: {'PASS' if result.passed else 'FAIL'}")
    return "\n".join(lines)


def main() -> int:
    result = validate()
    print(format_report(result))
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
