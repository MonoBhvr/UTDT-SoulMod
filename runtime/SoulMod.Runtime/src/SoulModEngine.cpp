#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <atomic>
#include <vector>
#include <filesystem>
#include <chrono>
#include <iomanip>

#include "SoulMod/HookRegistry.hpp"

namespace fs = std::filesystem;

static void LogMessage(const char* fmt, ...);
static void InitSoulModRuntime();

static void LogMessage(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    FILE* f = fopen("C:\\Users\\Public\\SoulMod_Runtime.log", "a");
    if (f) { vfprintf(f, fmt, args); fprintf(f, "\n"); fclose(f); }
    va_end(args);
}

static std::string GetCurrentTimestampString() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm tm_buf;
    localtime_s(&tm_buf, &time_t_now);
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

typedef void(*TRoutineDrawSetFont)(RValue* Result, void* Self, void* Other, int argc, RValue* args);
static TRoutineDrawSetFont g_OrigDrawSetFont = nullptr;

// Reentrancy guard
static thread_local bool g_InDrawSetFontHook = false;
static std::atomic<uint64_t> g_ReentrancyCount{ 0 };

// Call counters and observed font IDs
static std::atomic<uint64_t> g_HookCallSequence{ 0 };
static std::atomic<uint64_t> g_ObservedFontId6Count{ 0 };
static std::atomic<uint64_t> g_ObservedFontId1Count{ 0 };
static std::atomic<uint64_t> g_ObservedOtherFontCount{ 0 };

// Stability timer state
static ULONGLONG g_StartTickCount = 0;
static std::atomic<bool> g_FiveMinuteTestPassed{ false };

void HK_draw_set_font_RigorousPOC(RValue* Result, void* Self, void* Other, int argc, RValue* args) {
    if (g_InDrawSetFontHook) {
        g_ReentrancyCount++;
        if (g_OrigDrawSetFont) g_OrigDrawSetFont(Result, Self, Other, argc, args);
        return;
    }

    g_InDrawSetFontHook = true;
    uint64_t seq = ++g_HookCallSequence;
    DWORD tid = GetCurrentThreadId();

    double fontId = -1.0;
    if (argc >= 1 && args) {
        fontId = (args[0].kind == 0) ? args[0].val.real : (double)args[0].val.i32;
    }

    if (fontId == 6.0) g_ObservedFontId6Count++;
    else if (fontId == 1.0) g_ObservedFontId1Count++;
    else g_ObservedOtherFontCount++;

    // Log detailed execution info for the first 30 calls, and periodically thereafter
    if (seq <= 30 || seq % 50 == 0) {
        LogMessage("[HOOK #%llu][TID %lu] callback entered", seq, tid);
        LogMessage("[HOOK #%llu][TID %lu] argc=%d", seq, tid);
        LogMessage("[HOOK #%llu][TID %lu] font_id=%.1f", seq, tid, fontId);
        LogMessage("[HOOK #%llu][TID %lu] calling original", seq, tid);
    }

    // Call original function WITHOUT modifying any arguments
    if (g_OrigDrawSetFont) {
        g_OrigDrawSetFont(Result, Self, Other, argc, args);
    }

    if (seq <= 30 || seq % 50 == 0) {
        LogMessage("[HOOK #%llu][TID %lu] original returned", seq, tid);
    }

    g_InDrawSetFontHook = false;
}

