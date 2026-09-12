#include "SoulMod/ModLoader.hpp"
#include "SoulMod/JsonValidator.hpp"
#include "SoulMod/Logger.hpp"
#include "SoulMod/PackageValidation.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace SoulMod {
namespace {

const std::string* RequiredString(const JsonValue& object, const std::string& key) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->type == JsonType::String && !value->stringValue.empty() ? &value->stringValue : nullptr;
}

const std::string* OptionalString(const JsonValue& object, const std::string& key) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->type == JsonType::String ? &value->stringValue : nullptr;
}

bool IsFormatVersionOne(const JsonValue& document) {
    const JsonValue* version = document.Find("formatVersion");
    return version != nullptr && version->type == JsonType::Number && version->numberValue == "1";
}

bool IsVersion(const std::string_view value) {
    if (value.empty()) return false;
    bool needsDigit = true;
    for (const char character : value) {
        if (character == '.') {
            if (needsDigit) return false;
            needsDigit = true;
        } else if (character >= '0' && character <= '9') {
            needsDigit = false;
        } else {
            return false;
        }
    }
    return !needsDigit;
}

bool IsContainedRegularFile(const fs::path& root, const fs::path& candidate) {
    std::error_code error;
    const fs::path canonicalRoot = fs::weakly_canonical(root, error);
    if (error) return false;
    const fs::path canonicalCandidate = fs::weakly_canonical(candidate, error);
    if (error || !fs::is_regular_file(canonicalCandidate)) return false;
    auto rootPart = canonicalRoot.begin();
    auto candidatePart = canonicalCandidate.begin();
    for (; rootPart != canonicalRoot.end(); ++rootPart, ++candidatePart) {
        if (candidatePart == canonicalCandidate.end() || *rootPart != *candidatePart) return false;
    }
    return true;
}

bool RequiresAsset(const std::string_view operation) {
    return operation == "global.json.load" || operation == "resource.redirect" || operation == "resource.replace" ||
        operation == "resource.add" || operation == "font.register";
}

bool IsSupportedOperation(const std::string_view operation) {
    return operation == "string.replace" || operation == "string.add" || operation == "font.redirect" ||
        operation == "font.register" || operation == "hook.before" || operation == "hook.after" ||
        operation == "hook.replace" || operation == "global.json.load" || operation == "resource.redirect";
}

bool IsResourceKey(const std::string& value) {
    const size_t separator = value.find(':');
    return separator != std::string::npos && separator != 0 && separator + 1 < value.size() && value.find(':', separator + 1) == std::string::npos;
}

bool ParseResourceKeys(const JsonValue* value, std::vector<std::string>& keys) {
    if (value == nullptr) return false;
    if (value->type != JsonType::Array) return false;
    std::set<std::string> unique;
    for (const JsonValue& entry : value->arrayValues) {
        if (entry.type != JsonType::String || !IsResourceKey(entry.stringValue) || !unique.insert(entry.stringValue).second) return false;
        keys.push_back(entry.stringValue);
    }
    return !keys.empty();
}

bool ParsePrecondition(const JsonValue* value, ModOperation& operation) {
    if (value == nullptr) return true;
    if (value->type != JsonType::Object) return false;
    OperationPrecondition precondition;
    if (const std::string* expected = OptionalString(*value, "expectedValue")) precondition.expectedValue = *expected;
    if (const std::string* sha256 = OptionalString(*value, "sha256")) precondition.sha256 = *sha256;
    if (!precondition.expectedValue && !precondition.sha256) return false;
    if (precondition.expectedValue) operation.expectedValue = *precondition.expectedValue;
    operation.precondition = std::move(precondition);
    return true;
}

bool ParseGlobalSelector(const JsonValue& operationValue, ModOperation& operation) {
    const JsonValue* selector = operationValue.Find("selector");
    if (selector == nullptr || selector->type != JsonType::Object) return false;
    const std::string* applyAt = RequiredString(*selector, "applyAt");
    if (applyAt == nullptr || *applyAt != "first.draw_set_font") return false;
    if (const JsonValue* mirrors = selector->Find("mirrorGlobals")) {
        if (mirrors->type != JsonType::Array) return false;
        for (const JsonValue& mirror : mirrors->arrayValues) {
            if (mirror.type != JsonType::String || mirror.stringValue.empty()) return false;
            operation.mirrorGlobals.push_back(mirror.stringValue);
        }
    }
    if (const JsonValue* when = selector->Find("when")) {
        if (when->type != JsonType::Object) return false;
        const std::string* global = RequiredString(*when, "global");
        const std::string* equals = RequiredString(*when, "equals");
        if (global == nullptr || equals == nullptr) return false;
        operation.conditionGlobal = *global;
        operation.conditionEquals = *equals;
    }
    return true;
}

