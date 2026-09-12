#pragma once

#include "SoulMod/ModPackage.hpp"
#include <string>
#include <vector>
#include <filesystem>

namespace SoulMod {

class ModLoader {
public:
    static ModLoader& Instance();

    std::vector<LoadedModPackage> DiscoverAndLoadMods(const std::string& modsDirectory);
    bool LoadSinglePackageFromDir(const std::string& modDirPath, LoadedModPackage& outPackage);
    void SetRuntimeTargetDirectory(std::filesystem::path directory);

private:
    ModLoader() = default;
    ~ModLoader() = default;

    bool ParseManifestFile(const std::string& manifestPath, ModManifest& outManifest);
    bool ParseOperationsFile(const std::filesystem::path& packageRoot, const std::string& operationsPath, const std::string& modId, std::vector<ModOperation>& outOps);
    std::filesystem::path runtimeTargetDirectory_;
};

} // namespace SoulMod
