#include "SoulMod/GlobalJsonEpoch.hpp"

#include <iostream>
#include <map>

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

class Host final : public SoulMod::JsonGlobalHost {
public:
    bool read = true;
    bool decode = true;
    bool snapshot = true;
    bool restore = true;
    std::string failWrite;
    std::map<std::string, std::string> values;
    std::map<std::string, std::string> snapshots;

    SoulMod::JsonReadiness IsReady(std::string_view name, std::string&) override { return values.contains(std::string(name)) ? SoulMod::JsonReadiness::Ready : SoulMod::JsonReadiness::NotReady; }
    bool ReadVerifiedJson(std::string& json, std::string&) override { json = "{}"; return read; }
    bool DecodeJson(std::string_view, std::string&) override { return decode; }
    bool Snapshot(std::string_view name, std::string&) override { if (!snapshot) return false; snapshots[std::string(name)] = values[std::string(name)]; return true; }
    bool WriteDecoded(std::string_view name, std::string&) override { if (name == failWrite) return false; values[std::string(name)] = "decoded"; return true; }
    bool Restore(std::string_view name, std::string&) override { if (!restore) return false; values[std::string(name)] = snapshots[std::string(name)]; return true; }
    void ClearTransaction() override { snapshots.clear(); }
};

SoulMod::JsonGlobalLoad Load() { return { "primary", { "mirror" } }; }

void Test_epoch_waits_then_applies_once() {
    Host host;
    SoulMod::JsonEpoch epoch;
    std::string error;
    Require(epoch.Apply(Load(), host, error) == SoulMod::JsonApplyResult::NotReady, "missing globals remain pending");
    Require(epoch.State() == SoulMod::JsonEpochState::Pending, "not-ready epoch stays pending");
    host.values = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
    Require(epoch.Apply(Load(), host, error) == SoulMod::JsonApplyResult::Applied, "ready globals apply verified JSON");
    Require(host.values["primary"] == "decoded" && host.values["mirror"] == "decoded", "target and mirror mutate together");
    Require(epoch.Apply(Load(), host, error) == SoulMod::JsonApplyResult::AlreadyApplied, "applied epoch is idempotent");
}

void Test_failed_write_rolls_back() {
    Host host;
    host.failWrite = "mirror";
    host.values = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
    SoulMod::JsonEpoch epoch;
    std::string error;
    Require(epoch.Apply(Load(), host, error) == SoulMod::JsonApplyResult::Failed, "write failure is terminal");
    Require(host.values["primary"] == "before-primary" && host.values["mirror"] == "before-mirror", "failed commit restores prior writes");
    Require(epoch.State() == SoulMod::JsonEpochState::Failed, "failed commit remains terminal");
}

void Test_terminal_read_decode_and_snapshot_failures_preserve_values() {
    for (int failure = 0; failure < 3; ++failure) {
        Host host;
        host.values = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
        if (failure == 0) host.read = false;
        if (failure == 1) host.decode = false;
        if (failure == 2) host.snapshot = false;
        SoulMod::JsonEpoch epoch;
        std::string error;
        Require(epoch.Apply(Load(), host, error) == SoulMod::JsonApplyResult::Failed, "host read/decode/snapshot failure is terminal");
        Require(host.values["primary"] == "before-primary" && host.values["mirror"] == "before-mirror", "pre-write failure preserves globals");
    }
}

void Test_deactivated_epoch_reapplies() {
    Host host;
    host.values = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
    SoulMod::JsonEpoch epoch;
    std::string error;
    Require(epoch.Apply(Load(), host, error) == SoulMod::JsonApplyResult::Applied, "initial selector epoch applies");
    Require(epoch.Deactivate(), "selector false deactivates applied epoch");
    Require(epoch.Apply(Load(), host, error) == SoulMod::JsonApplyResult::Applied, "selector true reapplies deactivated epoch");
}

}

int main() {
    Test_epoch_waits_then_applies_once();
    Test_failed_write_rolls_back();
    Test_terminal_read_decode_and_snapshot_failures_preserve_values();
    Test_deactivated_epoch_reapplies();
    if (failures != 0) return 1;
    std::cout << "RESULT: PASS (global JSON epoch atomicity)\n";
    return 0;
}
