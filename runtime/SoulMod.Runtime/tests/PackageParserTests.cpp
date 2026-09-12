// ModLoader package parser contract tests -- manifest + operations parsing.
//
// Exercises SoulMod::ModLoader::LoadSinglePackageFromDir over on-disk temp
// packages: valid manifest+operations parsing, malformed-JSON rejection (the
// parser validates structure and propagates failure), missing-manifest
// rejection, empty-id rejection, default operationsFile, and operation
// dispatch fields. Windows-lane only (links SoulMod.Runtime.Core).

#include "SoulMod/ModLoader.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace
{
    int g_failureCount = 0;

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failureCount;
        }
    }

    class TempDir
    {
    public:
        TempDir()
        {
            const auto base = fs::temp_directory_path() / "soulmod-runtime-parsertest";
            for (int attempt = 0; attempt < 1000; ++attempt)
            {
                candidate_ = base;
                candidate_ += "-";
                candidate_ += std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count() + attempt);
                std::error_code ec;
                if (fs::create_directories(candidate_, ec))
                    return;
            }
            candidate_.clear();
        }

        ~TempDir()
        {
            if (!candidate_.empty())
            {
                std::error_code ec;
                fs::remove_all(candidate_, ec);
            }
        }

        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;

        const fs::path& Path() const { return candidate_; }
        bool Valid() const { return !candidate_.empty(); }

    private:
        fs::path candidate_;
    };

    void WriteFile(const fs::path& path, const std::string& content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }
}

// Given: a package directory with a valid manifest and operations file
// When:  LoadSinglePackageFromDir parses it
// Then:  id, name, version, targetFile, and operations are populated.
void Test_valid_package_parses_fully()
{
    TempDir dir;
    Require(dir.Valid(), "temp dir must be created");
    WriteFile(dir.Path() / "manifest.json",
        R"({"formatVersion":1,"id":"test.mod","name":"Test Mod","version":"1.0.0","targetFile":"data.win","operationsFile":"ops.json"})");
    WriteFile(dir.Path() / "ops.json",
        R"([{"id":"op.replace","modId":"test.mod","operation":"string.replace","value":"hello","target":{"type":"string","name":"greeting"}}])");

    SoulMod::LoadedModPackage pkg;
    Require(SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(dir.Path().string(), pkg),
        "valid package must parse");
    Require(pkg.manifest.id == "test.mod", "manifest id must be retained");
    Require(pkg.manifest.name == "Test Mod", "manifest name must be retained");
    Require(pkg.manifest.version == "1.0.0", "manifest version must be retained");
    Require(pkg.manifest.targetFile == "data.win", "manifest targetFile must be retained");
    Require(pkg.manifest.operationsFile == "ops.json", "operationsFile must be retained");
    Require(pkg.operations.size() == 1, "exactly one operation must be parsed");
    if (!pkg.operations.empty())
    {
        const auto& op = pkg.operations.front();
        Require(op.id == "op.replace", "operation id must be retained");
        Require(op.operationType == "string.replace", "operation type must be retained");
        Require(op.value == "hello", "operation value must be retained");
        Require(op.target.type == "string", "operation target type must be retained");
        Require(op.target.name == "greeting", "operation target name must be retained");
    }
}

// Given: a manifest with an unclosed brace (structurally invalid JSON)
// When:  LoadSinglePackageFromDir parses it
// Then:  it returns false (malformed manifest is rejected, not permissively
//        accepted).
void Test_malformed_manifest_is_rejected()
{
    TempDir dir;
    Require(dir.Valid(), "temp dir must be created");
    WriteFile(dir.Path() / "manifest.json", R"({"id":"broken","name":"Broken)");
    WriteFile(dir.Path() / "ops.json", "[]");
    SoulMod::LoadedModPackage pkg;
    Require(!SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(dir.Path().string(), pkg),
        "malformed manifest JSON must be rejected");
}

// Given: a valid manifest but an operations file with an unclosed array
// When:  LoadSinglePackageFromDir parses it
// Then:  it returns false (ParseOperationsFile failure is now propagated, not
//        silently ignored).
void Test_malformed_operations_is_rejected()
{
    TempDir dir;
    Require(dir.Valid(), "temp dir must be created");
    WriteFile(dir.Path() / "manifest.json",
        R"({"formatVersion":1,"id":"ops.broken","name":"Ops","version":"1.0.0","targetFile":"data.win","operationsFile":"ops.json"})");
    WriteFile(dir.Path() / "ops.json", R"([{"id":"op","operation":"string.replace")");
    SoulMod::LoadedModPackage pkg;
    Require(!SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(dir.Path().string(), pkg),
        "malformed operations JSON must be rejected and propagated");
}