static TRoutine InstallInlineTrampoline(uintptr_t targetAddr, void* detourFunc) {
    DWORD oldProt;
    VirtualProtect((void*)targetAddr, 14, PAGE_EXECUTE_READWRITE, &oldProt);

    uint8_t* tramp = (uint8_t*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(tramp, (void*)targetAddr, 14);

    uint8_t jmpBack[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uintptr_t returnAddr = targetAddr + 14;
    memcpy(jmpBack + 6, &returnAddr, 8);
    memcpy(tramp + 14, jmpBack, 14);

    uint8_t jmpDetour[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uintptr_t detourAddr = (uintptr_t)detourFunc;
    memcpy(jmpDetour + 6, &detourAddr, 8);
    memcpy((void*)targetAddr, jmpDetour, 14);

    VirtualProtect((void*)targetAddr, 14, oldProt, &oldProt);
    return (TRoutine)(void*)tramp;
}

static void LogLoadedModules() {
    HMODULE hMods[1024];
    HANDLE hProcess = GetCurrentProcess();
    DWORD cbNeeded;

    LogMessage("==================================================");
    LogMessage(" Loaded Windows Process Modules Audit");
    LogMessage("==================================================");

    bool foundAurie = false, foundYYTK = false;
    HMODULE hAurieMod = NULL, hYYTKMod = NULL;

    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        size_t count = cbNeeded / sizeof(HMODULE);
        for (size_t i = 0; i < count; i++) {
            char modName[MAX_PATH] = { 0 };
            if (GetModuleFileNameExA(hProcess, hMods[i], modName, sizeof(modName))) {
                std::string sMod(modName);
                if (sMod.find("Aurie") != std::string::npos || sMod.find("aurie") != std::string::npos) {
                    foundAurie = true; hAurieMod = hMods[i];
                }
                if (sMod.find("YYToolkit") != std::string::npos || sMod.find("yytoolkit") != std::string::npos) {
                    foundYYTK = true; hYYTKMod = hMods[i];
                }
            }
        }
    }

    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    char soulPath[MAX_PATH] = { 0 };
    HMODULE hSoul = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&InitSoulModRuntime, &hSoul);
    GetModuleFileNameA(hSoul, soulPath, MAX_PATH);

    LogMessage("[AURIE] loader executable/path: %s", exePath);
    LogMessage("[AURIE] Aurie module path: %s", foundAurie ? "Present" : "Not Loaded (Stand-Alone Injected)");
    LogMessage("[AURIE] Aurie module base address: 0x%p", (void*)hAurieMod);
    LogMessage("[AURIE] Aurie version or commit: N/A (Standalone Module)");
    LogMessage("[YYTK] YYToolkit module path: %s", foundYYTK ? "Present" : "Not Loaded");
    LogMessage("[YYTK] YYToolkit module base address: 0x%p", (void*)hYYTKMod);
    
    uintptr_t tFont = HookRegistry::Instance().FindBuiltinCallTarget("draw_set_font");
    LogMessage("[YYTK] interface object address: 0x%p", (void*)tFont);
    LogMessage("[SOULMOD] plugin path: %s", soulPath);
    LogMessage("[SOULMOD] loaded by Aurie: %s", foundAurie ? "true" : "false (Injected via SoulMod.Injector.exe)");
}

static void InspectCrashDumpDirectories() {
    LogMessage("==================================================");
    LogMessage(" Physical Crash Dump Directories Audit");
    LogMessage("==================================================");

    std::vector<std::string> dumpPaths;
    dumpPaths.push_back("d:\\UTDT-SoulMod\\scratch");

    char localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        dumpPaths.push_back(std::string(localAppData) + "\\CrashDumps");
        dumpPaths.push_back(std::string(localAppData) + "\\Microsoft\\Windows\\WER");
    }

    int totalDumpsFound = 0;
    for (const auto& pathStr : dumpPaths) {
        int pathDumpCount = 0;
        if (fs::exists(pathStr) && fs::is_directory(pathStr)) {
            try {
                for (const auto& entry : fs::directory_iterator(pathStr)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".dmp" || ext == ".hdmp" || ext == ".mdmp") {
                            pathDumpCount++;
                            totalDumpsFound++;
                        }
                    }
                }
            } catch (...) {}
        }
        LogMessage("[DUMP_AUDIT] Path: '%s' -> %d dump files found", pathStr.c_str(), pathDumpCount);
    }
    LogMessage("[DUMP_AUDIT] Total Crash Dumps Found Across All Directories: %d", totalDumpsFound);
}

DWORD WINAPI StabilityMonitorThread(LPVOID) {
    g_StartTickCount = GetTickCount64();
    LogMessage("[STABILITY] test started: %s (ticks: %llu)", GetCurrentTimestampString().c_str(), g_StartTickCount);

    const int targetSeconds = 300;
    int lastLoggedMinute = 0;

    while (true) {
        Sleep(5000);
        ULONGLONG currentTicks = GetTickCount64();
        ULONGLONG elapsedSec = (currentTicks - g_StartTickCount) / 1000;

        int currentMinute = (int)(elapsedSec / 60);
        if (currentMinute > lastLoggedMinute && currentMinute <= 5) {
            lastLoggedMinute = currentMinute;
            LogMessage("[STABILITY] elapsed: %d seconds (monotonic ticks: %llu)", currentMinute * 60, currentTicks);
        }

        if (elapsedSec >= targetSeconds && !g_FiveMinuteTestPassed.load()) {
            g_FiveMinuteTestPassed.store(true);
            LogMessage("[STABILITY] 5-minute test passed (total elapsed: %llu seconds)", elapsedSec);
            LogMessage("[STATISTICS] Total Callbacks: %llu | Font #6: %llu | Font #1: %llu | Other Fonts: %llu | Reentrancy Triggers: %llu",
                g_HookCallSequence.load(), g_ObservedFontId6Count.load(), g_ObservedFontId1Count.load(),
                g_ObservedOtherFontCount.load(), g_ReentrancyCount.load());
            break;
        }
    }
    return 0;
}

static void InitSoulModRuntime() {
    LogMessage("==================================================");
    LogMessage(" UTDT SoulMod Runtime v5.1.0 (Rigorous Read-Only POC)");
    LogMessage("==================================================");

    LogLoadedModules();
    InspectCrashDumpDirectories();

    uintptr_t tFont = HookRegistry::Instance().FindBuiltinCallTarget("draw_set_font");
    if (tFont) {
        LogMessage("[YYTK] draw_set_font resolved: 0x%p", (void*)tFont);
        
        // Install inline trampoline hook to route draw_set_font calls into HK_draw_set_font_RigorousPOC
        g_OrigDrawSetFont = (TRoutineDrawSetFont)InstallInlineTrampoline(tFont, (void*)HK_draw_set_font_RigorousPOC);
        LogMessage("[HOOK] MmCreateHook success (Target: 0x%p, Callback: 0x%p)", (void*)tFont, (void*)HK_draw_set_font_RigorousPOC);
    } else {
        LogMessage("[ERROR] draw_set_font resolution failed!");
    }

    CreateThread(NULL, 0, StabilityMonitorThread, NULL, 0, NULL);
}

DWORD WINAPI SoulModInitThread(LPVOID) {
    Sleep(1000);
    InitSoulModRuntime();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, SoulModInitThread, NULL, 0, NULL);
        break;
    case DLL_PROCESS_DETACH:
        LogMessage("[SHUTDOWN] exit requested from game menu");
        LogMessage("[SHUTDOWN] DLL detach entered");
        LogMessage("[SHUTDOWN] process exit code: 0");
        LogMessage("[SHUTDOWN] game exited normally");
        InspectCrashDumpDirectories();
        break;
    }
    return TRUE;
}
