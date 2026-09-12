#include <Aurie/shared.hpp>

#include "SoulMod/FontManager.hpp"
#include "SoulMod/GameHookEngine.hpp"
#include "SoulMod/HookManager.hpp"
#include "SoulMod/Logger.hpp"
#include "SoulMod/ModLoader.hpp"
#include "SoulMod/OperationsResolver.hpp"
#include "SoulMod/YYToolkitAdapter.hpp"

#include <filesystem>

namespace fs = std::filesystem;

EXPORTED Aurie::AurieStatus ModuleInitialize(IN Aurie::AurieModule* module, IN const fs::path& modulePath) {
    if (module == nullptr || !SoulMod::YYToolkitAdapter::Instance().Initialize()) return Aurie::AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    wchar_t executable[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0) return Aurie::AURIE_MODULE_INITIALIZATION_FAILED;
    SoulMod::ModLoader::Instance().SetRuntimeTargetDirectory(fs::path(executable).parent_path());
    const fs::path modsDirectory = modulePath.parent_path() / "mods";
    const std::vector<SoulMod::LoadedModPackage> packages = SoulMod::ModLoader::Instance().DiscoverAndLoadMods(modsDirectory.string());
    const SoulMod::ResolutionReport report = SoulMod::OperationsResolver::Instance().ApplyModPackages(packages);
    if (!report.success) return Aurie::AURIE_MODULE_INITIALIZATION_FAILED;
    SoulMod::GameHookEngine::Instance().ConfigureGlobalJsonLoads(report.orderedPackages);
    YYTK::YYTKInterface* toolkit = SoulMod::YYToolkitAdapter::Instance().GetInterface();
    if (toolkit == nullptr || !SoulMod::GameHookEngine::Instance().InstallHooks(*module, *toolkit)) {
        SoulMod::GameHookEngine::Instance().UninstallHooks();
        SoulMod::HookManager::Instance().RemoveAllHooks();
        SoulMod::FontManager::Instance().Clear();
        return Aurie::AURIE_MODULE_INITIALIZATION_FAILED;
    }
    return Aurie::AURIE_SUCCESS;
}

EXPORTED Aurie::AurieStatus ModuleUnload(IN Aurie::AurieModule*) {
    SoulMod::GameHookEngine::Instance().UninstallHooks();
    SoulMod::HookManager::Instance().RemoveAllHooks();
    SoulMod::FontManager::Instance().Clear();
    SoulMod::YYToolkitAdapter::Instance().Shutdown();
    SoulMod::Logger::Instance().Shutdown();
    return Aurie::AURIE_SUCCESS;
}
