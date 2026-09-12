#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <YYToolkit/YYTK_Shared.hpp>

#include "RuntimeOperations.hpp"
#include "GlobalJsonLoad.hpp"
#include "RestartCoordinator.hpp"

using namespace Aurie;
using namespace YYTK;

namespace fs = std::filesystem;

namespace
{
    YYTKInterface* g_ModuleInterface = nullptr;
    PVOID g_FileTextOpenReadOriginal = nullptr;
    PVOID g_DrawSetFontOriginal = nullptr;
    soulmod::PackageOperations g_Operations;
    std::unordered_map<std::string, double> g_RegisteredFontIds;
    struct ResolvedFontRedirect
    {
        std::string sourceName;
        std::string targetName;
        bool activationLogged = false;
    };
    std::unordered_map<int, ResolvedFontRedirect> g_ResolvedFontRedirects;
    std::vector<std::unique_ptr<soulmod::GlobalJsonLoadEpoch>> g_GlobalJsonLoadEpochs;
    std::vector<bool> g_GlobalJsonLoadWaitingLogged;
    soulmod::RestartCoordinator g_RestartCoordinator;
    thread_local bool g_InFileHook = false;
    thread_local bool g_InFontHook = false;
    using TRoutine = void(*)(RValue* result, CInstance* self, CInstance* other, int argumentCount, RValue* arguments);

    class ReentrancyScope
    {
    public:
        explicit ReentrancyScope(bool& flag) : flag_(flag) { flag_ = true; }
        ~ReentrancyScope() { flag_ = false; }
        ReentrancyScope(const ReentrancyScope&) = delete;
        ReentrancyScope& operator=(const ReentrancyScope&) = delete;

    private:
        bool& flag_;
    };

    void LogPlugin(const char* format, ...)
    {
        va_list arguments;
        va_start(arguments, format);
        FILE* output = nullptr;
        fopen_s(&output, "C:\\Users\\Public\\SoulModPlugin.log", "a");
        if (output)
        {
            vfprintf(output, format, arguments);
            fputc('\n', output);
            fclose(output);
        }
        va_end(arguments);
    }

    void CallOriginal(PVOID original, RValue* result, CInstance* self, CInstance* other, int argumentCount, RValue* arguments)
    {
        if (original)
            reinterpret_cast<TRoutine>(original)(result, self, other, argumentCount, arguments);
    }

    const char* GlobalJsonLoadStateName(soulmod::GlobalJsonLoadState state)
    {
        switch (state)
        {
        case soulmod::GlobalJsonLoadState::Inactive: return "Inactive";
        case soulmod::GlobalJsonLoadState::Pending: return "Pending";
        case soulmod::GlobalJsonLoadState::Applying: return "Applying";
        case soulmod::GlobalJsonLoadState::Applied: return "Applied";
        case soulmod::GlobalJsonLoadState::Failed: return "Failed";
        }
        return "Unknown";
    }

    class YYToolkitGlobalJsonLoadHost final : public soulmod::GlobalJsonLoadHost
    {
    public:
        soulmod::GlobalJsonLoadConditionReadiness ReadConditionGlobal(std::string_view name, std::string& value, std::string& error) override
        {
            const std::string globalName(name);
            const RValue exists = g_ModuleInterface->CallBuiltin("variable_global_exists", { RValue(globalName) });
            if (exists.m_Kind == VALUE_UNDEFINED)
            {
                error = "condition global existence check returned undefined";
                return soulmod::GlobalJsonLoadConditionReadiness::Failure;
            }
            if (!exists.ToBoolean()) return soulmod::GlobalJsonLoadConditionReadiness::Missing;
            const RValue condition = g_ModuleInterface->CallBuiltin("variable_global_get", { RValue(globalName) });
            if (condition.m_Kind == VALUE_UNDEFINED)
            {
                error = "condition global value returned undefined";
                return soulmod::GlobalJsonLoadConditionReadiness::Failure;
            }
            if (condition.m_Kind != VALUE_STRING)
            {
                error = "condition global value is not a string";
                return soulmod::GlobalJsonLoadConditionReadiness::Failure;
            }
            value = condition.ToString();
            return soulmod::GlobalJsonLoadConditionReadiness::String;
        }

