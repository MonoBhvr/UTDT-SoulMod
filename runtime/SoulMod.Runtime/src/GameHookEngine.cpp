#include "SoulMod/GameHookEngine.hpp"

#include "SoulMod/FontManager.hpp"
#include "SoulMod/Logger.hpp"

#include <cmath>
#include <limits>

namespace SoulMod {

GameHookEngine& GameHookEngine::Instance() { static GameHookEngine engine; return engine; }

void GameHookEngine::ConfigureGlobalJsonLoads(const std::vector<LoadedModPackage>& packages) {
    epochs_.clear();
    for (const LoadedModPackage& package : packages) {
        for (const ModOperation& operation : package.operations) {
            if (operation.operationType != "global.json.load") continue;
            auto binding = std::make_unique<EpochBinding>();
            binding->load = { operation.target.name, operation.mirrorGlobals, std::filesystem::path(package.manifest.packagePath) / operation.assetPath, operation.assetSha256 };
            binding->conditionGlobal = operation.conditionGlobal;
            binding->conditionEquals = operation.conditionEquals;
            epochs_.push_back(std::move(binding));
        }
    }
}

bool GameHookEngine::InstallRequiredHook(const char* name, void* callback, void*& original) {
    PVOID target = nullptr;
    const Aurie::AurieStatus lookup = interface_->GetNamedRoutinePointer(name, &target);
    if (!Aurie::AurieSuccess(lookup) || target == nullptr) return false;
    return Aurie::AurieSuccess(Aurie::MmCreateHook(module_, name, target, callback, &original));
}

bool GameHookEngine::InstallHooks(Aurie::AurieModule& module, YYTK::YYTKInterface& toolkit) {
    if (installed_) return true;
    module_ = &module;
    interface_ = &toolkit;
    if (!BindFontRedirects()) {
        module_ = nullptr; interface_ = nullptr;
        return false;
    }
    if (!InstallRequiredHook("draw_set_font", reinterpret_cast<PVOID>(HookDrawSetFont), originalDrawSetFont_)) {
        module_ = nullptr; interface_ = nullptr;
        return false;
    }
    installed_ = true;
    return true;
}

void GameHookEngine::UninstallHooks() {
    if (!installed_) return;
    Aurie::MmRemoveHook(module_, "draw_set_font");
    originalDrawSetFont_ = nullptr;
    interface_ = nullptr;
    module_ = nullptr;
    installed_ = false;
}

bool GameHookEngine::BindFontRedirects() {
    return FontManager::Instance().BindFontRedirects([this](const std::string& sourceName) -> std::optional<int32_t> {
        const YYTK::RValue sourceIndex = interface_->CallBuiltin("asset_get_index", { YYTK::RValue(sourceName) });
        if (sourceIndex.m_Kind == YYTK::VALUE_UNDEFINED) return std::nullopt;
        const double numericIndex = sourceIndex.ToDouble();
        if (!std::isfinite(numericIndex) || numericIndex < 0 ||
            numericIndex > static_cast<double>(std::numeric_limits<int32_t>::max())) return std::nullopt;
        const int32_t sourceId = static_cast<int32_t>(numericIndex);
        return static_cast<double>(sourceId) == numericIndex && sourceId >= 0 ? std::optional<int32_t>(sourceId) : std::nullopt;
    });
}

void GameHookEngine::ApplyGlobalJsonLoads() {
    for (const std::unique_ptr<EpochBinding>& binding : epochs_) {
        if (!binding->conditionGlobal.empty()) {
            const YYTK::RValue exists = interface_->CallBuiltin("variable_global_exists", { YYTK::RValue(binding->conditionGlobal) });
            if (exists.m_Kind == YYTK::VALUE_UNDEFINED || !exists.ToBoolean()) { binding->epoch.Deactivate(); continue; }
            const YYTK::RValue value = interface_->CallBuiltin("variable_global_get", { YYTK::RValue(binding->conditionGlobal) });
            if (value.m_Kind == YYTK::VALUE_UNDEFINED || value.ToString() != binding->conditionEquals) { binding->epoch.Deactivate(); continue; }
        }
        std::string error;
        const JsonApplyResult result = ApplyVerifiedGlobalJson(binding->epoch, binding->load, *interface_, error);
        if (result == JsonApplyResult::Failed) Logger::Instance().Error("global JSON epoch failed: " + error);
    }
}

void GameHookEngine::HookDrawSetFont(YYTK::RValue* result, YYTK::CInstance* self, YYTK::CInstance* other, const int argc, YYTK::RValue* args) {
    GameHookEngine& engine = Instance();
    engine.ApplyGlobalJsonLoads();
    if (engine.originalDrawSetFont_ == nullptr || argc < 1 || args == nullptr) return;
    Routine original = reinterpret_cast<Routine>(engine.originalDrawSetFont_);
    const auto redirected = FontManager::Instance().GetRedirectedFontName(static_cast<int32_t>(args[0].ToDouble()));
    if (redirected) {
        const int32_t replacementId = FontManager::Instance().EnsureVirtualFontId(*redirected, *engine.interface_);
        if (replacementId >= 0) {
            YYTK::RValue replacement(static_cast<double>(replacementId));
            original(result, self, other, 1, &replacement);
            return;
        }
    }
    original(result, self, other, argc, args);
}

}
