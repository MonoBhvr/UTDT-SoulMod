#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace SoulMod {

class StringManager {
public:
    static StringManager& Instance();

    void RegisterStringReplacement(const std::string& originalText, const std::string& replacementText, const std::string& modId);
    std::optional<std::string> GetReplacement(const std::string& originalText) const;
    
    size_t GetReplacementCount() const;
    void Clear();

private:
    StringManager() = default;
    ~StringManager() = default;

    struct StringEntry {
        std::string replacementText;
        std::string modId;
    };

    std::unordered_map<std::string, StringEntry> m_stringMap;
    mutable std::mutex m_mutex;
};

} // namespace SoulMod