bool ParseFontSelector(const JsonValue& operationValue, ModOperation& operation) {
    const JsonValue* selector = operationValue.Find("selector");
    if (selector == nullptr || selector->type != JsonType::Object) return false;
    const JsonValue* size = selector->Find("size"); const JsonValue* bold = selector->Find("bold");
    const JsonValue* italic = selector->Find("italic"); const JsonValue* first = selector->Find("first"); const JsonValue* last = selector->Find("last");
    if (size == nullptr || bold == nullptr || italic == nullptr || first == nullptr || last == nullptr || size->type != JsonType::Number || first->type != JsonType::Number || last->type != JsonType::Number || bold->type != JsonType::Boolean || italic->type != JsonType::Boolean) return false;
    const auto parse = [](const std::string& text, int& value) { const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value); return error == std::errc{} && ptr == text.data() + text.size(); };
    if (!parse(size->numberValue, operation.fontSize) || !parse(first->numberValue, operation.fontFirst) || !parse(last->numberValue, operation.fontLast) || operation.fontSize <= 0 || operation.fontFirst < 0 || operation.fontLast < operation.fontFirst) return false;
    operation.fontBold = bold->booleanValue; operation.fontItalic = italic->booleanValue;
    return true;
}

bool ParseOperation(const JsonValue& value, const fs::path& packageRoot, const std::string& packageId, ModOperation& operation) {
    if (value.type != JsonType::Object) return false;
    const std::string* id = RequiredString(value, "id");
    const std::string* modId = RequiredString(value, "modId");
    const std::string* kind = RequiredString(value, "operation");
    const JsonValue* target = value.Find("target");
    const JsonValue* operationVersion = value.Find("operationVersion");
    if (id == nullptr || modId == nullptr || *modId != packageId || kind == nullptr || target == nullptr || target->type != JsonType::Object ||
        (operationVersion != nullptr && (operationVersion->type != JsonType::Number || operationVersion->numberValue != "1"))) return false;
    const std::string* targetType = RequiredString(*target, "type");
    const std::string* targetName = RequiredString(*target, "name");
    if (targetType == nullptr || targetName == nullptr) return false;
    operation = {};
    operation.id = *id;
    operation.modId = *modId;
    operation.operationVersion = 1;
    operation.operationType = *kind;
    if (!IsSupportedOperation(operation.operationType)) return false;
    operation.target = { *targetType, *targetName };
    if (const std::string* valueText = OptionalString(value, "value")) operation.value = *valueText;
    if (const std::string* expected = OptionalString(value, "expectedValue")) {
        operation.expectedValue = *expected;
        operation.precondition = OperationPrecondition{ *expected, std::nullopt };
    }
    if (const std::string* asset = OptionalString(value, "assetPath")) operation.assetPath = *asset;
    if (const std::string* assetHash = OptionalString(value, "assetSha256")) operation.assetSha256 = *assetHash;
    if (!ParsePrecondition(value.Find("precondition"), operation)) return false;
    if (const JsonValue* reads = value.Find("reads")) { if (!ParseResourceKeys(reads, operation.reads)) return false; }
    else operation.reads = { operation.target.type + ":" + operation.target.name };
    if (const JsonValue* writes = value.Find("writes")) { if (!ParseResourceKeys(writes, operation.writes)) return false; }
    else operation.writes = { operation.target.type + ":" + operation.target.name };
    if (operation.operationType == "global.json.load" && !ParseGlobalSelector(value, operation)) return false;
    if (operation.operationType == "font.register" && !ParseFontSelector(value, operation)) return false;
    if (!RequiresAsset(operation.operationType)) return true;
    if (!IsSafeRelativePath(operation.assetPath) || !IsSha256(operation.assetSha256)) return false;
    std::string error;
    return IsContainedRegularFile(packageRoot, packageRoot / operation.assetPath) &&
        VerifySha256File(packageRoot / operation.assetPath, operation.assetSha256, error);
}