        soulmod::GlobalJsonLoadReadiness CheckGlobalReady(std::string_view name, std::string& error) override
        {
            const RValue exists = g_ModuleInterface->CallBuiltin("variable_global_exists", { RValue(std::string(name)) });
            if (exists.m_Kind == VALUE_UNDEFINED)
            {
                error = "variable_global_exists returned undefined";
                return soulmod::GlobalJsonLoadReadiness::Failure;
            }
            return exists.ToBoolean() ? soulmod::GlobalJsonLoadReadiness::Ready : soulmod::GlobalJsonLoadReadiness::NotReady;
        }

        bool ReadVerifiedAsset(const soulmod::GlobalJsonLoad& operation, std::string& bytes, std::string& error) override
        {
            return soulmod::ReadVerifiedAsset(operation, bytes, error);
        }

        bool DecodeJson(std::string_view bytes, std::string& error) override
        {
            decodedValue_ = g_ModuleInterface->CallBuiltin("json_decode", { RValue(std::string(bytes)) });
            if (decodedValue_.m_Kind == VALUE_UNDEFINED)
            {
                error = "json_decode returned undefined";
                return false;
            }
            return true;
        }

        bool SnapshotGlobal(std::string_view name, std::string& error) override
        {
            const std::string globalName(name);
            if (!g_ModuleInterface->CallBuiltin("variable_global_exists", { RValue(globalName) }).ToBoolean())
            {
                error = "global does not exist";
                return false;
            }
            snapshots_.emplace(globalName, g_ModuleInterface->CallBuiltin("variable_global_get", { RValue(globalName) }));
            return true;
        }

        bool SetDecodedGlobal(std::string_view name, std::string& error) override
        {
            const std::string globalName(name);
            g_ModuleInterface->CallBuiltin("variable_global_set", { RValue(globalName), decodedValue_ });
            const RValue storedValue = g_ModuleInterface->CallBuiltin("variable_global_get", { RValue(globalName) });
            if (!MatchesValue(decodedValue_, storedValue))
            {
                error = "global JSON write verification mismatch";
                LogPlugin("[OP_RUNTIME] global-json-load stage=write-verify-failed name=%s", globalName.c_str());
                return false;
            }
            LogPlugin("[OP_RUNTIME] global-json-load stage=write-verified name=%s", globalName.c_str());
            return true;
        }

        bool RestoreSnapshot(std::string_view name, std::string& error) override
        {
            const auto snapshot = snapshots_.find(std::string(name));
            if (snapshot == snapshots_.end())
            {
                error = "snapshot missing";
                return false;
            }
            g_ModuleInterface->CallBuiltin("variable_global_set", { RValue(snapshot->first), snapshot->second });
            const RValue restoredValue = g_ModuleInterface->CallBuiltin("variable_global_get", { RValue(snapshot->first) });
            if (!MatchesValue(snapshot->second, restoredValue))
            {
                error = "global JSON rollback verification mismatch";
                LogPlugin("[OP_RUNTIME] global-json-load stage=rollback-verify-failed name=%s", snapshot->first.c_str());
                return false;
            }
            LogPlugin("[OP_RUNTIME] global-json-load stage=rollback-verified name=%s", snapshot->first.c_str());
            return true;
        }

        void Clear() override
        {
            snapshots_.clear();
            decodedValue_ = RValue();
        }

    private:
        static bool MatchesValue(const RValue& expected, const RValue& actual)
        {
            return expected.m_Kind == actual.m_Kind && expected.ToDouble() == actual.ToDouble();
        }

        RValue decodedValue_;
        std::map<std::string, RValue> snapshots_;
    };

