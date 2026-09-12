#include "SoulMod/HookRegistry.hpp"
#include <cstdio>
#include <cstdarg>

static void LogHook(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    FILE* f = fopen("C:\\Users\\Public\\SoulMod_Runtime.log", "a");
    if (f) { vfprintf(f, fmt, args); fprintf(f, "\n"); fclose(f); }
    va_end(args);
}

HookRegistry& HookRegistry::Instance() {
    static HookRegistry instance;
    return instance;
}

bool HookRegistry::Initialize() {
    if (m_initialized) return true;
    m_baseAddress = (uintptr_t)GetModuleHandleA(NULL);
    m_initialized = true;
    LogHook("[HOOK_REG] Base address = 0x%p", (void*)m_baseAddress);
    return true;
}

uintptr_t HookRegistry::FindBuiltinCallTarget(const std::string& functionName) {
    if (!m_baseAddress) Initialize();

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)m_baseAddress;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(m_baseAddress + dos->e_lfanew);

    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    uintptr_t stringVA = 0;
    uintptr_t codeStart = 0;
    size_t codeSize = 0;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        char secName[9] = { 0 };
        memcpy(secName, section->Name, 8);

        if (strcmp(secName, ".rdata") == 0) {
            uintptr_t rdataStart = m_baseAddress + section->VirtualAddress;
            size_t rdataSize = section->Misc.VirtualSize;

            for (uintptr_t p = rdataStart; p < rdataStart + rdataSize - functionName.length(); p++) {
                if (strcmp((const char*)p, functionName.c_str()) == 0) {
                    stringVA = p;
                    break;
                }
            }
        }
        else if (strcmp(secName, ".text") == 0) {
            codeStart = m_baseAddress + section->VirtualAddress;
            codeSize = section->Misc.VirtualSize;
        }
    }

    if (stringVA == 0 || codeStart == 0) {
        LogHook("[HOOK_REG] Failed to find string '%s' in .rdata", functionName.c_str());
        return 0;
    }

    // Search for LEA RCX, [stringVA]
    for (uintptr_t p = codeStart; p < codeStart + codeSize - 7; p++) {
        unsigned char* b = (unsigned char*)p;
        if (b[0] == 0x48 && b[1] == 0x8D && (b[2] == 0x0D || b[2] == 0x05)) {
            int32_t disp = *(int32_t*)(b + 3);
            uintptr_t target = p + 7 + disp;

            if (target == stringVA) {
                // Scan forward 0..64 bytes from p for LEA reg, [RoutineVA]
                for (int off = 0; off < 64; off++) {
                    uintptr_t sub = p + off;
                    if (sub + 7 > codeStart + codeSize) break;
                    unsigned char* sb = (unsigned char*)sub;

                    if ((sb[0] == 0x48 || sb[0] == 0x4C) && sb[1] == 0x8D && (sb[2] & 0xC0) == 0x00) {
                        int32_t rdisp = *(int32_t*)(sb + 3);
                        uintptr_t routineVA = sub + 7 + rdisp;

                        if (routineVA >= codeStart && routineVA < codeStart + codeSize && routineVA != stringVA) {
                            LogHook("[HOOK_REG] Resolved '%s' routineVA = 0x%p", functionName.c_str(), (void*)routineVA);
                            return routineVA;
                        }
                    }
                }
            }
        }
    }

    LogHook("[HOOK_REG] Failed to resolve call target for '%s'", functionName.c_str());
    return 0;
}

static std::unordered_map<std::string, BuiltinHookCallback> g_Callbacks;
static std::unordered_map<std::string, TRoutine> g_Originals;

static void GenericHookThunk(const std::string& name, RValue* Result, void* Self, void* Other, int argc, RValue* args) {
    auto itCb = g_Callbacks.find(name);
    auto itOrig = g_Originals.find(name);
    TRoutine orig = (itOrig != g_Originals.end()) ? itOrig->second : nullptr;

    if (itCb != g_Callbacks.end()) {
        itCb->second(Result, Self, Other, argc, args, orig);
    } else if (orig) {
        orig(Result, Self, Other, argc, args);
    }
}

static void Thunk_draw_text(RValue* Result, void* Self, void* Other, int argc, RValue* args) {
    GenericHookThunk("draw_text", Result, Self, Other, argc, args);
}

