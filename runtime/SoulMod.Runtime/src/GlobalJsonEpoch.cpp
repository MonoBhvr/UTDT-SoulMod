#include "SoulMod/GlobalJsonEpoch.hpp"
#include "SoulMod/PackageValidation.hpp"

#include <YYToolkit/YYTK_Shared.hpp>

#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace SoulMod {
namespace {

class YYToolkitJsonGlobalHost final : public JsonGlobalHost {
public:
    YYToolkitJsonGlobalHost(YYTK::YYTKInterface& toolkit, const JsonGlobalLoad& load) : interface_(toolkit), load_(load) {}

    JsonReadiness IsReady(const std::string_view name, std::string& error) override {
        const YYTK::RValue exists = interface_.CallBuiltin("variable_global_exists", { YYTK::RValue(name) });
        if (exists.m_Kind == YYTK::VALUE_UNDEFINED) { error = "variable_global_exists returned undefined"; return JsonReadiness::Failed; }
        return exists.ToBoolean() ? JsonReadiness::Ready : JsonReadiness::NotReady;
    }
    bool ReadVerifiedJson(std::string& json, std::string& error) override {
        std::ifstream input(load_.assetPath, std::ios::binary);
        if (!input) { error = "cannot open global JSON asset"; return false; }
        std::ostringstream bytes; bytes << input.rdbuf(); json = bytes.str();
        return !input.bad() && VerifySha256File(load_.assetPath, load_.assetSha256, error);
    }
    bool DecodeJson(const std::string_view json, std::string& error) override {
        decoded_ = interface_.CallBuiltin("json_decode", { YYTK::RValue(json) });
        if (decoded_.m_Kind != YYTK::VALUE_UNDEFINED) return true;
        error = "json_decode returned undefined";
        return false;
    }
    bool Snapshot(const std::string_view name, std::string& error) override {
        const std::string global(name);
        const YYTK::RValue value = interface_.CallBuiltin("variable_global_get", { YYTK::RValue(global) });
        if (value.m_Kind == YYTK::VALUE_UNDEFINED) { error = "variable_global_get returned undefined"; return false; }
        snapshots_.emplace(global, value);
        return true;
    }
    bool WriteDecoded(const std::string_view name, std::string& error) override { return Set(name, decoded_, error); }
    bool Restore(const std::string_view name, std::string& error) override {
        const auto snapshot = snapshots_.find(std::string(name));
        if (snapshot == snapshots_.end()) { error = "global snapshot missing"; return false; }
        return Set(snapshot->first, snapshot->second, error);
    }
    void ClearTransaction() override { snapshots_.clear(); decoded_ = YYTK::RValue(); }

private:
    bool Set(const std::string_view name, const YYTK::RValue& value, std::string& error) {
        const std::string global(name);
        interface_.CallBuiltin("variable_global_set", { YYTK::RValue(global), value });
        const YYTK::RValue observed = interface_.CallBuiltin("variable_global_get", { YYTK::RValue(global) });
        if (observed.m_Kind == value.m_Kind && observed.ToDouble() == value.ToDouble()) return true;
        error = "global write verification mismatch";
        return false;
    }
    YYTK::YYTKInterface& interface_;
    const JsonGlobalLoad& load_;
    YYTK::RValue decoded_;
    std::map<std::string, YYTK::RValue> snapshots_;
};

}

JsonEpochState JsonEpoch::State() const {
    return state_.load();
}

bool JsonEpoch::Deactivate() {
    JsonEpochState current = state_.load();
    while (current != JsonEpochState::Applying) {
        if (state_.compare_exchange_weak(current, JsonEpochState::Pending)) return current != JsonEpochState::Pending;
    }
    return false;
}

JsonApplyResult JsonEpoch::Apply(const JsonGlobalLoad& load, JsonGlobalHost& host, std::string& error) {
    error.clear();
    JsonEpochState expected = JsonEpochState::Pending;
    if (!state_.compare_exchange_strong(expected, JsonEpochState::Applying)) {
        if (expected == JsonEpochState::Applied) return JsonApplyResult::AlreadyApplied;
        if (expected == JsonEpochState::Failed) return JsonApplyResult::Failed;
        return JsonApplyResult::AlreadyApplying;
    }

    std::vector<std::string_view> names;
    names.reserve(load.mirrors.size() + 1);
    names.push_back(load.target);
    for (const std::string& mirror : load.mirrors) names.push_back(mirror);
    for (const std::string_view name : names) {
        const JsonReadiness readiness = host.IsReady(name, error);
        if (readiness == JsonReadiness::Ready) continue;
        host.ClearTransaction();
        if (readiness == JsonReadiness::NotReady) {
            state_.store(JsonEpochState::Pending);
            return JsonApplyResult::NotReady;
        }
        state_.store(JsonEpochState::Failed);
        return JsonApplyResult::Failed;
    }

    std::string json;
    if (!host.ReadVerifiedJson(json, error) || !host.DecodeJson(json, error)) {
        host.ClearTransaction();
        state_.store(JsonEpochState::Failed);
        return JsonApplyResult::Failed;
    }
    for (const std::string_view name : names) {
        if (host.Snapshot(name, error)) continue;
        host.ClearTransaction();
        state_.store(JsonEpochState::Failed);
        return JsonApplyResult::Failed;
    }

    std::vector<std::string_view> written;
    for (const std::string_view name : names) {
        if (host.WriteDecoded(name, error)) {
            written.push_back(name);
            continue;
        }
        if (std::find(written.begin(), written.end(), name) == written.end()) written.push_back(name);
        for (auto it = written.rbegin(); it != written.rend(); ++it) {
            std::string rollbackError;
            if (!host.Restore(*it, rollbackError) && error.empty()) error = rollbackError;
        }
        host.ClearTransaction();
        state_.store(JsonEpochState::Failed);
        return JsonApplyResult::Failed;
    }

    host.ClearTransaction();
    state_.store(JsonEpochState::Applied);
    return JsonApplyResult::Applied;
}

JsonApplyResult ApplyVerifiedGlobalJson(JsonEpoch& epoch, const JsonGlobalLoad& load, YYTK::YYTKInterface& toolkit, std::string& error) {
    YYToolkitJsonGlobalHost host(toolkit, load);
    return epoch.Apply(load, host, error);
}

}
