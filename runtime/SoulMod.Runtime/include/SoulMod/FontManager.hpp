#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <functional>
#include <optional>

namespace YYTK { struct YYTKInterface; }

namespace SoulMod {

struct FontRegistration {
    std::string fontName;
    std::string fontAssetPath;
    int32_t assignedFontId = -1;
    std::string modId;
    int size = 10;
    bool bold = false;
    bool italic = false;
    int first = 32;
    int last = 55203;
};

class FontManager {
public:
    static FontManager& Instance();

    void RegisterFontRedirect(const std::string& targetFontName, const std::string& replacementFontName, const std::string& modId);
    void RegisterVirtualFont(const std::string& fontName, const std::string& assetPath, const std::string& modId, int size, bool bold, bool italic, int first, int last);

    bool BindFontRedirects(const std::function<std::optional<int32_t>(const std::string&)>& resolveFontIndex);
    std::optional<std::string> GetRedirectedFontName(int32_t originalFontId) const;
    int32_t GetVirtualFontId(const std::string& fontName) const;
    int32_t EnsureVirtualFontId(const std::string& fontName, YYTK::YYTKInterface& toolkit);

    size_t GetFontCount() const;
    void Clear();

private:
    FontManager() = default;
    ~FontManager() = default;

    std::unordered_map<std::string, std::string> m_fontRedirects; // original -> target
    std::unordered_map<int32_t, std::string> m_resolvedFontRedirects;
    std::unordered_map<std::string, FontRegistration> m_virtualFonts; // fontName -> metadata
    mutable std::mutex m_mutex;
    int32_t m_nextVirtualFontId = 1000;
};

} // namespace SoulMod
