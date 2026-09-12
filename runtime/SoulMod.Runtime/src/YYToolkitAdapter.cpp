#include "SoulMod/YYToolkitAdapter.hpp"
#include "SoulMod/Logger.hpp"
#include "SoulMod/StringManager.hpp"
#include "SoulMod/FontManager.hpp"
#include <filesystem>
#include <mutex>
#include <vector>

#include <Aurie/shared.hpp>
#include <YYToolkit/YYTK_Shared.hpp>

#include <windows.h>
#include <psapi.h>
#include <sstream>
#include <string>

namespace SoulMod {

// ─── CreateFileW hook (VFS language file redirection) ─────────────────────────

typedef HANDLE(WINAPI* CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileW_t g_OriginalCreateFileW = NULL;
static std::mutex g_redirectMutex;
struct IatPatch final { PROC* slot; PROC original; };
static std::vector<IatPatch> g_iatPatches;

static HANDLE WINAPI HookedCreateFileW(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
) {
    if (lpFileName != NULL) {
        std::wstring fileName(lpFileName);
        const std::filesystem::path asset = YYToolkitAdapter::Instance().FindFileRedirect(fileName);
        if (!asset.empty()) return g_OriginalCreateFileW(asset.c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }

    return g_OriginalCreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
        lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

void YYToolkitAdapter::RegisterFileRedirect(std::string requestedPath, std::filesystem::path assetPath) { std::lock_guard<std::mutex> lock(g_redirectMutex); m_fileRedirects.emplace(std::move(requestedPath), std::move(assetPath)); }
void YYToolkitAdapter::ClearFileRedirects() { std::lock_guard<std::mutex> lock(g_redirectMutex); m_fileRedirects.clear(); }
std::filesystem::path YYToolkitAdapter::FindFileRedirect(const std::wstring& requestedPath) const { std::lock_guard<std::mutex> lock(g_redirectMutex); for (const auto& [requested, asset] : m_fileRedirects) if (requestedPath.ends_with(std::filesystem::path(requested).wstring())) return asset; return {}; }

// ─── IAT patcher ──────────────────────────────────────────────────────────────

static void PatchIAT(HMODULE hExe, const char* targetDll, const char* funcName, void* hookFn, void** originalFn) {
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hExe;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hExe + dosHeader->e_lfanew);
    IMAGE_DATA_DIRECTORY importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.Size == 0) return;

    PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hExe + importDir.VirtualAddress);
    for (; importDesc->Name; importDesc++) {
        const char* modName = (const char*)((BYTE*)hExe + importDesc->Name);
        if (_stricmp(modName, targetDll) != 0) continue;

        PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((BYTE*)hExe + importDesc->FirstThunk);
        for (; thunk->u1.Function; thunk++) {
            PROC* pfn = (PROC*)&thunk->u1.Function;
            HMODULE hTargetDll = GetModuleHandleA(targetDll);
            if (!hTargetDll) continue;
            if ((uintptr_t)*pfn == (uintptr_t)GetProcAddress(hTargetDll, funcName)) {
                DWORD oldProtect = 0;
                if (!VirtualProtect(pfn, sizeof(PROC), PAGE_EXECUTE_READWRITE, &oldProtect)) return;
                *originalFn = (void*)*pfn;
                const PROC original = *pfn;
                *pfn = (PROC)hookFn;
                DWORD ignored = 0;
                if (!VirtualProtect(pfn, sizeof(PROC), oldProtect, &ignored)) { *pfn = original; return; }
                g_iatPatches.push_back({ pfn, original });
                Logger::Instance().Info(std::string("[VFS] IAT hooked: ") + funcName);
                return;
            }
        }
    }
    // Not a warning if missing — some DLLs may not import it
}

static bool RestoreIatPatches() {
    std::vector<IatPatch> remaining;
    for (auto patch = g_iatPatches.rbegin(); patch != g_iatPatches.rend(); ++patch) {
        DWORD oldProtect = 0;
        if (!VirtualProtect(patch->slot, sizeof(PROC), PAGE_EXECUTE_READWRITE, &oldProtect)) { remaining.push_back(*patch); continue; }
        *patch->slot = patch->original;
        DWORD ignored = 0;
        if (!VirtualProtect(patch->slot, sizeof(PROC), oldProtect, &ignored)) { remaining.push_back(*patch); continue; }
    }
    g_iatPatches = std::move(remaining);
    return g_iatPatches.empty();
}

// ─── YYToolkitAdapter ─────────────────────────────────────────────────────────

YYToolkitAdapter& YYToolkitAdapter::Instance() {
    static YYToolkitAdapter instance;
    return instance;
}

YYTK::YYTKInterface* YYToolkitAdapter::GetInterface() const {
    return static_cast<YYTK::YYTKInterface*>(m_yytoolkitInterface);
}

bool YYToolkitAdapter::Initialize() {
    if (m_initialized) return true;

    DetectProcessInfo();

    if (!BindYYToolkit()) {
        Logger::Instance().Error("YYToolkit binding unavailable; runtime initialization aborted.");
        return false;
    }

    Logger::Instance().Info("Detecting GameMaker Runner Process Architecture...");
    std::stringstream ss;
    ss << "Process: " << m_processInfo.processName
       << " | Base: 0x" << std::hex << m_processInfo.baseAddress
       << " | Module Size: 0x" << m_processInfo.moduleSize
       << " | x64: " << (m_processInfo.is64Bit ? "True" : "False");
    Logger::Instance().Info(ss.str());

    HMODULE hExe = GetModuleHandleA(NULL);

    // Hook CreateFileW only (VFS language file redirection)
    // CreateProcessW hook REMOVED — caused game window to not appear
    PatchIAT(hExe, "kernel32.dll",   "CreateFileW", (void*)HookedCreateFileW, (void**)&g_OriginalCreateFileW);
    PatchIAT(hExe, "KERNELBASE.dll", "CreateFileW", (void*)HookedCreateFileW, (void**)&g_OriginalCreateFileW);
    if (!g_OriginalCreateFileW) {
        g_OriginalCreateFileW = CreateFileW;
        Logger::Instance().Warn("[VFS] IAT hook for CreateFileW not found — using direct fallback.");
    }

    m_initialized = true;
    return true;
}

void YYToolkitAdapter::Shutdown() {
    if (!m_initialized) return;
    Logger::Instance().Info("Unbinding YYToolkit Adapter...");
    if (!RestoreIatPatches()) {
        Logger::Instance().Error("IAT restoration incomplete; shutdown remains retryable.");
        return;
    }
    g_OriginalCreateFileW = nullptr;
    m_yytoolkitBound = false;
    m_yytoolkitInterface = nullptr;
    m_bindingStatus = YYToolkitBindingStatus::NotAttempted;
    m_resolvedSymbols.clear();
    ClearFileRedirects();
    m_initialized = false;
}

bool YYToolkitAdapter::BindYYToolkit() {
    if (Aurie::Internal::g_PpGetFrameworkRoutine == nullptr) {
        m_bindingStatus = YYToolkitBindingStatus::AurieDispatcherUnavailable;
        return false;
    }

    YYTK::YYTKInterface* toolkit = nullptr;
    const Aurie::AurieStatus status = Aurie::ObGetInterface(
        "YYTK_Main",
        reinterpret_cast<Aurie::AurieInterfaceBase*&>(toolkit));
    if (!Aurie::AurieSuccess(status) || toolkit == nullptr) {
        m_bindingStatus = YYToolkitBindingStatus::YYToolkitInterfaceUnavailable;
        return false;
    }

    m_yytoolkitInterface = toolkit;
    m_yytoolkitBound = true;
    m_bindingStatus = YYToolkitBindingStatus::Bound;
    return true;
}

void YYToolkitAdapter::DetectProcessInfo() {
    char pathBuffer[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, pathBuffer, MAX_PATH);
    m_processInfo.processPath = pathBuffer;
    std::string pathStr(pathBuffer);
    size_t lastSlash = pathStr.find_last_of("\\/");
    if (lastSlash != std::string::npos)
        m_processInfo.processName = pathStr.substr(lastSlash + 1);
    else
        m_processInfo.processName = pathStr;

    HMODULE hExe = GetModuleHandleA(NULL);
    m_processInfo.baseAddress = reinterpret_cast<uintptr_t>(hExe);

    MODULEINFO modInfo = { 0 };
    if (GetModuleInformation(GetCurrentProcess(), hExe, &modInfo, sizeof(modInfo)))
        m_processInfo.moduleSize = modInfo.SizeOfImage;

#if defined(_WIN64)
    m_processInfo.is64Bit = true;
#else
    m_processInfo.is64Bit = false;
#endif
}

uintptr_t YYToolkitAdapter::FindExportOrPattern(const std::string& symbolName) {
    auto it = m_resolvedSymbols.find(symbolName);
    if (it != m_resolvedSymbols.end()) return it->second;
    return 0;
}

std::string YYToolkitAdapter::GetRunnerIdentification() const {
    std::stringstream ss;
    ss << "GameMaker Runner [Engine: VM, Arch: "
       << (m_processInfo.is64Bit ? "x64" : "x86")
       << ", Executable: " << m_processInfo.processName << "]";
    return ss.str();
}

} // namespace SoulMod