    void ApplyGlobalJsonLoads()
    {
        for (size_t index = 0; index < g_Operations.globalJsonLoads.size(); ++index)
        {
            const soulmod::GlobalJsonLoad& operation = g_Operations.globalJsonLoads[index];
            soulmod::GlobalJsonLoadEpoch& epoch = *g_GlobalJsonLoadEpochs[index];

            YYToolkitGlobalJsonLoadHost host;
            std::string error;
            const soulmod::GlobalJsonLoadApplyOutcome outcome = soulmod::ApplyGlobalJsonLoad(operation, host, epoch, error);
            if (outcome.deactivated)
                LogPlugin("[OP_RUNTIME] global-json-load deactivated id=%s", operation.operationId.c_str());
            if (outcome.epochStarted)
            {
                g_GlobalJsonLoadWaitingLogged[index] = false;
                LogPlugin("[OP_RUNTIME] global-json-load epoch-start id=%s", operation.operationId.c_str());
            }
            if (outcome.result == soulmod::GlobalJsonLoadApplyResult::NotReady)
            {
                if (!g_GlobalJsonLoadWaitingLogged[index])
                {
                    LogPlugin("[OP_RUNTIME] global-json-load waiting id=%s state=%s", operation.operationId.c_str(), GlobalJsonLoadStateName(epoch.State()));
                    g_GlobalJsonLoadWaitingLogged[index] = true;
                }
            }
            else if (outcome.result == soulmod::GlobalJsonLoadApplyResult::Applied)
                LogPlugin("[OP_RUNTIME] global-json-load applied id=%s state=%s", operation.operationId.c_str(), GlobalJsonLoadStateName(epoch.State()));
            else if (outcome.failureStarted)
                LogPlugin("[OP_RUNTIME] global-json-load failed id=%s state=%s reason=%s", operation.operationId.c_str(), GlobalJsonLoadStateName(epoch.State()), error.c_str());
        }
    }

    void RequestRestartIfReady()
    {
        std::vector<soulmod::GlobalJsonLoadState> globalJsonLoadStates;
        globalJsonLoadStates.reserve(g_GlobalJsonLoadEpochs.size());
        for (const auto& epoch : g_GlobalJsonLoadEpochs)
            globalJsonLoadStates.push_back(epoch->State());

        if (!g_RestartCoordinator.TryClaimRestart(soulmod::AreSelectedGlobalJsonLoadsApplied(globalJsonLoadStates)))
            return;

        LogPlugin("[OP_RUNTIME] restart requested builtin=game_restart reason=global-json-and-font-ready");
        g_ModuleInterface->CallBuiltin("game_restart", {});
    }

    bool EnsureRegisteredFont(const std::string& fontName, double& fontId)
    {
        const auto existing = g_RegisteredFontIds.find(fontName);
        if (existing != g_RegisteredFontIds.end())
        {
            fontId = existing->second;
            return true;
        }

        const auto registration = g_Operations.registeredFonts.find(fontName);
        if (registration == g_Operations.registeredFonts.end())
        {
            LogPlugin("[OP_RUNTIME] font_add skipped name=%s reason=registration-not-found", fontName.c_str());
            return false;
        }

        const soulmod::FontRegistration& font = registration->second;
        const std::string assetPath = font.assetPath.string();
        LogPlugin("[OP_RUNTIME] font_add request name=%s path=%s size=%d first=%d last=%d", font.name.c_str(), assetPath.c_str(), font.size, font.first, font.last);
        const RValue fontResult = g_ModuleInterface->CallBuiltin("font_add", {
            RValue(assetPath), RValue(static_cast<double>(font.size)), RValue(font.bold), RValue(font.italic),
            RValue(static_cast<double>(font.first)), RValue(static_cast<double>(font.last))
        });
        const double candidateId = fontResult.ToDouble();
        if (candidateId < 0)
        {
            LogPlugin("[OP_RUNTIME] font_add failed name=%s returned=%.0f", font.name.c_str(), candidateId);
            return false;
        }

        const RValue existsResult = g_ModuleInterface->CallBuiltin("font_exists", { RValue(candidateId) });
        const bool exists = existsResult.ToBoolean();
        LogPlugin("[OP_RUNTIME] font_exists id=%.0f result=%s", candidateId, exists ? "true" : "false");
        if (!exists)
            return false;

        g_RegisteredFontIds.emplace(font.name, candidateId);
        fontId = candidateId;
        return true;
    }

