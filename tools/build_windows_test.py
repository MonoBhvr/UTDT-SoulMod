from __future__ import annotations

from pathlib import Path
from xml.etree import ElementTree


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "build_windows.ps1"
WORKER_PROJECT = ROOT / "converter" / "SoulMod.UmtWorker" / "SoulMod.UmtWorker.csproj"
README = ROOT / "README.md"


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"{context}: missing {needle!r}")


def test_entrypoint_contract() -> None:
    script = SCRIPT.read_text(encoding="utf-8")
    require(script, "param(", "PowerShell entrypoint")
    require(script, "[string] $RepoRoot", "configurable repository root")
    require(script, "[switch] $PrerequisitesOnly", "deterministic prerequisite seam")
    require(script, "Join-Path $PSScriptRoot '..'", "repository-relative default root")
    require(script, "RepoRoot does not exist", "actionable malformed root failure")
    require(script, "PREREQUISITE_MISSING", "typed missing prerequisite failure")
    require(script, "VS2022/MSVC", "actionable Visual Studio prerequisite")
    require(script, "AurieRoot", "configurable Aurie prerequisite")
    require(script, "Aurie/TestModule/source/Aurie/shared.hpp", "Aurie include-root prerequisite")
    require(script, "YYToolkit/ExamplePlugin/include/YYToolkit/YYTK_Shared.hpp", "YYToolkit include-root prerequisite")
    require(script, "-DSOULMOD_AURIE_ROOT=$script:AurieIncludeRoot", "Aurie CMake root propagation")
    require(script, "-DSOULMOD_YYTOOLKIT_ROOT=$script:YYToolkitIncludeRoot", "YYToolkit CMake root propagation")
    require(script, "'-B'", "CMake configure")
    require(script, "'-A', 'x64'", "x64 CMake configure")
    require(script, "'test'", "converter tests")
    require(script, "--test-dir", "promoted CTest execution")
    require(script, "SoulMod.Runtime.build-identity-$Configuration.txt", "CMake build identity consumption")
    require(script, "BUILD_IDENTITY_MISMATCH", "typed build identity failure")
    require(script, "Get-FileHash", "runtime DLL SHA-256 capture")
    require(script, "SoulMod.Runtime.dll", "runtime artifact verification")
    require(script, "Remove-Artifact $buildDirectory", "stale runtime build directory removal")
    require(script, "PocHook.cpp|SoulModEngine.cpp|HookRegistry.cpp", "identity source provenance rejection")
    require(script, "SoulMod.UmtWorker.exe", "worker artifact verification")
    require(script, "Remove-Artifact $outputDirectory", "stale worker output removal")
    require(script, "Require-FreshArtifact", "fresh artifact verification")
    require(script, "COMMAND_TIMEOUT", "bounded external command")
    require(script, "COMMAND_TIMEOUT_CLEANUP", "typed timeout cleanup failure")
    require(script, "Stop-ProcessTree", "process tree timeout cleanup")
    require(script, ".WaitForExit()", "post-termination wait")
    require(script, ".Dispose()", "process disposal")
    require(script, "-p:SoulModUmtWorkerPath=", "test-lane worker property")
    require(script, "SOULMOD_UMT_WORKER_PATH", "worker environment pin")
    require(script, "finally", "worker environment restoration")
    require(script, "Remove-Item Env:SOULMOD_UMT_WORKER_PATH", "worker environment cleanup")
    if "ValidateSet('Debug', 'Release')" in script:
        raise AssertionError("native proof lane cannot expose unsupported Debug configuration")

    worker_publish = script.index("function Publish-Worker")
    converter_test = script.index("'test', $solution")
    if worker_publish >= converter_test:
        raise AssertionError("worker publication must precede converter tests")
    fresh_worker = script.index("'published SoulMod.UmtWorker.dll'")
    worker_assignment = script.index("$script:WorkerExecutable =")
    environment_pin = script.index("$env:SOULMOD_UMT_WORKER_PATH = $script:WorkerExecutable")
    environment_restore = script.index("Remove-Item Env:SOULMOD_UMT_WORKER_PATH")
    if not fresh_worker < worker_assignment < environment_pin < converter_test < environment_restore:
        raise AssertionError("fresh worker verification, environment pin, test, and restoration are out of order")

    for machine_path in ("d:\\UTDT-SoulMod", "C:\\Program Files", "/mnt/"):
        if machine_path in script:
            raise AssertionError(f"machine-specific path leaked into entrypoint: {machine_path}")


def test_worker_project_contract() -> None:
    project = WORKER_PROJECT.read_text(encoding="utf-8")
    require(project, "UmtWorkerDependencyDirectory", "declared worker dependency directory")
    require(project, "UmtWorkerDependencyDirectory is required", "missing dependency failure")
    if "..\\..\\scratch\\lib" in project:
        raise AssertionError("worker project still contains the machine-specific scratch/lib hint path")


def test_converter_test_project_declares_worker_prerequisite() -> None:
    root = ElementTree.parse(ROOT / "converter" / "SoulMod.Converter.Tests" / "SoulMod.Converter.Tests.csproj").getroot()
    targets = [element for element in root if element.tag == "Target"]
    target = next((element for element in targets if element.attrib.get("Name") == "ValidateSoulModUmtWorkerPath"), None)
    if target is None:
        raise AssertionError("converter test project must declare ValidateSoulModUmtWorkerPath")
    if target.attrib.get("BeforeTargets") != "VSTest":
        raise AssertionError("worker prerequisite must run before VSTest")
    errors = [element for element in target if element.tag == "Error"]
    error_text = "\n".join(element.attrib.get("Text", "") for element in errors)
    require(error_text, "SoulModUmtWorkerPath is required", "missing worker path failure")
    require(error_text, "does not exist", "stale/absent worker path failure")


def test_documented_invocation_contract() -> None:
    readme = README.read_text(encoding="utf-8")
    require(readme, "tools/build_windows.ps1", "documented Windows entrypoint")
    require(readme, "-UmtDependencyDirectory", "documented worker dependency parameter")
    require(readme, "-AurieRoot", "documented Aurie parameter")
    require(readme, "PrerequisitesOnly", "documented prerequisite check")


def main() -> None:
    test_entrypoint_contract()
    test_worker_project_contract()
    test_converter_test_project_declares_worker_prerequisite()
    test_documented_invocation_contract()
    print("RESULT: PASS")


if __name__ == "__main__":
    main()
