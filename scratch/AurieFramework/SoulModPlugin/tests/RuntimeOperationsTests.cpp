#include "RuntimeOperations.hpp"
#include "GlobalJsonLoad.hpp"
#include "RestartCoordinator.hpp"

#include <array>
#include <atomic>
#include <iostream>
#include <fstream>
#include <map>
#include <optional>
#include <vector>

namespace
{
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }

    class FakeGlobalJsonLoadHost final : public soulmod::GlobalJsonLoadHost
    {
    public:
        bool readSucceeds = true;
        bool decodeSucceeds = true;
        bool conditionReadFails = false;
        std::string failWriteName;
        std::map<std::string, std::string> globals;
        std::map<std::string, std::string> conditionGlobals;
        std::map<std::string, std::string> snapshots;
        std::vector<std::string> events;

        soulmod::GlobalJsonLoadReadiness CheckGlobalReady(std::string_view name, std::string&) override
        {
            events.push_back("ready:" + std::string(name));
            return globals.contains(std::string(name)) ? soulmod::GlobalJsonLoadReadiness::Ready : soulmod::GlobalJsonLoadReadiness::NotReady;
        }

        soulmod::GlobalJsonLoadConditionReadiness ReadConditionGlobal(std::string_view name, std::string& value, std::string& error) override
        {
            events.push_back("condition:" + std::string(name));
            if (conditionReadFails) { error = "condition global is not a string"; return soulmod::GlobalJsonLoadConditionReadiness::Failure; }
            const auto condition = conditionGlobals.find(std::string(name));
            if (condition == conditionGlobals.end()) return soulmod::GlobalJsonLoadConditionReadiness::Missing;
            value = condition->second;
            return soulmod::GlobalJsonLoadConditionReadiness::String;
        }

        bool ReadVerifiedAsset(const soulmod::GlobalJsonLoad&, std::string& bytes, std::string& error) override
        {
            events.push_back("read");
            if (!readSucceeds) { error = "asset verification failed"; return false; }
            bytes = "{\"value\":1}";
            return true;
        }

        bool DecodeJson(std::string_view, std::string& error) override
        {
            events.push_back("decode");
            if (!decodeSucceeds) { error = "JSON decode failed"; return false; }
            return true;
        }

        bool SnapshotGlobal(std::string_view name, std::string& error) override
        {
            events.push_back("snapshot:" + std::string(name));
            const auto global = globals.find(std::string(name));
            if (global == globals.end()) { error = "global does not exist"; return false; }
            snapshots.emplace(global->first, global->second);
            return true;
        }

        bool SetDecodedGlobal(std::string_view name, std::string& error) override
        {
            events.push_back("write:" + std::string(name));
            if (name == failWriteName) { error = "write failed"; return false; }
            globals[std::string(name)] = "decoded";
            return true;
        }

        bool RestoreSnapshot(std::string_view name, std::string&) override
        {
            events.push_back("restore:" + std::string(name));
            globals[std::string(name)] = snapshots.at(std::string(name));
            return true;
        }

        void Clear() override { events.push_back("clear"); snapshots.clear(); }
    };

    soulmod::GlobalJsonLoad MakeGlobalJsonLoad(std::optional<soulmod::GlobalJsonLoadCondition> condition = std::nullopt)
    {
        return { "test.load", "primary", { "mirror" }, std::move(condition), "asset.json", std::string(64, '0'), "first.draw_set_font" };
    }

    void RequireEvents(const std::vector<std::string>& actual, const std::vector<std::string>& expected, const std::string& message)
    {
        Require(actual == expected, message);
    }
}

