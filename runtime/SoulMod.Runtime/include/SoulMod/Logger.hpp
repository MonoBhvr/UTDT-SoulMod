#pragma once

#include <string>
#include <string_view>
#include <mutex>

namespace SoulMod {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

class Logger {
public:
    static Logger& Instance();

    void Initialize(const std::string& logFilePath);
    void Shutdown();

    void Log(LogLevel level, std::string_view message);

    void Trace(std::string_view message) { Log(LogLevel::Trace, message); }
    void Debug(std::string_view message) { Log(LogLevel::Debug, message); }
    void Info(std::string_view message)  { Log(LogLevel::Info, message); }
    void Warn(std::string_view message)  { Log(LogLevel::Warning, message); }
    void Error(std::string_view message) { Log(LogLevel::Error, message); }
    void Critical(std::string_view message) { Log(LogLevel::Critical, message); }

private:
    Logger() = default;
    ~Logger();

    std::string m_logFilePath;
    std::recursive_mutex m_mutex;
    bool m_initialized = false;
};

} // namespace SoulMod
