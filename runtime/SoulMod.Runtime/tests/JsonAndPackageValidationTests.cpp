#include "SoulMod/JsonValidator.hpp"
#include "SoulMod/ModLoader.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class TempPackage {
public:
    TempPackage() {
        path_ = fs::temp_directory_path() / ("soulmod-json-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }

    ~TempPackage() { std::error_code error; fs::remove_all(path_, error); }
    const fs::path& Path() const { return path_; }

private:
    fs::path path_;
};

void Write(const fs::path& path, const char* text) {
    std::ofstream output(path);
    output << text;
}

bool Load(const TempPackage& package) {
    SoulMod::LoadedModPackage loaded;
    return SoulMod::ModLoader::Instance().LoadSinglePackageFromDir(package.Path().string(), loaded);
}

void Test_invalid_escapes_are_rejected() {
    std::string error;
    Require(!SoulMod::ValidateJson(R"("bad\q")", error), "invalid escape must fail");
    Require(!SoulMod::ValidateJson(R"("\u12G4")", error), "non-hex Unicode escape must fail");
    Require(!SoulMod::ValidateJson(R"("\uD800")", error), "unpaired high surrogate must fail");
    Require(!SoulMod::ValidateJson(R"("\uDC00")", error), "unpaired low surrogate must fail");
}

void Test_valid_surrogate_pair_parses() {
    SoulMod::JsonValue value;
    std::string error;
    Require(SoulMod::ParseJson(R"("\uD83D\uDE00")", value, error), "valid surrogate pair must parse");
    Require(value.type == SoulMod::JsonType::String && !value.stringValue.empty(), "parsed Unicode string must be retained");
}

void Test_nested_manifest_keys_do_not_satisfy_required_fields() {
    TempPackage package;
    Write(package.Path() / "manifest.json", R"({"metadata":{"id":"nested","name":"Nested","version":"1","targetFile":"data.win"}})");
    Write(package.Path() / "operations.json", "[]");
    Require(!Load(package), "nested manifest keys must not satisfy root requirements");
}

void Test_invalid_operations_are_rejected() {
    TempPackage package;
    Write(package.Path() / "manifest.json", R"({"id":"valid","name":"Valid","version":"1","targetFile":"data.win"})");
    Write(package.Path() / "operations.json", R"([{"id":"op","metadata":{"operation":"string.replace"},"target":{"type":"string","name":"x"}}])");
    Require(!Load(package), "nested operation key must not satisfy required operation");
}

void Test_missing_operation_target_fields_are_rejected() {
    TempPackage package;
    Write(package.Path() / "manifest.json", R"({"id":"valid","name":"Valid","version":"1","targetFile":"data.win"})");
    Write(package.Path() / "operations.json", R"([{"id":"op","operation":"string.replace","target":{"type":"string"}}])");
    Require(!Load(package), "operation target without name must fail");
}

}

int main() {
    Test_invalid_escapes_are_rejected();
    Test_valid_surrogate_pair_parses();
    Test_nested_manifest_keys_do_not_satisfy_required_fields();
    Test_invalid_operations_are_rejected();
    Test_missing_operation_target_fields_are_rejected();
    if (failures != 0) {
        std::cerr << "RESULT: FAIL (" << failures << " assertion(s) failed)\n";
        return 1;
    }
    std::cout << "RESULT: PASS (strict JSON and package structure)\n";
    return 0;
}
