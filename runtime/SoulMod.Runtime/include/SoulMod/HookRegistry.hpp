#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>

// GameMaker x64 RValue Structure Definition
struct RValue {
    union {
        double real;
        int32_t i32;
        int64_t i64;
        const char* str;
        void* ptr;
    } val;
    uint32_t flags;
    uint32_t kind; // 0: Real, 1: String, 2: Array, 7: Int64, 12: Undefined
};

typedef void(*TRoutine)(RValue* Result, void* Self, void* Other, int argc, RValue* args);
typedef std::function<void(RValue* Result, void* Self, void* Other, int argc, RValue* args, TRoutine OriginalRoutine)> BuiltinHookCallback;

class HookRegistry {
public:
    static HookRegistry& Instance();

    bool Initialize();
    bool RegisterBuiltin(const std::string& functionName, BuiltinHookCallback callback);
    TRoutine GetOriginal(const std::string& functionName);
    uintptr_t FindBuiltinCallTarget(const std::string& functionName);

private:
    HookRegistry() = default;

    std::mutex m_mutex;
    bool m_initialized = false;
    uintptr_t m_baseAddress = 0;
    std::unordered_map<std::string, TRoutine> m_originalRoutines;
    std::unordered_map<std::string, BuiltinHookCallback> m_userCallbacks;
};
