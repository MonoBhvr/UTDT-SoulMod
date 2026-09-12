#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>

namespace SoulMod {

struct OperationTarget {
    std::string type;
    std::string name;
};

struct OperationPrecondition {
    std::optional<std::string> expectedValue;
    std::optional<std::string> sha256;
};

struct ModOperation {
    std::string id;
    std::string modId;
    int operationVersion = 1;
    OperationTarget target;
    std::string operationType; // string.replace, font.redirect, font.register, call.argument.replace, etc.
    std::string expectedValue;
    std::optional<OperationPrecondition> precondition;
    std::string value; // replacement text / font name / GML snippet
    std::string assetPath;
    std::string assetSha256;
    std::vector<std::string> mirrorGlobals;
    std::string conditionGlobal;
    std::string conditionEquals;
    int fontSize = 10;
    bool fontBold = false;
    bool fontItalic = false;
    int fontFirst = 32;
    int fontLast = 55203;
    int32_t priority = 100;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
};

struct ModManifest {
    int formatVersion = 1;
    std::string id;
    std::string name;
    std::string version;
    std::string targetFile;
    std::string targetDataSha256;
    std::vector<std::string> authors;
    std::vector<std::string> dependencies;
    std::vector<std::string> loadBefore;
    std::vector<std::string> loadAfter;
    std::string conflictPolicy = "stop";
    std::string operationsFile = "operations.json";
    std::string packagePath;
};

struct LoadedModPackage {
    ModManifest manifest;
    std::vector<ModOperation> operations;
};

} // namespace SoulMod