    bool ResolveFontRedirects()
    {
        std::unordered_map<int, ResolvedFontRedirect> resolved;
        for (const auto& [sourceName, targetName] : g_Operations.fontRedirects)
        {
            const RValue sourceIndex = g_ModuleInterface->CallBuiltin("asset_get_index", { RValue(sourceName) });
            if (sourceIndex.m_Kind != VALUE_REAL && sourceIndex.m_Kind != VALUE_INT32 && sourceIndex.m_Kind != VALUE_INT64)
            {
                LogPlugin("[OP_RUNTIME] font binding failed resource=%s reason=non-numeric-index", sourceName.c_str());
                return false;
            }

            const double numericIndex = sourceIndex.ToDouble();
            if (!std::isfinite(numericIndex) || std::trunc(numericIndex) != numericIndex || numericIndex < 0 || numericIndex > static_cast<double>((std::numeric_limits<int>::max)()))
            {
                LogPlugin("[OP_RUNTIME] font binding failed resource=%s reason=invalid-index value=%.17g", sourceName.c_str(), numericIndex);
                return false;
            }

            const int sourceId = static_cast<int>(numericIndex);
            if (!resolved.emplace(sourceId, ResolvedFontRedirect{ sourceName, targetName }).second)
            {
                LogPlugin("[OP_RUNTIME] font binding failed resource=%s reason=source-id-collision index=%d", sourceName.c_str(), sourceId);
                return false;
            }
        }

        g_ResolvedFontRedirects = std::move(resolved);
        for (const auto& [sourceId, binding] : g_ResolvedFontRedirects)
            LogPlugin("[OP_RUNTIME] font binding resource=%s source=%d target=%s", binding.sourceName.c_str(), sourceId, binding.targetName.c_str());
        return true;
    }

    void HK_file_text_open_read(RValue* result, CInstance* self, CInstance* other, int argumentCount, RValue* arguments)
    {
        if (g_InFileHook)
        {
            CallOriginal(g_FileTextOpenReadOriginal, result, self, other, argumentCount, arguments);
            return;
        }

        ReentrancyScope scope(g_InFileHook);
        if (argumentCount >= 1 && arguments)
        {
            const std::string requestedPath = arguments[0].ToString();
            LogPlugin("[OP_RUNTIME] file request path=%s", requestedPath.c_str());
            const auto redirect = soulmod::SelectFileRedirect(g_Operations, requestedPath);
            if (redirect)
            {
                const std::string replacementPath = redirect->string();
                LogPlugin("[OP_RUNTIME] file redirect request=%s asset=%s", requestedPath.c_str(), replacementPath.c_str());
                RValue replacement(replacementPath);
                std::vector<RValue> redirectedArguments(arguments, arguments + argumentCount);
                redirectedArguments[0] = replacement;
                CallOriginal(g_FileTextOpenReadOriginal, result, self, other, argumentCount, redirectedArguments.data());
                LogPlugin("[OP_RUNTIME] file open asset=%s", replacementPath.c_str());
                return;
            }
        }
        CallOriginal(g_FileTextOpenReadOriginal, result, self, other, argumentCount, arguments);
    }

    void HK_draw_set_font(RValue* result, CInstance* self, CInstance* other, int argumentCount, RValue* arguments)
    {
        if (g_InFontHook)
        {
            CallOriginal(g_DrawSetFontOriginal, result, self, other, argumentCount, arguments);
            return;
        }

        ReentrancyScope scope(g_InFontHook);
        ApplyGlobalJsonLoads();
        if (argumentCount >= 1 && arguments)
        {
            const int sourceFontId = static_cast<int>(arguments[0].ToDouble());
            const auto binding = g_ResolvedFontRedirects.find(sourceFontId);
            if (binding != g_ResolvedFontRedirects.end())
            {
                double replacementFontId = -1;
                if (EnsureRegisteredFont(binding->second.targetName, replacementFontId))
                {
                    if (!binding->second.activationLogged)
                    {
                        LogPlugin("[OP_RUNTIME] font redirect activated resource=%s source=%d target=%s registered=%.0f", binding->second.sourceName.c_str(), sourceFontId, binding->second.targetName.c_str(), replacementFontId);
                        binding->second.activationLogged = true;
                    }
                    RValue replacement(replacementFontId);
                    CallOriginal(g_DrawSetFontOriginal, result, self, other, 1, &replacement);
                    g_RestartCoordinator.MarkFontReady();
                    RequestRestartIfReady();
                    return;
                }
            }
        }
        CallOriginal(g_DrawSetFontOriginal, result, self, other, argumentCount, arguments);
    }