int main(int argc, char* argv[])
{
    Require(argc == 2, "expected the runtime-proof package directory");

    soulmod::PackageOperations operations;
    std::string error;
    const std::filesystem::path packageRoot = argv[1];
    Require(soulmod::LoadPackageOperations(packageRoot, operations, error), "global.json.load must parse the runtime-proof package: " + error);
    Require(operations.packageId == "soulmod.runtime-proof.chapter1", "manifest ID must be retained");
    Require(operations.globalJsonLoads.size() == 1, "runtime proof must declare exactly one global JSON load");
    const soulmod::GlobalJsonLoad& globalLoad = operations.globalJsonLoads.front();
    Require(globalLoad.operationId == "chapter1.language.global-map", "global JSON load must retain its operation ID");
    Require(globalLoad.applyAt == "first.draw_set_font", "global JSON load must retain its application point");
    Require(globalLoad.condition.has_value(), "runtime proof must retain its conditional selector");
    Require(globalLoad.condition->global == "lang" && globalLoad.condition->equals == "ja", "runtime proof condition must select the Japanese slot");
    Require(globalLoad.mirrorGlobals.empty(), "runtime proof must not declare globals absent from the target build");

    Require(operations.fileRedirects.empty(), "runtime proof must not declare resource redirects");
    Require(!soulmod::SelectFileRedirect(operations, "C:\\Game\\lang\\lang_ja.json").has_value(), "runtime proof must leave language files untouched");
    Require(!soulmod::SelectFileRedirect(operations, "lang/lang_en.json").has_value(), "undeclared logical path must not redirect");
    Require(!soulmod::SelectFileRedirect(operations, "lang\\lang_ja.json").has_value(), "runtime proof must leave backslash language paths untouched");

    Require(!soulmod::SelectFontRedirect(operations, "fnt_main").has_value(), "runtime proof must leave the generic font slot untouched");
    const auto japaneseFontTarget = soulmod::SelectFontRedirect(operations, "fnt_ja_main");
    Require(japaneseFontTarget.has_value(), "declared Japanese font resource must redirect");
    Require(*japaneseFontTarget == "soulmod.galmuri11", "Japanese font redirect must select declared registration");
    Require(!soulmod::SelectFontRedirect(operations, "fnt_unknown").has_value(), "undeclared source font resource must not redirect");

    const auto font = operations.registeredFonts.find(*japaneseFontTarget);
    Require(font != operations.registeredFonts.end(), "font redirect target must name a registered font");
    Require(font->second.size == 10 && font->second.first == 32 && font->second.last == 55203, "font selector must be retained");

    const std::filesystem::path selectorFontPackage = packageRoot / "native-selector-font-package";
    std::filesystem::create_directories(selectorFontPackage / "assets" / "fonts");
    std::filesystem::copy_file(font->second.assetPath, selectorFontPackage / "assets" / "fonts" / "Galmuri11.ttf", std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream manifest(selectorFontPackage / "manifest.json");
        manifest << R"({"formatVersion":1,"id":"selector.font.package","operationsFile":"operations.json"})";
        std::ofstream operationFile(selectorFontPackage / "operations.json");
        operationFile << R"([{"id":"selector.font.register","modId":"selector.font.package","target":{"type":"font","name":"registered.font"},"operation":"font.register","selector":{"size":14,"bold":false,"italic":false,"first":32,"last":55203},"assetPath":"assets/fonts/Galmuri11.ttf","assetSha256":"2c709890595668f7bdb6df408420fda957dde0288e95b31a1cc17a2ab98b4b4f"},{"id":"selector.font.redirect","modId":"selector.font.package","target":{"type":"font","name":"fnt_main"},"operation":"font.redirect","selector":{"sourceId":6},"value":"registered.font"}])";
    }
    soulmod::PackageOperations selectorFontOperations;
    Require(!soulmod::LoadPackageOperations(selectorFontPackage, selectorFontOperations, error), "font redirects must reject legacy selectors");
    std::filesystem::remove_all(selectorFontPackage);

    const auto discovered = soulmod::DiscoverPackageRoot(packageRoot / "bin" / "SoulModPlugin.dll", error);
    Require(discovered.has_value() && *discovered == packageRoot, "generic discovery must locate package while walking module ancestors");

    const std::filesystem::path invalidPackage = packageRoot / "native-invalid-package";
    std::filesystem::create_directories(invalidPackage);
    {
        std::ofstream manifest(invalidPackage / "manifest.json");
        manifest << R"({"formatVersion":1,"id":"invalid.package","operationsFile":"operations.json"})";
        std::ofstream operationFile(invalidPackage / "operations.json");
        operationFile << R"([{"id":"invalid.redirect","modId":"invalid.package","target":{"type":"file","name":"../outside.json"},"operation":"resource.redirect","assetPath":"../outside.json","assetSha256":"0000000000000000000000000000000000000000000000000000000000000000"}])";
    }
    soulmod::PackageOperations invalidOperations;
    Require(!soulmod::LoadPackageOperations(invalidPackage, invalidOperations, error), "path traversal in operation paths must be rejected");
    std::filesystem::remove_all(invalidPackage);

    const std::filesystem::path noMirrorPackage = packageRoot / "native-no-mirror-package";
    std::filesystem::create_directories(noMirrorPackage / "assets");
    std::filesystem::copy_file(globalLoad.assetPath, noMirrorPackage / "assets" / "map.json", std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream manifest(noMirrorPackage / "manifest.json");
        manifest << R"({"formatVersion":1,"id":"no.mirror.package","operationsFile":"operations.json"})";
        std::ofstream operationFile(noMirrorPackage / "operations.json");
        operationFile << "[{\"id\":\"no.mirror.load\",\"modId\":\"no.mirror.package\",\"target\":{\"type\":\"global\",\"name\":\"test_global\"},\"operation\":\"global.json.load\",\"selector\":{\"applyAt\":\"first.draw_set_font\"},\"assetPath\":\"assets/map.json\",\"assetSha256\":\"" << globalLoad.assetSha256 << "\"}]";
    }
    soulmod::PackageOperations noMirrorOperations;
    Require(soulmod::LoadPackageOperations(noMirrorPackage, noMirrorOperations, error), "global JSON load without mirrors must parse: " + error);
    Require(noMirrorOperations.globalJsonLoads.size() == 1 && noMirrorOperations.globalJsonLoads.front().mirrorGlobals.empty(), "absent mirrorGlobals must produce an empty mirror vector");
    {
        std::ofstream operationFile(noMirrorPackage / "operations.json");
        operationFile << "[{\"id\":\"invalid.when\",\"modId\":\"no.mirror.package\",\"target\":{\"type\":\"global\",\"name\":\"test_global\"},\"operation\":\"global.json.load\",\"selector\":{\"applyAt\":\"first.draw_set_font\",\"when\":{\"global\":\"invalid.name\",\"equals\":\"selected\"}},\"assetPath\":\"assets/map.json\",\"assetSha256\":\"" << globalLoad.assetSha256 << "\"}]";
    }
    Require(!soulmod::LoadPackageOperations(noMirrorPackage, noMirrorOperations, error), "global JSON load must reject qualified condition globals");
    {
        std::ofstream operationFile(noMirrorPackage / "operations.json");
        operationFile << "[{\"id\":\"invalid.when\",\"modId\":\"no.mirror.package\",\"target\":{\"type\":\"global\",\"name\":\"test_global\"},\"operation\":\"global.json.load\",\"selector\":{\"applyAt\":\"first.draw_set_font\",\"when\":{\"global\":\"condition_global\",\"equals\":1}},\"assetPath\":\"assets/map.json\",\"assetSha256\":\"" << globalLoad.assetSha256 << "\"}]";
    }
    Require(!soulmod::LoadPackageOperations(noMirrorPackage, noMirrorOperations, error), "global JSON load must reject non-string condition values");
    {
        std::ofstream operationFile(noMirrorPackage / "operations.json");
        operationFile << "[{\"id\":\"invalid.when\",\"modId\":\"no.mirror.package\",\"target\":{\"type\":\"global\",\"name\":\"test_global\"},\"operation\":\"global.json.load\",\"selector\":{\"applyAt\":\"first.draw_set_font\",\"when\":{\"global\":\"condition_global\",\"equals\":\"selected\",\"extra\":true}},\"assetPath\":\"assets/map.json\",\"assetSha256\":\"" << globalLoad.assetSha256 << "\"}]";
    }
    Require(!soulmod::LoadPackageOperations(noMirrorPackage, noMirrorOperations, error), "global JSON load must reject unknown condition members");
    std::filesystem::remove_all(noMirrorPackage);

    const soulmod::GlobalJsonLoad applicationLoad = MakeGlobalJsonLoad();
    {
        FakeGlobalJsonLoadHost host;
        soulmod::GlobalJsonLoadEpoch epoch(applicationLoad);
        const soulmod::GlobalJsonLoadApplyOutcome firstAttempt = soulmod::ApplyGlobalJsonLoad(applicationLoad, host, epoch, error);
        Require(firstAttempt.result == soulmod::GlobalJsonLoadApplyResult::NotReady, "absent globals must report global JSON loading not ready");
        Require(epoch.State() == soulmod::GlobalJsonLoadState::Pending, "absent globals must leave global JSON loading pending");
        RequireEvents(host.events, { "ready:primary", "clear" }, "absent globals must only perform readiness checks");
        host.globals = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
        Require(soulmod::ApplyGlobalJsonLoad(applicationLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::Applied, "created globals must allow global JSON loading to apply");
        Require(host.globals["primary"] == "decoded" && host.globals["mirror"] == "decoded", "ready globals must receive decoded data");
        const size_t eventsBeforeThirdAttempt = host.events.size();
        Require(soulmod::ApplyGlobalJsonLoad(applicationLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::AlreadyApplied, "ready global JSON loading must apply exactly once");
        Require(host.events.size() == eventsBeforeThirdAttempt, "already applied global JSON loading must not perform more work");
    }
    {
        FakeGlobalJsonLoadHost host;
        host.globals = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
        soulmod::GlobalJsonLoadEpoch epoch(applicationLoad);
        Require(soulmod::ApplyGlobalJsonLoad(applicationLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::Applied, "global JSON load must apply verified decoded data");
        Require(host.globals["primary"] == "decoded" && host.globals["mirror"] == "decoded", "global JSON load must update target and mirrors");
        RequireEvents(host.events, { "ready:primary", "ready:mirror", "read", "decode", "snapshot:primary", "snapshot:mirror", "write:primary", "write:mirror", "clear" }, "global JSON load must check readiness before verification, decoding, snapshots, and writes");
        Require(soulmod::ApplyGlobalJsonLoad(applicationLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::AlreadyApplied, "successful global JSON load must be idempotent");
        Require(host.events.size() == 9, "idempotent global JSON load must not write twice");
    }
    {
        FakeGlobalJsonLoadHost host;
        host.decodeSucceeds = false;
        host.globals = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
        soulmod::GlobalJsonLoadEpoch epoch(applicationLoad);
        Require(soulmod::ApplyGlobalJsonLoad(applicationLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::TerminalFailure, "decode failure must be terminal");
        Require(host.globals["primary"] == "before-primary" && host.globals["mirror"] == "before-mirror", "decode failure must not write globals");
        RequireEvents(host.events, { "ready:primary", "ready:mirror", "read", "decode", "clear" }, "decode failure must stop before snapshots and writes");
        Require(soulmod::ApplyGlobalJsonLoad(applicationLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::TerminalFailure, "terminal global JSON failure must not retry");
    }
    {
        FakeGlobalJsonLoadHost host;
        host.failWriteName = "mirror";
        host.globals = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
        soulmod::GlobalJsonLoadEpoch epoch(applicationLoad);
        Require(soulmod::ApplyGlobalJsonLoad(applicationLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::TerminalFailure, "partial write failure must be terminal");
        Require(host.globals["primary"] == "before-primary" && host.globals["mirror"] == "before-mirror", "partial write failure must roll back prior writes");
        RequireEvents(host.events, { "ready:primary", "ready:mirror", "read", "decode", "snapshot:primary", "snapshot:mirror", "write:primary", "write:mirror", "restore:primary", "clear" }, "partial write failure must restore prior writes in reverse order");
    }
    {
        const soulmod::GlobalJsonLoad conditionalLoad = MakeGlobalJsonLoad(soulmod::GlobalJsonLoadCondition{ "lang", "ja" });
        FakeGlobalJsonLoadHost host;
        host.globals = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
        host.conditionGlobals["lang"] = "en";
        soulmod::GlobalJsonLoadEpoch epoch(conditionalLoad);
        const soulmod::GlobalJsonLoadApplyOutcome falseAttempt = soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error);
        Require(falseAttempt.result == soulmod::GlobalJsonLoadApplyResult::ConditionNotMatched, "false condition must leave its epoch inactive");
        Require(epoch.State() == soulmod::GlobalJsonLoadState::Inactive, "false condition must keep the epoch inactive");
        Require(host.globals["primary"] == "before-primary" && host.globals["mirror"] == "before-mirror", "false condition must leave stock globals authoritative");
        RequireEvents(host.events, { "condition:lang" }, "false condition must not perform target work");

        host.events.clear();
        host.conditionGlobals["lang"] = "ja";
        const soulmod::GlobalJsonLoadApplyOutcome firstApply = soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error);
        Require(firstApply.epochStarted && firstApply.result == soulmod::GlobalJsonLoadApplyResult::Applied, "matching condition must start and apply an epoch");
        RequireEvents(host.events, { "condition:lang", "ready:primary", "ready:mirror", "read", "decode", "snapshot:primary", "snapshot:mirror", "write:primary", "write:mirror", "clear" }, "first matching condition must apply exactly once");

        host.events.clear();
        Require(soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::AlreadyApplied, "stable matching condition must stay applied");
        RequireEvents(host.events, { "condition:lang" }, "stable applied condition must not repeat target work");

        host.events.clear();
        host.conditionGlobals["lang"] = "en";
        const soulmod::GlobalJsonLoadApplyOutcome deactivation = soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error);
        Require(deactivation.deactivated && deactivation.result == soulmod::GlobalJsonLoadApplyResult::ConditionDeactivated, "false transition must deactivate the epoch");
        Require(host.globals["primary"] == "decoded" && host.globals["mirror"] == "decoded", "condition deactivation must not restore stale snapshots");
        RequireEvents(host.events, { "condition:lang" }, "condition deactivation must not restore or clear target state");

        host.events.clear();
        host.conditionGlobals["lang"] = "ja";
        const soulmod::GlobalJsonLoadApplyOutcome reapplied = soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error);
        Require(reapplied.epochStarted && reapplied.result == soulmod::GlobalJsonLoadApplyResult::Applied, "matching condition after deactivation must start one new epoch");
        RequireEvents(host.events, { "condition:lang", "ready:primary", "ready:mirror", "read", "decode", "snapshot:primary", "snapshot:mirror", "write:primary", "write:mirror", "clear" }, "new matching epoch must apply exactly once");
    }
    {
        const soulmod::GlobalJsonLoad conditionalLoad = MakeGlobalJsonLoad(soulmod::GlobalJsonLoadCondition{ "lang", "ja" });
        FakeGlobalJsonLoadHost host;
        host.globals = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
        soulmod::GlobalJsonLoadEpoch epoch(conditionalLoad);
        Require(soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::ConditionNotMatched, "missing condition global must leave its epoch inactive");
        RequireEvents(host.events, { "condition:lang" }, "missing condition global must not touch targets");
        host.events.clear();
        host.conditionGlobals["lang"] = "ja";
        Require(soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::Applied, "missing condition global must remain retryable when it appears");
    }
    {
        const soulmod::GlobalJsonLoad conditionalLoad = MakeGlobalJsonLoad(soulmod::GlobalJsonLoadCondition{ "lang", "ja" });
        FakeGlobalJsonLoadHost host;
        host.globals = { { "primary", "before-primary" }, { "mirror", "before-mirror" } };
        host.conditionGlobals["lang"] = "ja";
        soulmod::GlobalJsonLoadEpoch epoch(conditionalLoad);
        Require(soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::Applied, "matching condition must apply before condition-value failure");
        host.events.clear();
        host.conditionReadFails = true;
        const soulmod::GlobalJsonLoadApplyOutcome failure = soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error);
        Require(failure.failureStarted && failure.result == soulmod::GlobalJsonLoadApplyResult::TerminalFailure, "invalid condition value must fail the active epoch");
        Require(epoch.State() == soulmod::GlobalJsonLoadState::Failed, "invalid condition value must mark the active epoch failed");
        RequireEvents(host.events, { "condition:lang" }, "invalid condition value must not perform target work");
        host.events.clear();
        host.conditionReadFails = false;
        Require(soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::TerminalFailure, "stable matching condition must not retry a failed epoch");
        RequireEvents(host.events, { "condition:lang" }, "stable failed condition must not repeat target work");
        host.events.clear();
        host.conditionGlobals["lang"] = "en";
        Require(soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error).result == soulmod::GlobalJsonLoadApplyResult::ConditionDeactivated, "false condition must reset a failed epoch");
        RequireEvents(host.events, { "condition:lang" }, "failed epoch deactivation must not restore snapshots");
        host.events.clear();
        host.conditionGlobals["lang"] = "ja";
        const soulmod::GlobalJsonLoadApplyOutcome reapplied = soulmod::ApplyGlobalJsonLoad(conditionalLoad, host, epoch, error);
        Require(reapplied.epochStarted && reapplied.result == soulmod::GlobalJsonLoadApplyResult::Applied, "reset failed epoch must apply once on the next matching condition");
    }
    {
        const std::array allInactive{ soulmod::GlobalJsonLoadState::Inactive, soulmod::GlobalJsonLoadState::Inactive };
        const std::array oneApplied{ soulmod::GlobalJsonLoadState::Applied };
        const std::array appliedAndInactive{ soulmod::GlobalJsonLoadState::Applied, soulmod::GlobalJsonLoadState::Inactive };
        const std::array appliedAndPending{ soulmod::GlobalJsonLoadState::Applied, soulmod::GlobalJsonLoadState::Pending };
        const std::array appliedAndFailed{ soulmod::GlobalJsonLoadState::Applied, soulmod::GlobalJsonLoadState::Failed };
        Require(!soulmod::AreSelectedGlobalJsonLoadsApplied(allInactive), "inactive optional loads must not select a restart epoch");
        Require(soulmod::AreSelectedGlobalJsonLoadsApplied(oneApplied), "one applied selected load must be restart-ready");
        Require(soulmod::AreSelectedGlobalJsonLoadsApplied(appliedAndInactive), "inactive optional loads must not block an applied selected load");
        Require(!soulmod::AreSelectedGlobalJsonLoadsApplied(appliedAndPending), "pending selected loads must block restart readiness");
        Require(!soulmod::AreSelectedGlobalJsonLoadsApplied(appliedAndFailed), "failed selected loads must block restart readiness");
    }
    {
        soulmod::RestartCoordinator coordinator;
        Require(!coordinator.TryClaimRestart(true), "current JSON readiness must not latch before font readiness");
        coordinator.MarkFontReady();
        Require(!coordinator.TryClaimRestart(false), "stale JSON readiness must not permit restart after deactivation");
        Require(coordinator.TryClaimRestart(true), "current JSON and font readiness must permit restart");
        Require(!coordinator.TryClaimRestart(true), "restart must be claimed exactly once");
    }
    {
        soulmod::RestartCoordinator coordinator;
        coordinator.MarkFontReady();
        Require(!coordinator.TryClaimRestart(false), "font readiness alone must not permit restart");
        coordinator.MarkFontReady();
        Require(coordinator.TryClaimRestart(true), "repeated font readiness must remain restartable when JSON is currently ready");
        Require(!coordinator.TryClaimRestart(true), "repeated readiness and claims must not permit a second restart");
    }
    {
        soulmod::RestartCoordinator coordinator1;
        coordinator1.MarkFontReady();
        Require(coordinator1.TryClaimRestart(true), "first coordinator must allow restart");

        soulmod::RestartCoordinator coordinator2;
        coordinator2.MarkFontReady();
        Require(coordinator2.TryClaimRestart(true), "fresh coordinator must allow independent restart");
    }

    std::cout << "PASS: RuntimeOperations config, global JSON loading, path matching, and font selection\n";
}