bool ReadJsonFile(const std::string& path, JsonValue& document, std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) { error = "cannot open JSON file"; return false; }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return ParseJson(buffer.str(), document, error);
}

}

ModLoader& ModLoader::Instance() { static ModLoader instance; return instance; }

void ModLoader::SetRuntimeTargetDirectory(fs::path directory) { runtimeTargetDirectory_ = std::move(directory); }

std::vector<LoadedModPackage> ModLoader::DiscoverAndLoadMods(const std::string& modsDirectory) {
    std::vector<LoadedModPackage> packages;
    if (!fs::is_directory(modsDirectory)) return packages;
    for (const auto& entry : fs::recursive_directory_iterator(modsDirectory)) {
        if (!entry.is_directory() || !fs::is_regular_file(entry.path() / "manifest.json")) continue;
        LoadedModPackage package;
        if (LoadSinglePackageFromDir(entry.path().string(), package)) packages.push_back(std::move(package));
    }
    return packages;
}

bool ModLoader::LoadSinglePackageFromDir(const std::string& modDirPath, LoadedModPackage& outPackage) {
    outPackage = {};
    const fs::path root = fs::weakly_canonical(modDirPath);
    const fs::path manifestPath = root / "manifest.json";
    if (!IsContainedRegularFile(root, manifestPath) || !ParseManifestFile(manifestPath.string(), outPackage.manifest)) return false;
    outPackage.manifest.packagePath = root.string();
    const fs::path operationsPath = root / outPackage.manifest.operationsFile;
    if (!IsSafeRelativePath(outPackage.manifest.operationsFile) || !IsContainedRegularFile(root, operationsPath)) return false;
    const fs::path targetDirectory = runtimeTargetDirectory_.empty() ? root : runtimeTargetDirectory_;
    std::string error;
    if (!outPackage.manifest.targetDataSha256.empty() &&
        !VerifySha256File(targetDirectory / outPackage.manifest.targetFile, outPackage.manifest.targetDataSha256, error)) return false;
    return ParseOperationsFile(root, operationsPath.string(), outPackage.manifest.id, outPackage.operations);
}

bool ModLoader::ParseManifestFile(const std::string& manifestPath, ModManifest& outManifest) {
    JsonValue document;
    std::string error;
    if (!ReadJsonFile(manifestPath, document, error) || document.type != JsonType::Object || !IsFormatVersionOne(document)) return false;
    const std::string* id = RequiredString(document, "id");
    const std::string* name = RequiredString(document, "name");
    const std::string* version = RequiredString(document, "version");
    const std::string* targetFile = RequiredString(document, "targetFile");
    const std::string* targetHash = OptionalString(document, "targetDataSha256");
    const std::string* operations = OptionalString(document, "operationsFile");
    if (id == nullptr || name == nullptr || version == nullptr || targetFile == nullptr ||
        !IsVersion(*version) || !IsSafeRelativePath(*targetFile) || (targetHash != nullptr && !IsSha256(*targetHash)) ||
        (operations != nullptr && !IsSafeRelativePath(*operations))) return false;
    outManifest = {};
    outManifest.formatVersion = 1;
    outManifest.id = *id;
    outManifest.name = *name;
    outManifest.version = *version;
    outManifest.targetFile = *targetFile;
    if (targetHash != nullptr) outManifest.targetDataSha256 = *targetHash;
    if (operations != nullptr) outManifest.operationsFile = *operations;
    if (const std::string* policy = OptionalString(document, "conflictPolicy")) outManifest.conflictPolicy = *policy;
    return true;
}

bool ModLoader::ParseOperationsFile(const fs::path& packageRoot, const std::string& operationsPath, const std::string& modId, std::vector<ModOperation>& outOps) {
    JsonValue document;
    std::string error;
    if (!ReadJsonFile(operationsPath, document, error) || document.type != JsonType::Array) return false;
    std::vector<ModOperation> parsed;
    parsed.reserve(document.arrayValues.size());
    for (const JsonValue& value : document.arrayValues) {
        ModOperation operation;
        if (!ParseOperation(value, packageRoot, modId, operation)) return false;
        parsed.push_back(std::move(operation));
    }
    outOps = std::move(parsed);
    return true;
}

}
