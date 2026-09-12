#include "SoulMod/StringManager.hpp"
#include "SoulMod/Logger.hpp"
#include <sstream>

namespace SoulMod {

StringManager& StringManager::Instance() {
    static StringManager instance;
    return instance;
}

void StringManager::RegisterStringReplacement(const std::string& originalText, const std::string& replacementText, const std::string& modId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_stringMap.find(originalText);
    if (it != m_stringMap.end()) {
        std::stringstream ss;
        ss << "String replacement collision: [" << originalText << "] already registered by Mod [" 
           << it->second.modId << "]. Overwriting with Mod [" << modId << "].";
        Logger::Instance().Warn(ss.str());
    }

    m_stringMap[originalText] = { replacementText, modId };
    
    std::stringstream ss;
    ss << "Registered string replacement: \"" << originalText << "\" -> \"" << replacementText << "\" (Mod: " << modId << ")";
    Logger::Instance().Debug(ss.str());
}

std::optional<std::string> StringManager::GetReplacement(const std::string& originalText) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_stringMap.find(originalText);
    if (it != m_stringMap.end()) {
        return it->second.replacementText;
    }
    return std::nullopt;
}

size_t StringManager::GetReplacementCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stringMap.size();
}

void StringManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stringMap.clear();
}

} // namespace SoulMod
