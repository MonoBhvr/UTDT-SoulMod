#include "SoulMod/Logger.hpp"
#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace SoulMod {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    Shutdown();
}

void Logger::Initialize(const std::string& logFilePath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return;

    m_logFilePath = logFilePath;
    m_initialized = true;
    
    Log(LogLevel::Info, "==================================================");
    Log(LogLevel::Info, "SoulMod Logger Subsystem Initialized");
    Log(LogLevel::Info, "==================================================");
}

void Logger::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return;

    Log(LogLevel::Info, "SoulMod Logger Subsystem Shutdown");
    m_initialized = false;
}

void Logger::Log(LogLevel level, std::string_view message) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const char* levelStr = "INFO";
    switch (level) {
        case LogLevel::Trace:    levelStr = "TRACE"; break;
        case LogLevel::Debug:    levelStr = "DEBUG"; break;
        case LogLevel::Info:     levelStr = "INFO"; break;
        case LogLevel::Warning:  levelStr = "WARN"; break;
        case LogLevel::Error:    levelStr = "ERROR"; break;
        case LogLevel::Critical: levelStr = "CRIT"; break;
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    char timeBuf[64] = { 0 };
    struct tm buf;
    localtime_s(&buf, &in_time_t);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &buf);

    std::stringstream ss;
    ss << "[" << timeBuf << "] [" << levelStr << "] [SoulMod] " << message << "\r\n";
    std::string formatted = ss.str();

    if (!m_logFilePath.empty()) {
        HANDLE hFile = CreateFileA(
            m_logFilePath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hFile != INVALID_HANDLE_VALUE) {
            SetFilePointer(hFile, 0, NULL, FILE_END);
            DWORD written = 0;
            WriteFile(hFile, formatted.c_str(), (DWORD)formatted.length(), &written, NULL);
            FlushFileBuffers(hFile);
            CloseHandle(hFile);
        }
    }

#if defined(_WIN32)
    OutputDebugStringA(formatted.c_str());
#endif
}

} // namespace SoulMod
