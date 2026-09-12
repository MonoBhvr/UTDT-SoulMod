#pragma once

#include <Aurie/shared.hpp>
#include <YYToolkit/YYTK_Shared.hpp>

#include <string>
#include <vector>
#include <memory>

#include "SoulMod/GlobalJsonEpoch.hpp"
#include "SoulMod/ModPackage.hpp"

namespace SoulMod {

class GameHookEngine {
public:
    static GameHookEngine& Instance();
    void ConfigureGlobalJsonLoads(const std::vector<LoadedModPackage>& packages);
    bool InstallHooks(Aurie::AurieModule& module, YYTK::YYTKInterface& toolkit);
    void UninstallHooks();

private:
    using Routine = void(*)(YYTK::RValue*, YYTK::CInstance*, YYTK::CInstance*, int, YYTK::RValue*);
    struct EpochBinding { JsonGlobalLoad load; std::string conditionGlobal; std::string conditionEquals; JsonEpoch epoch; };

    static void HookDrawSetFont(YYTK::RValue* result, YYTK::CInstance* self, YYTK::CInstance* other, int argc, YYTK::RValue* args);
    bool InstallRequiredHook(const char* name, void* callback, void*& original);
    bool BindFontRedirects();
    void ApplyGlobalJsonLoads();

    Aurie::AurieModule* module_ = nullptr;
    YYTK::YYTKInterface* interface_ = nullptr;
    void* originalDrawSetFont_ = nullptr;
    std::vector<std::unique_ptr<EpochBinding>> epochs_;
    bool installed_ = false;
};

}
