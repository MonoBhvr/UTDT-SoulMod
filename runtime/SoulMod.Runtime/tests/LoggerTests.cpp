#include "SoulMod/Logger.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
        : root_(fs::temp_directory_path() / ("soulmod-logger-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        fs::create_directories(root_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    const fs::path& Root() const { return root_; }

private:
    fs::path root_;
};

std::string ReadAll(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void Test_logger_writes_startup_and_shutdown_markers() {
    TemporaryDirectory directory;
    const fs::path logPath = directory.Root() / "SoulMod.Runtime.log";

    SoulMod::Logger::Instance().Shutdown();
    SoulMod::Logger::Instance().Initialize(logPath.string());
    SoulMod::Logger::Instance().Info("LoggerTests happy-path marker");
    SoulMod::Logger::Instance().Shutdown();

    const std::string contents = ReadAll(logPath);
    Require(fs::exists(logPath), "logger creates its configured log file");
    Require(contents.find("SoulMod Logger Subsystem Initialized") != std::string::npos,
        "logger writes its startup marker");
    Require(contents.find("LoggerTests happy-path marker") != std::string::npos,
        "logger flushes an ordinary message");
    Require(contents.find("SoulMod Logger Subsystem Shutdown") != std::string::npos,
        "logger flushes its shutdown marker");
}

void Test_logger_survives_unwritable_log_path() {
    TemporaryDirectory directory;
    const fs::path blockedParent = directory.Root() / "blocked-parent";
    std::ofstream(blockedParent, std::ios::binary) << "not a directory";
    const fs::path blockedLogPath = blockedParent / "SoulMod.Runtime.log";

    SoulMod::Logger::Instance().Initialize(blockedLogPath.string());
    SoulMod::Logger::Instance().Info("LoggerTests blocked-path marker");
    SoulMod::Logger::Instance().Shutdown();

    Require(!fs::exists(blockedLogPath), "logger tolerates an unwritable log path without creating a file");
}

}

int main() {
    Test_logger_writes_startup_and_shutdown_markers();
    Test_logger_survives_unwritable_log_path();
    if (failures != 0) return 1;
    std::cout << "RESULT: PASS (logger file output and unwritable path)\n";
    return 0;
}
