#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>

struct RValue {
    union {
        double real;
        int32_t i32;
        int64_t i64;
        const char* str;
        void* ptr;
    } val;
    uint32_t flags;
    uint32_t kind; // 0: Real, 1: String
};

typedef void(*TRoutine)(RValue* Result, void* Self, void* Other, int argc, RValue* args);

static TRoutine g_OrigDrawText = nullptr;

static void LogMessage(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FILE* f = fopen("C:\\Users\\Public\\SoulMod_POC.log", "a");
    if (f) {
        vfprintf(f, fmt, args);
        fprintf(f, "\n");
        fclose(f);
    }
    va_end(args);
}

// ─── Hook Callback: Intercepting GameMaker 'draw_text' ─────────────────────────
static void Hooked_draw_text(RValue* Result, void* Self, void* Other, int argc, RValue* args) {
    LogMessage("[POC SUCCESS] Intercepted GameMaker builtin function: draw_text (argc=%d)", argc);

    if (argc >= 3 && args != nullptr) {
        if (args[2].kind == 1 && args[2].val.str != nullptr) {
            LogMessage("  -> Original Text Argument: '%s'", args[2].val.str);

            static std::string mutatedStr;
            mutatedStr = std::string("[SoulMod-POC] ") + args[2].val.str;
            args[2].val.str = mutatedStr.c_str();

            LogMessage("  -> MUTATED Text Argument: '%s'", args[2].val.str);
        }
    }

    if (g_OrigDrawText) {
        g_OrigDrawText(Result, Self, Other, argc, args);
    }
}

// ─── YYToolkit VM Resolver Pattern Hook Installer ───────────────────────────
static void InstallPocHook() {
    LogMessage("==================================================");
    LogMessage(" SoulMod Phase 0 VM Resolver Dynamic Hook...");
    LogMessage("==================================================");

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);

    // Target string VA for "draw_text" (Base + 0x4F5CD0)
    uintptr_t drawTextStringVA = base + 0x4F5CD0;
    LogMessage("Executable Base: 0x%llX | String VA: 0x%llX", base, drawTextStringVA);

    // Dynamic scanning for code referencing the string or direct function table
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    uintptr_t codeStart = 0;
    size_t codeSize = 0;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        char secName[9] = { 0 };
        memcpy(secName, section->Name, 8);
        if (strcmp(secName, ".text") == 0) {
            codeStart = base + section->VirtualAddress;
            codeSize = section->Misc.VirtualSize;
            break;
        }
    }

    LogMessage("Scanning .text Code Section: 0x%llX - 0x%llX", codeStart, codeStart + codeSize);

    bool hooked = false;

    // Scan for LEA instruction referencing drawTextStringVA (48 8D 05 [rel32])
    for (uintptr_t p = codeStart; p < codeStart + codeSize - 7; p++) {
        unsigned char* b = (unsigned char*)p;
        // LEA RAX/RCX/RDX, [rip + disp32]
        if (b[0] == 0x48 && (b[1] == 0x8D || b[1] == 0x8D) && (b[2] & 0x07) == 0x05) {
            int32_t disp = *(int32_t*)(b + 3);
            uintptr_t targetVA = p + 7 + disp;

            if (targetVA == drawTextStringVA) {
                LogMessage("Found LEA Instruction referencing 'draw_text' string @ 0x%llX", p);

                // Nearby instruction contains routine pointer or registration call
                // Scan forward up to 64 bytes for function call or pointer assignment
                for (uintptr_t sub = p; sub < p + 64; sub++) {
                    unsigned char* sb = (unsigned char*)sub;
                    // MOV [rdata/data], routine OR CALL routine
                    if (sb[0] == 0xE8) { // CALL rel32
                        int32_t callDisp = *(int32_t*)(sb + 1);
                        uintptr_t callTarget = sub + 5 + callDisp;
                        LogMessage("  -> Found Registration Call @ 0x%llX (Target: 0x%llX)", sub, callTarget);

                        // Hook registration routine target
                        g_OrigDrawText = (TRoutine)callTarget;

                        DWORD oldProt;
                        VirtualProtect((void*)(sub + 1), 4, PAGE_EXECUTE_READWRITE, &oldProt);
                        int32_t newDisp = (int32_t)((uintptr_t)Hooked_draw_text - (sub + 5));
                        *(int32_t*)(sub + 1) = newDisp;
                        VirtualProtect((void*)(sub + 1), 4, oldProt, &oldProt);

                        LogMessage("[SUCCESS] GameMaker builtin function 'draw_text' CALL HOOKED successfully!");
                        hooked = true;
                        break;
                    }
                }
            }
        }
        if (hooked) break;
    }

    if (!hooked) {
        LogMessage("[WARN] Builtin function LEA pattern scanner complete.");
    }
}

DWORD WINAPI PocInitThread(LPVOID) {
    Sleep(2500);
    InstallPocHook();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, PocInitThread, NULL, 0, NULL);
    }
    return TRUE;
}
