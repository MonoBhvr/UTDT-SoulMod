# /// script
# requires-python = ">=3.12"
# ///
# ─── How to run ───
# PYTHONDONTWRITEBYTECODE=1 python3 tools/package_contract_test.py

from __future__ import annotations

import json
from hashlib import sha256
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import TypeAlias

JsonValue: TypeAlias = str | int | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
ROOT = Path(__file__).resolve().parents[1]
MANIFEST_REQUIRED = {"formatVersion", "id", "name", "version", "targetFile", "targets", "operationsFile"}
MANIFEST_ALLOWED = {
    "formatVersion", "id", "name", "version", "targetFile", "targetDataSha256", "targets",
    "authors", "license", "dependencies", "loadBefore", "loadAfter", "conflictPolicy", "operationsFile",
}
OPERATION_REQUIRED = {"id", "operationVersion", "modId", "target", "operation", "reads", "writes"}
CHAPTER1_DATA_SHA256 = "82c2bb61b8d78cd287120f6301588fecba34ec5a890bac711b7a8774c760ec70"
CHAPTER1_LANGUAGE_SHA256 = "78a7f9f2fece7fd7e4ebc6fb44cb0c0a2adb42f6a27ff364d6b93342259b7425"
GALMURI11_SHA256 = "2c709890595668f7bdb6df408420fda957dde0288e95b31a1cc17a2ab98b4b4f"


def load_json(path: Path) -> JsonValue:
    return json.loads(path.read_text(encoding="utf-8"))


def test_schema_declares_v1_closed_contract() -> None:
    manifest = load_json(ROOT / "shared" / "schema" / "manifest.schema.json")
    operations = load_json(ROOT / "shared" / "schema" / "operations.schema.json")
    assert isinstance(manifest, dict)
    assert isinstance(operations, dict)
    assert manifest["additionalProperties"] is False
    assert set(manifest["required"]) == MANIFEST_REQUIRED
    assert manifest["properties"]["formatVersion"] == {"const": 1}
    operation = operations["$defs"]["operation"]
    assert operation["additionalProperties"] is False
    assert set(operation["required"]) == OPERATION_REQUIRED
    assert operation["properties"]["operationVersion"] == {"const": 1}


def test_shared_cases_name_each_required_failure() -> None:
    cases = load_json(ROOT / "tests" / "fixtures" / "package-contract" / "cases.json")
    assert isinstance(cases, list)
    names = {case["name"] for case in cases if isinstance(case, dict)}
    assert names == {
        "valid-package",
        "malformed-json",
        "unknown-format-version",
        "unknown-operation-version",
        "unknown-operation-enum",
        "mismatched-mod-id",
        "duplicate-operation-id",
        "malformed-global-selector",
        "missing-asset-hash",
        "invalid-target-fingerprint",
    }


def test_promoted_runtime_font_binding_is_generic() -> None:
    runtime_root = ROOT / "runtime" / "SoulMod.Runtime"
    engine = (runtime_root / "src" / "GameHookEngine.cpp").read_text(encoding="utf-8")
    promoted_sources = (runtime_root / "CMakeLists.txt").read_text(encoding="utf-8")
    core_text = "\n".join(
        source.read_text(encoding="utf-8")
        for source in sorted((runtime_root / "src").glob("*.cpp"))
    ).casefold()

    assert "BindFontRedirects" in engine
    assert '"asset_get_index"' in engine
    assert "dtkr" not in promoted_sources.casefold()
    assert "runtime-proof" not in promoted_sources.casefold()
    assert "dtkr" not in core_text
    assert "runtime-proof" not in core_text
    assert "assets/lang/lang_ja.json" not in core_text
    assert "assets/fonts/galmuri11.ttf" not in core_text


def validate_mod_manifests(mods_root: Path) -> None:
    package_directories = sorted(path for path in mods_root.iterdir() if path.is_dir())
    assert package_directories
    for package_directory in package_directories:
        manifest_path = package_directory / "manifest.json"
        assert manifest_path.is_file(), f"{package_directory.name} is missing manifest.json"
        manifest = load_json(manifest_path)
        assert isinstance(manifest, dict), f"{package_directory.name} manifest must be an object"
        assert manifest.get("formatVersion") == 1, f"{package_directory.name} must declare formatVersion 1"
        unknown_fields = set(manifest) - MANIFEST_ALLOWED
        assert not unknown_fields, f"{package_directory.name} has legacy or unknown fields: {sorted(unknown_fields)}"


