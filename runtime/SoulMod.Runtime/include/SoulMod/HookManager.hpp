#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <cstdint>

namespace SoulMod {

using HookCallback = std::function<void(void** args)>;

struct HookEntry {
    std::string functionName;
    std::string modId;
    int32_t priority;
    HookCallback callback;
};

class HookManager {
public:
    static HookManager& Instance();

    bool RegisterBeforeHook(const std::string& functionName, const std::string& modId, int32_t priority, HookCallback callback);
    bool RegisterAfterHook(const std::string& functionName, const std::string& modId, int32_t priority, HookCallback callback);
    bool RegisterReplaceHook(const std::string& functionName, const std::string& modId, int32_t priority, HookCallback callback);

    size_t GetActiveHookCount() const;
    void RemoveAllHooks();

private:
    HookManager() = default;
    ~HookManager() = default;

    std::vector<HookEntry> m_beforeHooks;
    std::vector<HookEntry> m_afterHooks;
    std::vector<HookEntry> m_replaceHooks;
    mutable std::mutex m_mutex;
};

} // namespace SoulMod
