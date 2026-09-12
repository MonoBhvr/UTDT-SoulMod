#!/usr/bin/env python3
"""Failing-first contract test for the SoulMod.Runtime source-map validator.

Proves the validator catches every adversarial regression of the promoted
source graph, including CMake mutations a naive first-match parser misses:

  * the stale two-source POC build
  * omission of a required promoted executor
  * ``list(APPEND ... PocHook.cpp)`` mutation
  * ``target_sources(SoulMod.Runtime ... PocHook.cpp)`` injection
  * ``set`` reassignment (last assignment must win)
  * a hardcoded absolute workstation include path
  * a stale generated build tree compiling POC sources

The validator must pass the real promoted CMake on every source-graph check
and fail each simulated regression.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import runtime_source_map_validator as validator  # noqa: E402

PROMOTED = """\
    src/dllmain.cpp
    src/ModLoader.cpp
    src/PackageValidation.cpp
    src/JsonValidator.cpp
    src/GlobalJsonEpoch.cpp
    src/OperationsResolver.cpp
    src/DependencyGraph.cpp
    src/YYToolkitAdapter.cpp
    src/GameHookEngine.cpp
    src/HookManager.cpp
    src/Logger.cpp
    src/StringManager.cpp
    src/FontManager.cpp
"""
EXCLUDED = "    src/PocHook.cpp\n    src/SoulModEngine.cpp\n    src/HookRegistry.cpp\n"
SOURCE_GRAPH_CHECKS = {
    "promoted_variable_declared",
    "all_required_promoted_sources_effective",
    "no_unexpected_effective_sources",
    "excluded_sources_absent_from_effective_target",
    "core_target_sources_effective",
    "product_target_has_no_literal_source_injection",
    "excluded_variable_declared",
    "add_library_consumes_promoted_graph",
    "no_absolute_workstation_include_paths",
    "framework_roots_required_not_optional",
    "adapter_uses_typed_aurie_yytk_binding",
    "cmake_build_identity_manifest",
    "product_links_core_system_libraries",
    "global_json_epoch_has_atomic_rollback",
}


def _write_cmake(path: Path, promoted: str = PROMOTED, excluded: str = EXCLUDED,
                 extra: str = "",
                 include_line: str = "        ${CMAKE_CURRENT_SOURCE_DIR}/include") -> None:
    path.write_text(
        f"set(SOULMOD_RUNTIME_PROMOTED_SOURCES\n{promoted}\n)\n"
        f"set(SOULMOD_RUNTIME_EXCLUDED_SOURCES\n{excluded}\n)\n"
        "set(SOULMOD_RUNTIME_CORE_SOURCES ${SOULMOD_RUNTIME_PROMOTED_SOURCES})\n"
        "list(REMOVE_ITEM SOULMOD_RUNTIME_CORE_SOURCES src/dllmain.cpp)\n"
        "add_library(SoulMod.Runtime.Core OBJECT ${SOULMOD_RUNTIME_CORE_SOURCES})\n"
        "add_library(SoulMod.Runtime SHARED src/dllmain.cpp $<TARGET_OBJECTS:SoulMod.Runtime.Core>)\n"
        f"{extra}\n"
        "target_include_directories(SoulMod.Runtime\n    PUBLIC\n"
        f"{include_line}\n)\n"
        "set(SOULMOD_AURIE_ROOT /aurie CACHE PATH \"\")\n"
        "set(SOULMOD_YYTOOLKIT_ROOT /yytk CACHE PATH \"\")\n"
        "if(NOT SOULMOD_AURIE_ROOT)\n  message(FATAL_ERROR \"\")\nendif()\n",
        encoding="utf-8",
    )


def _failed_names(result) -> set:
    return {name for name, status, _ in result.checks if status == "FAIL"}


def test_real_source_graph_checks_pass() -> None:
    result = validator.validate(validator.RUNTIME_CMAKE, validator.RUNTIME_BUILD)
    graph_failures = _failed_names(result) & SOURCE_GRAPH_CHECKS
    assert not graph_failures, (
        "real CMake source-graph checks must pass; failed: "
        + ", ".join(sorted(graph_failures))
    )


def test_real_stale_build_is_detected() -> None:
    result = validator.validate(validator.RUNTIME_CMAKE, validator.RUNTIME_BUILD)
    assert "stale_build_tree_clean" in _failed_names(result), (
        "the known stale runtime/build tree compiling POC sources must be detected"
    )


def test_clean_build_dir_passes() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        result = validator.validate(validator.RUNTIME_CMAKE, Path(tmp) / "nonexistent")
    assert "stale_build_tree_clean" not in _failed_names(result)


def test_stale_two_source_poc_fails() -> None:
    stale = "    src/SoulModEngine.cpp\n    src/HookRegistry.cpp\n"
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "CMakeLists.txt"
        _write_cmake(path, stale, excluded="")
        result = validator.validate(path, Path(tmp) / "no-build")
    assert "all_required_promoted_sources_effective" in _failed_names(result)


def test_omitted_executor_fails() -> None:
    promoted = "\n".join(l for l in PROMOTED.splitlines() if "OperationsResolver" not in l)
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "CMakeLists.txt"
        _write_cmake(path, promoted + "\n")
        result = validator.validate(path, Path(tmp) / "no-build")
    assert "all_required_promoted_sources_effective" in _failed_names(result)


def test_list_append_poc_hook_fails() -> None:
    mutation = "list(APPEND SOULMOD_RUNTIME_PROMOTED_SOURCES src/PocHook.cpp)"
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "CMakeLists.txt"
        _write_cmake(path, PROMOTED, extra=mutation)
        result = validator.validate(path, Path(tmp) / "no-build")
    assert "excluded_sources_absent_from_effective_target" in _failed_names(result), (
        "list(APPEND) of PocHook must be caught by the effective-sources check"
    )


def test_target_sources_poc_injection_fails() -> None:
    mutation = "target_sources(SoulMod.Runtime PRIVATE src/SoulModEngine.cpp)"
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "CMakeLists.txt"
        _write_cmake(path, PROMOTED, extra=mutation)
        result = validator.validate(path, Path(tmp) / "no-build")
    assert "product_target_has_no_literal_source_injection" in _failed_names(result), (
        "product target_sources injection must be caught"
    )


def test_core_target_sources_poc_injection_fails() -> None:
    mutation = "target_sources(SoulMod.Runtime.Core PRIVATE src/PocHook.cpp)"
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "CMakeLists.txt"
        _write_cmake(path, PROMOTED, extra=mutation)
        result = validator.validate(path, Path(tmp) / "no-build")
    assert "core_target_sources_effective" in _failed_names(result), (
        "core target_sources injection must be caught"
    )


def test_set_reassignment_last_wins() -> None:
    reassigned = ("set(SOULMOD_RUNTIME_PROMOTED_SOURCES "
                  "src/SoulModEngine.cpp src/HookRegistry.cpp)")
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "CMakeLists.txt"
        _write_cmake(path, PROMOTED, extra=reassigned)
        result = validator.validate(path, Path(tmp) / "no-build")
    assert "all_required_promoted_sources_effective" in _failed_names(result), (
        "set reassignment must be honored (last wins)"
    )


def test_absolute_workstation_include_fails() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "CMakeLists.txt"
        _write_cmake(path, PROMOTED,
                     include_line='        d:\\\\UTDT-SoulMod\\\\scratch\\\\AurieFramework')
        result = validator.validate(path, Path(tmp) / "no-build")
    assert "no_absolute_workstation_include_paths" in _failed_names(result)


def test_stale_build_dir_detected() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        build = Path(tmp) / "build"
        build.mkdir()
        (build / "SoulMod.Runtime.vcxproj").write_text(
            '<Project><ItemGroup>'
            '<ClCompile Include="D:\\UTDT-SoulMod\\runtime\\SoulMod.Runtime\\src\\SoulModEngine.cpp" />'
            '<ClCompile Include="D:\\UTDT-SoulMod\\runtime\\SoulMod.Runtime\\src\\HookRegistry.cpp" />'
            '</ItemGroup></Project>',
            encoding="utf-8",
        )
        result = validator.validate(validator.RUNTIME_CMAKE, build)
    assert "stale_build_tree_clean" in _failed_names(result), (
        "a generated project compiling POC sources must trip the stale guard"
    )


def main() -> int:
    tests = [
        test_real_source_graph_checks_pass,
        test_real_stale_build_is_detected,
        test_clean_build_dir_passes,
        test_stale_two_source_poc_fails,
        test_omitted_executor_fails,
        test_list_append_poc_hook_fails,
        test_target_sources_poc_injection_fails,
        test_core_target_sources_poc_injection_fails,
        test_set_reassignment_last_wins,
        test_absolute_workstation_include_fails,
        test_stale_build_dir_detected,
    ]
    failures = 0
    for test in tests:
        try:
            test()
            print(f"PASS: {test.__name__}")
        except AssertionError as exc:
            print(f"FAIL: {test.__name__}: {exc}")
            failures += 1
    if failures:
        print(f"RESULT: FAIL ({failures}/{len(tests)} failed)")
        return 1
    print(f"RESULT: PASS ({len(tests)}/{len(tests)} passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