def test_all_immediate_mod_packages_reject_legacy_manifest_shapes() -> None:
    validate_mod_manifests(ROOT / "mods")


def test_legacy_manifest_shape_fails_mod_package_scan() -> None:
    with TemporaryDirectory() as temporary_root:
        mods_root = Path(temporary_root)
        legacy_package = mods_root / "legacy-package"
        legacy_package.mkdir()
        (legacy_package / "manifest.json").write_text(json.dumps({
            "formatVersion": 1,
            "id": "legacy.package",
            "name": "Legacy package",
            "version": "1.0.0",
            "targetFile": "data.win",
            "stringsFile": "strings.json",
            "priority": 200,
        }), encoding="utf-8")
        try:
            validate_mod_manifests(mods_root)
        except AssertionError as error:
            assert "priority" in str(error)
            assert "stringsFile" in str(error)
        else:
            raise AssertionError("legacy manifest fields must fail the mods package scan")


def applies_for_language(expected_language: str, active_language: str) -> bool:
    return active_language == expected_language


def test_chapter1_package_declares_verified_japanese_operations() -> None:
    package_root = ROOT / "mods" / "dtkr.ch1"
    manifest = load_json(package_root / "manifest.json")
    operations = load_json(package_root / "operations.json")
    assert isinstance(manifest, dict)
    assert isinstance(operations, list)

    assert manifest["formatVersion"] == 1
    assert manifest["id"] == "dtkr.ch1"
    assert manifest["targetFile"] == "chapter1_windows/data.win"
    assert manifest["targetDataSha256"] == CHAPTER1_DATA_SHA256
    assert manifest["targets"] == [{
        "id": "deltarune.chapter1",
        "targetFile": "chapter1_windows/data.win",
        "targetDataSha256": CHAPTER1_DATA_SHA256,
    }]

    entries = {entry["operation"]: entry for entry in operations if isinstance(entry, dict)}
    assert set(entries) == {"global.json.load", "font.register", "font.redirect"}

    language = entries["global.json.load"]
    assert language["target"] == {"type": "global", "name": "lang_map"}
    assert language["assetPath"] == "assets/lang/lang_ja.json"
    assert language["assetSha256"] == CHAPTER1_LANGUAGE_SHA256
    condition = language["selector"]["when"]
    assert condition == {"global": "lang", "equals": "ja"}
    assert applies_for_language(condition["equals"], "ja")
    assert not applies_for_language(condition["equals"], "en")

    font = entries["font.register"]
    assert font["target"] == {"type": "font", "name": "soulmod.galmuri11"}
    assert font["assetPath"] == "assets/fonts/Galmuri11.ttf"
    assert font["assetSha256"] == GALMURI11_SHA256
    assert font["selector"] == {"size": 10, "bold": False, "italic": False, "first": 32, "last": 55203}

    redirect = entries["font.redirect"]
    assert redirect["target"] == {"type": "font", "name": "fnt_ja_main"}
    assert redirect["value"] == "soulmod.galmuri11"
    assert "selector" not in redirect

    for asset_path, expected_hash in (
        ("assets/lang/lang_ja.json", CHAPTER1_LANGUAGE_SHA256),
        ("assets/fonts/Galmuri11.ttf", GALMURI11_SHA256),
    ):
        assert sha256((package_root / asset_path).read_bytes()).hexdigest() == expected_hash


def main() -> int:
    test_schema_declares_v1_closed_contract()
    test_shared_cases_name_each_required_failure()
    test_promoted_runtime_font_binding_is_generic()
    test_all_immediate_mod_packages_reject_legacy_manifest_shapes()
    test_legacy_manifest_shape_fails_mod_package_scan()
    test_chapter1_package_declares_verified_japanese_operations()
    print("RESULT: PASS (6/6 package contract checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
