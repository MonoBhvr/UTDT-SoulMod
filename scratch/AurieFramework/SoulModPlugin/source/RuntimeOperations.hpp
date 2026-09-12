#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace soulmod
{
    struct FontRegistration
    {
        std::string name;
        std::filesystem::path assetPath;
        int size = 0;
        bool bold = false;
        bool italic = false;
        int first = 0;
        int last = 0;
    };

    struct GlobalJsonLoadCondition
    {
        std::string global;
        std::string equals;
    };

    struct GlobalJsonLoad
    {
        std::string operationId;
        std::string targetGlobal;
        std::vector<std::string> mirrorGlobals;
        std::optional<GlobalJsonLoadCondition> condition;
        std::filesystem::path assetPath;
        std::string assetSha256;
        std::string applyAt;
    };

    struct PackageOperations
    {
        std::string packageId;
        std::filesystem::path root;
        std::unordered_map<std::string, std::filesystem::path> fileRedirects;
        std::unordered_map<std::string, FontRegistration> registeredFonts;
        std::unordered_map<std::string, std::string> fontRedirects;
        std::vector<GlobalJsonLoad> globalJsonLoads;
    };

    bool LoadPackageOperations(const std::filesystem::path& packageRoot, PackageOperations& operations, std::string& error);
    std::optional<std::filesystem::path> SelectFileRedirect(const PackageOperations& operations, const std::string& requestedPath);
    std::optional<std::string> SelectFontRedirect(const PackageOperations& operations, std::string_view sourceFontName);
    std::optional<std::filesystem::path> DiscoverPackageRoot(const std::filesystem::path& modulePath, std::string& error);
    bool ReadVerifiedAsset(const GlobalJsonLoad& operation, std::string& bytes, std::string& error);
}
