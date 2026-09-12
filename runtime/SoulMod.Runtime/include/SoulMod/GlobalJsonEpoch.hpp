#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

namespace YYTK { struct YYTKInterface; }

namespace SoulMod {

enum class JsonEpochState { Pending, Applying, Applied, Failed };
enum class JsonApplyResult { Applied, NotReady, AlreadyApplied, AlreadyApplying, Failed };
enum class JsonReadiness { Ready, NotReady, Failed };

struct JsonGlobalLoad {
    std::string target;
    std::vector<std::string> mirrors;
    std::filesystem::path assetPath;
    std::string assetSha256;
};

class JsonEpoch;
JsonApplyResult ApplyVerifiedGlobalJson(JsonEpoch& epoch, const JsonGlobalLoad& load, YYTK::YYTKInterface& toolkit, std::string& error);

class JsonGlobalHost {
public:
    virtual ~JsonGlobalHost() = default;
    virtual JsonReadiness IsReady(std::string_view name, std::string& error) = 0;
    virtual bool ReadVerifiedJson(std::string& json, std::string& error) = 0;
    virtual bool DecodeJson(std::string_view json, std::string& error) = 0;
    virtual bool Snapshot(std::string_view name, std::string& error) = 0;
    virtual bool WriteDecoded(std::string_view name, std::string& error) = 0;
    virtual bool Restore(std::string_view name, std::string& error) = 0;
    virtual void ClearTransaction() = 0;
};

class JsonEpoch {
public:
    JsonEpoch() = default;
    JsonEpochState State() const;
    bool Deactivate();
    JsonApplyResult Apply(const JsonGlobalLoad& load, JsonGlobalHost& host, std::string& error);

private:
    std::atomic<JsonEpochState> state_{JsonEpochState::Pending};
};

}
