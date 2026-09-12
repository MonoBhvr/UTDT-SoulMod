#include "SoulMod/HookManager.hpp"
#include "SoulMod/Logger.hpp"
#include <algorithm>
#include <sstream>

namespace SoulMod {

HookManager& HookManager::Instance() {
    static HookManager instance;
    return instance;
}

bool HookManager::RegisterBeforeHook(const std::string& functionName, const std::string& modId, int32_t priority, HookCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_beforeHooks.push_back({ functionName, modId, priority, callback });
    
    std::stringstream ss;
    ss << "Registered 'before' hook on [" << functionName << "] for Mod [" << modId << "] (Priority: " << priority << ")";
    Logger::Instance().Info(ss.str());
    return true;
}

bool HookManager::RegisterAfterHook(const std::string& functionName, const std::string& modId, int32_t priority, HookCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_afterHooks.push_back({ functionName, modId, priority, callback });
    
    std::stringstream ss;
    ss << "Registered 'after' hook on [" << functionName << "] for Mod [" << modId << "] (Priority: " << priority << ")";
    Logger::Instance().Info(ss.str());
    return true;
}

bool HookManager::RegisterReplaceHook(const std::string& functionName, const std::string& modId, int32_t priority, HookCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check if replace hook already exists for this function
    for (const auto& entry : m_replaceHooks) {
        if (entry.functionName == functionName) {
            std::stringstream ss;
            ss << "CONFLICT: Function [" << functionName << "] already has a replace hook registered by Mod [" 
               << entry.modId << "]. Mod [" << modId << "] cannot overwrite it!";
            Logger::Instance().Error(ss.str());
            return false;
        }
    }

    m_replaceHooks.push_back({ functionName, modId, priority, callback });
    
    std::stringstream ss;
    ss << "Registered 'replace' hook on [" << functionName << "] for Mod [" << modId << "]";
    Logger::Instance().Info(ss.str());
    return true;
}

size_t HookManager::GetActiveHookCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_beforeHooks.size() + m_afterHooks.size() + m_replaceHooks.size();
}

void HookManager::RemoveAllHooks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_beforeHooks.clear();
    m_afterHooks.clear();
    m_replaceHooks.clear();
    Logger::Instance().Info("All runtime hooks safely uninstalled and removed.");
}

} // namespace SoulMod