    AurieStatus InstallHook(AurieModule* module, const char* routineName, PVOID hook, PVOID* original)
    {
        PVOID target = nullptr;
        const AurieStatus lookup = g_ModuleInterface->GetNamedRoutinePointer(routineName, &target);
        if (!AurieSuccess(lookup) || !target)
        {
            LogPlugin("[OP_RUNTIME] hook lookup failed routine=%s status=%s", routineName, AurieStatusToString(lookup));
            return lookup;
        }
        const AurieStatus installed = MmCreateHook(module, routineName, target, hook, original);
        LogPlugin("[OP_RUNTIME] hook install routine=%s status=%s", routineName, AurieStatusToString(installed));
        return installed;
    }
}

EXPORTED AurieStatus ModuleInitialize(IN AurieModule* module, IN const fs::path& modulePath)
{
    LogPlugin("==================================================");
    LogPlugin("SoulModPlugin operation-driven runtime proof v0.1.0");
    LogPlugin("==================================================");
    LogPlugin("[OP_RUNTIME] lifecycle module=%S", modulePath.c_str());

    std::string error;
    const auto packageRoot = soulmod::DiscoverPackageRoot(modulePath, error);
    if (!packageRoot || !soulmod::LoadPackageOperations(*packageRoot, g_Operations, error))
    {
        LogPlugin("[OP_RUNTIME] package load failed reason=%s", error.c_str());
        return AURIE_MODULE_INITIALIZATION_FAILED;
    }
    g_GlobalJsonLoadEpochs.clear();
    g_GlobalJsonLoadWaitingLogged.clear();
    for (size_t index = 0; index < g_Operations.globalJsonLoads.size(); ++index)
        g_GlobalJsonLoadEpochs.push_back(std::make_unique<soulmod::GlobalJsonLoadEpoch>(g_Operations.globalJsonLoads[index]));
    g_GlobalJsonLoadWaitingLogged.resize(g_Operations.globalJsonLoads.size(), false);
    LogPlugin("[OP_RUNTIME] package loaded id=%s root=%s files=%zu fonts=%zu redirects=%zu global-json-loads=%zu", g_Operations.packageId.c_str(), packageRoot->string().c_str(), g_Operations.fileRedirects.size(), g_Operations.registeredFonts.size(), g_Operations.fontRedirects.size(), g_Operations.globalJsonLoads.size());

    const AurieStatus interfaceStatus = ObGetInterface("YYTK_Main", reinterpret_cast<AurieInterfaceBase*&>(g_ModuleInterface));
    if (!AurieSuccess(interfaceStatus) || !g_ModuleInterface)
    {
        LogPlugin("[OP_RUNTIME] YYTK_Main unavailable status=%s", AurieStatusToString(interfaceStatus));
        return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    }

    if (!ResolveFontRedirects())
        return AURIE_MODULE_INITIALIZATION_FAILED;

    AurieStatus fileHook = AURIE_SUCCESS;
    if (!g_Operations.fileRedirects.empty())
        fileHook = InstallHook(module, "file_text_open_read", reinterpret_cast<PVOID>(HK_file_text_open_read), &g_FileTextOpenReadOriginal);
    const AurieStatus fontHook = InstallHook(module, "draw_set_font", reinterpret_cast<PVOID>(HK_draw_set_font), &g_DrawSetFontOriginal);
    if (!AurieSuccess(fileHook) || !AurieSuccess(fontHook))
        return AURIE_MODULE_INITIALIZATION_FAILED;

    LogPlugin("[OP_RUNTIME] lifecycle ready");
    return AURIE_SUCCESS;
}

EXPORTED AurieStatus ModuleUnload(IN AurieModule* module)
{
    UNREFERENCED_PARAMETER(module);
    for (size_t index = 0; index < g_GlobalJsonLoadEpochs.size(); ++index)
        LogPlugin("[OP_RUNTIME] global-json-load unload id=%s state=%s", g_Operations.globalJsonLoads[index].operationId.c_str(), GlobalJsonLoadStateName(g_GlobalJsonLoadEpochs[index]->State()));
    g_GlobalJsonLoadEpochs.clear();
    g_GlobalJsonLoadWaitingLogged.clear();
    g_RegisteredFontIds.clear();
    g_ResolvedFontRedirects.clear();
    g_Operations = {};
    LogPlugin("[OP_RUNTIME] lifecycle unload bookkeeping-cleared");
    return AURIE_SUCCESS;
}