// Given: a manifest whose top-level JSON value is an array, not an object
// When:  LoadSinglePackageFromDir parses it
// Then:  it returns false (manifest root must be a JSON object).
void Test_non_object_manifest_is_rejected()
{
    TempDir dir;
    Require(dir.Valid(), "temp dir must be created");
    WriteFile(dir.Path() / "manifest.json", R"([1, 2, 3])");
    SoulMod::LoadedModPackage pkg;
    Require(!SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(dir.Path().string(), pkg),
        "non-object manifest root must be rejected");
}

// Given: an operations file whose top-level JSON value is an object, not array
// When:  LoadSinglePackageFromDir parses it
// Then:  it returns false (operations root must be a JSON array).
void Test_non_array_operations_is_rejected()
{
    TempDir dir;
    Require(dir.Valid(), "temp dir must be created");
    WriteFile(dir.Path() / "manifest.json",
        R"({"formatVersion":1,"id":"obj.ops","name":"Obj","version":"1.0.0","targetFile":"data.win","operationsFile":"ops.json"})");
    WriteFile(dir.Path() / "ops.json", R"({"id":"op","operation":"string.replace"})");
    SoulMod::LoadedModPackage pkg;
    Require(!SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(dir.Path().string(), pkg),
        "non-array operations root must be rejected");
}

// Given: a directory with no manifest.json
// When:  LoadSinglePackageFromDir is called
// Then:  it returns false.
void Test_missing_manifest_is_rejected()
{
    TempDir dir;
    Require(dir.Valid(), "temp dir must be created");
    SoulMod::LoadedModPackage pkg;
    Require(!SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(dir.Path().string(), pkg),
        "missing manifest must be rejected");
}

// Given: a manifest whose id field is absent
// When:  LoadSinglePackageFromDir parses it
// Then:  it returns false (id is the required key).
void Test_manifest_without_id_is_rejected()
{
    TempDir dir;
    Require(dir.Valid(), "temp dir must be created");
    WriteFile(dir.Path() / "manifest.json",
        R"({"name":"No Id","version":"1.0.0","targetFile":"data.win","operationsFile":"ops.json"})");
    WriteFile(dir.Path() / "ops.json", "[]");
    SoulMod::LoadedModPackage pkg;
    Require(!SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(dir.Path().string(), pkg),
        "manifest without id must be rejected");
}

// Given: a manifest that omits operationsFile
// When:  LoadSinglePackageFromDir parses it
// Then:  it defaults operationsFile to "operations.json" and loads that file.
void Test_default_operations_file_resolves()
{
    TempDir dir;
    Require(dir.Valid(), "temp dir must be created");
    WriteFile(dir.Path() / "manifest.json",
        R"({"formatVersion":1,"id":"default.ops","name":"Default","version":"1.0.0","targetFile":"data.win"})");
    WriteFile(dir.Path() / "operations.json",
        R"([{"id":"op.font","modId":"default.ops","operation":"font.redirect","value":"custom.font","target":{"type":"font","name":"fnt_main"}}])");
    SoulMod::LoadedModPackage pkg;
    Require(SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(dir.Path().string(), pkg),
        "package with default operationsFile must parse");
    Require(pkg.manifest.operationsFile == "operations.json", "operationsFile must default to operations.json");
    Require(pkg.operations.size() == 1, "default operations file must be loaded");
    if (!pkg.operations.empty())
    {
        Require(pkg.operations.front().operationType == "font.redirect", "font redirect operation must be parsed");
        Require(pkg.operations.front().target.name == "fnt_main", "font target name must be retained");
    }
}

int main()
{
    Test_valid_package_parses_fully();
    Test_malformed_manifest_is_rejected();
    Test_malformed_operations_is_rejected();
    Test_non_object_manifest_is_rejected();
    Test_non_array_operations_is_rejected();
    Test_missing_manifest_is_rejected();
    Test_manifest_without_id_is_rejected();
    Test_default_operations_file_resolves();

    if (g_failureCount != 0)
    {
        std::cerr << "RESULT: FAIL (" << g_failureCount << " assertion(s) failed)\n";
        return 1;
    }
    std::cout << "RESULT: PASS (ModLoader manifest/operations parser + malformed-JSON rejection contract)\n";
    return 0;
}