static void Thunk_draw_text_ext(RValue* Result, void* Self, void* Other, int argc, RValue* args) {
    GenericHookThunk("draw_text_ext", Result, Self, Other, argc, args);
}

static void Thunk_draw_text_transformed(RValue* Result, void* Self, void* Other, int argc, RValue* args) {
    GenericHookThunk("draw_text_transformed", Result, Self, Other, argc, args);
}

static void Thunk_draw_set_font(RValue* Result, void* Self, void* Other, int argc, RValue* args) {
    GenericHookThunk("draw_set_font", Result, Self, Other, argc, args);
}

// ─── Inline Hook / Trampoline Helper ──────────────────────────────────────────
struct TrampolineInfo {
    uint8_t originalBytes[16];
    uint8_t trampolineBuffer[32];
};

static std::unordered_map<uintptr_t, TrampolineInfo> g_Trampolines;

static TRoutine CreateInlineHook(uintptr_t targetAddr, void* detourFunc) {
    DWORD oldProt;
    VirtualProtect((void*)targetAddr, 14, PAGE_EXECUTE_READWRITE, &oldProt);

    TrampolineInfo& info = g_Trampolines[targetAddr];
    memcpy(info.originalBytes, (void*)targetAddr, 14);

    VirtualProtect(info.trampolineBuffer, sizeof(info.trampolineBuffer), PAGE_EXECUTE_READWRITE, &oldProt);
    memcpy(info.trampolineBuffer, (void*)targetAddr, 14);

    // JMP back to targetAddr + 14: FF 25 00 00 00 00 [targetAddr + 14]
    uint8_t jmpBack[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uintptr_t returnAddr = targetAddr + 14;
    memcpy(jmpBack + 6, &returnAddr, 8);
    memcpy(info.trampolineBuffer + 14, jmpBack, 14);

    // Write JMP detour to targetAddr: FF 25 00 00 00 00 [detourFunc]
    uint8_t jmpDetour[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uintptr_t detourAddr = (uintptr_t)detourFunc;
    memcpy(jmpDetour + 6, &detourAddr, 8);
    memcpy((void*)targetAddr, jmpDetour, 14);

    VirtualProtect((void*)targetAddr, 14, oldProt, &oldProt);

    return (TRoutine)(void*)info.trampolineBuffer;
}

bool HookRegistry::RegisterBuiltin(const std::string& functionName, BuiltinHookCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);

    uintptr_t targetAddr = FindBuiltinCallTarget(functionName);
    if (!targetAddr) {
        LogHook("[HOOK_REG] RegisterBuiltin failed for '%s' (targetAddr=0)", functionName.c_str());
        return false;
    }

    g_Callbacks[functionName] = callback;

    TRoutine detourThunk = nullptr;
    if (functionName == "draw_text") detourThunk = Thunk_draw_text;
    else if (functionName == "draw_text_ext") detourThunk = Thunk_draw_text_ext;
    else if (functionName == "draw_text_transformed") detourThunk = Thunk_draw_text_transformed;
    else if (functionName == "draw_set_font") detourThunk = Thunk_draw_set_font;

    if (detourThunk != nullptr) {
        TRoutine originalTrampoline = CreateInlineHook(targetAddr, (void*)detourThunk);
        g_Originals[functionName] = originalTrampoline;
        m_originalRoutines[functionName] = originalTrampoline;
        LogHook("[HOOK_REG] Inline entry hook installed for '%s' @ 0x%p -> thunk @ 0x%p",
            functionName.c_str(), (void*)targetAddr, (void*)detourThunk);
        return true;
    } else {
        g_Originals[functionName] = (TRoutine)targetAddr;
        m_originalRoutines[functionName] = (TRoutine)targetAddr;
        LogHook("[HOOK_REG] Registered original routine for '%s' @ 0x%p (no detour)",
            functionName.c_str(), (void*)targetAddr);
        return true;
    }
}

TRoutine HookRegistry::GetOriginal(const std::string& functionName) {
    auto it = m_originalRoutines.find(functionName);
    if (it != m_originalRoutines.end()) return it->second;

    uintptr_t addr = FindBuiltinCallTarget(functionName);
    if (addr) {
        m_originalRoutines[functionName] = (TRoutine)addr;
        return (TRoutine)addr;
    }
    return nullptr;
}
