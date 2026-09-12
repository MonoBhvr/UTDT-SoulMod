#include "SoulMod/FontManager.hpp"
#include "SoulMod/Logger.hpp"
#include <sstream>
#include <YYToolkit/YYTK_Shared.hpp>

namespace SoulMod {

FontManager& FontManager::Instance() {
    static FontManager instance;
    return instance;
}

void FontManager::RegisterFontRedirect(const std::string& targetFontName, const std::string& replacementFontName, const std::string& modId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fontRedirects[targetFontName] = replacementFontName;

    std::stringstream ss;
    ss << "Registered font redirect: [" << targetFontName << "] -> [" << replacementFontName << "] (Mod: " << modId << ")";
    Logger::Instance().Info(ss.str());
}

void FontManager::RegisterVirtualFont(const std::string& fontName, const std::string& assetPath, const std::string& modId, const int size, const bool bold, const bool italic, const int first, const int last) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_virtualFonts[fontName] = { fontName, assetPath, -1, modId, size, bold, italic, first, last };

    std::stringstream ss;
    ss << "Registered virtual font: [" << fontName << "] (Asset: " << assetPath << ", Mod: " << modId << ")";
    Logger::Instance().Info(ss.str());
}

bool FontManager::BindFontRedirects(const std::function<std::optional<int32_t>(const std::string&)>& resolveFontIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::unordered_map<int32_t, std::string> resolved;
    for (const auto& [sourceName, replacementName] : m_fontRedirects) {
        const std::optional<int32_t> sourceId = resolveFontIndex(sourceName);
        if (!sourceId || !resolved.emplace(*sourceId, replacementName).second) return false;
    }
    m_resolvedFontRedirects = std::move(resolved);
    return true;
}

std::optional<std::string> FontManager::GetRedirectedFontName(const int32_t originalFontId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_resolvedFontRedirects.find(originalFontId);
    return it == m_resolvedFontRedirects.end() ? std::nullopt : std::optional<std::string>(it->second);
}

int32_t FontManager::GetVirtualFontId(const std::string& fontName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_virtualFonts.find(fontName);
    if (it != m_virtualFonts.end()) {
        return it->second.assignedFontId;
    }
    return -1;
}

int32_t FontManager::EnsureVirtualFontId(const std::string& fontName, YYTK::YYTKInterface& toolkit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_virtualFonts.find(fontName);
    if (found == m_virtualFonts.end()) return -1;
    if (found->second.assignedFontId >= 0) return found->second.assignedFontId;
    const YYTK::RValue result = toolkit.CallBuiltin("font_add", {
        YYTK::RValue(found->second.fontAssetPath), YYTK::RValue(static_cast<double>(found->second.size)), YYTK::RValue(found->second.bold),
        YYTK::RValue(found->second.italic), YYTK::RValue(static_cast<double>(found->second.first)), YYTK::RValue(static_cast<double>(found->second.last))
    });
    const double candidate = result.ToDouble();
    if (result.m_Kind == YYTK::VALUE_UNDEFINED || candidate < 0) return -1;
    const YYTK::RValue exists = toolkit.CallBuiltin("font_exists", { YYTK::RValue(candidate) });
    if (exists.m_Kind == YYTK::VALUE_UNDEFINED || !exists.ToBoolean()) return -1;
    found->second.assignedFontId = static_cast<int32_t>(candidate);
    return found->second.assignedFontId;
}

size_t FontManager::GetFontCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_fontRedirects.size() + m_virtualFonts.size();
}

void FontManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fontRedirects.clear();
    m_resolvedFontRedirects.clear();
    m_virtualFonts.clear();
    m_nextVirtualFontId = 1000;
}

} // namespace SoulMod
