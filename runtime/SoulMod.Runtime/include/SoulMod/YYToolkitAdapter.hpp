#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <filesystem>

namespace YYTK { struct YYTKInterface; }

namespace SoulMod {

struct ProcessInfo {
    std::string processName;
    std::string processPath;
    uintptr_t baseAddress = 0;
    size_t moduleSize = 0;
    bool is64Bit = true;
};

enum class YYToolkitBindingStatus {
    NotAttempted,
    AurieDispatcherUnavailable,
    YYToolkitInterfaceUnavailable,
    Bound,
};

class YYToolkitAdapter {
public:
    static YYToolkitAdapter& Instance();

    bool Initialize();
    void Shutdown();

    const ProcessInfo& GetProcessInfo() const { return m_processInfo; }
    bool IsYYToolkitBound() const { return m_yytoolkitBound; }
    YYToolkitBindingStatus GetBindingStatus() const { return m_bindingStatus; }
    YYTK::YYTKInterface* GetInterface() const;
    std::string GetRunnerIdentification() const;

    uintptr_t FindExportOrPattern(const std::string& symbolName);
    void RegisterFileRedirect(std::string requestedPath, std::filesystem::path assetPath);
    void ClearFileRedirects();
    std::filesystem::path FindFileRedirect(const std::wstring& requestedPath) const;

private:
    YYToolkitAdapter() = default;
    ~YYToolkitAdapter() = default;

    ProcessInfo m_processInfo;
    bool m_yytoolkitBound = false;
    bool m_initialized = false;
    YYToolkitBindingStatus m_bindingStatus = YYToolkitBindingStatus::NotAttempted;
    void* m_yytoolkitInterface = nullptr;
    std::unordered_map<std::string, uintptr_t> m_resolvedSymbols;
    std::unordered_map<std::string, std::filesystem::path> m_fileRedirects;

    void DetectProcessInfo();
    bool BindYYToolkit();
    void ScanBuiltinFunctionSymbols();
};

} // namespace SoulMod
