#include "SoulMod/OperationsResolver.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
int failures = 0;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct OperationSpec {
    std::string id;
    std::string type;
    std::string key;
    std::string value;
    std::string expected;
};

SoulMod::ModOperation MakeOperation(const OperationSpec& spec) {
    SoulMod::ModOperation operation;
    operation.id = spec.id;
    operation.operationType = spec.type;
    operation.target = { spec.key.substr(0, spec.key.find(':')), spec.key.substr(spec.key.find(':') + 1) };
    operation.value = spec.value;
    operation.reads = { spec.key };
    operation.writes = { spec.key };
    if (!spec.expected.empty()) operation.precondition = SoulMod::OperationPrecondition{ spec.expected, {} };
    return operation;
}

SoulMod::LoadedModPackage MakePackage(std::string id, std::vector<SoulMod::ModOperation> operations) {
    SoulMod::LoadedModPackage package;
    package.manifest.id = std::move(id);
    for (SoulMod::ModOperation& operation : operations) operation.modId = package.manifest.id;
    package.operations = std::move(operations);
    return package;
}

SoulMod::ResolutionReport Resolve(std::vector<SoulMod::LoadedModPackage> packages) {
    return SoulMod::OperationsResolver::ResolvePackages(packages);
}

void Test_mergeable_strings_build_a_plan() {
    const auto report = Resolve({
        MakePackage("beta", { MakeOperation({ "beta.text", "string.replace", "string:beta", "new-beta", "old-beta" }) }),
        MakePackage("alpha", { MakeOperation({ "alpha.text", "string.replace", "string:alpha", "new-alpha", "old-alpha" }) }),
    });
    Require(report.success, "independent string changes must resolve");
    Require(report.plan.size() == 2, "both string changes must be planned");
    Require(report.plan[0].packageId == "alpha" && report.plan[1].packageId == "beta", "independent packages must use lexical order");
    Require(report.plan[0].classification == SoulMod::ResolutionClass::MergeableString, "string changes must be classified as mergeable");
}

void Test_ordered_hooks_are_planned_in_package_order() {
    const auto report = Resolve({
        MakePackage("zeta", { MakeOperation({ "zeta.before", "hook.before", "hook:draw", {}, {} }) }),
        MakePackage("alpha", { MakeOperation({ "alpha.after", "hook.after", "hook:draw", {}, {} }) }),
    });
    Require(report.success, "ordered hooks must resolve");
    Require(report.plan.size() == 2 && report.plan[0].packageId == "alpha", "hooks must inherit deterministic package order");
    Require(report.plan[0].classification == SoulMod::ResolutionClass::OrderedHook, "hooks must be classified as ordered");
}

void Test_soft_properties_are_mergeable_when_keys_differ() {
    const auto report = Resolve({
        MakePackage("fonts", { MakeOperation({ "fonts.main", "font.redirect", "font:main", "virtual-main", {} }) }),
        MakePackage("files", { MakeOperation({ "files.data", "resource.redirect", "file:data.win", "assets/data.win", {} }) }),
    });
    Require(report.success, "independent soft properties must resolve");
    Require(report.plan[0].classification == SoulMod::ResolutionClass::SoftProperty, "font redirect must be a soft property");
    Require(report.plan[1].classification == SoulMod::ResolutionClass::SoftProperty, "file redirect must be a soft property");
}

void Test_conflicting_preconditions_fail_closed() {
    const auto report = Resolve({
        MakePackage("first", { MakeOperation({ "first.text", "string.replace", "string:greeting", "one", "old-one" }) }),
        MakePackage("second", { MakeOperation({ "second.text", "string.replace", "string:greeting", "two", "old-two" }) }),
    });
    Require(!report.success && report.outcome == SoulMod::ResolutionOutcome::Conflict, "precondition mismatch must fail resolution");
    Require(report.issues.size() == 1 && report.issues[0].classification == SoulMod::ResolutionClass::PreconditionConflict, "precondition conflict must be actionable");
}

void Test_exclusive_and_hard_replacements_fail_closed() {
    const auto exclusive = Resolve({
        MakePackage("first", { MakeOperation({ "first.replace", "hook.replace", "hook:draw", {}, {} }) }),
        MakePackage("second", { MakeOperation({ "second.replace", "hook.replace", "hook:draw", {}, {} }) }),
    });
    Require(!exclusive.success && exclusive.issues[0].classification == SoulMod::ResolutionClass::HardReplacement, "same exclusive hook target must conflict");

    const auto hard = Resolve({
        MakePackage("first", { MakeOperation({ "first.resource", "resource.replace", "file:data.win", "one", {} }) }),
        MakePackage("second", { MakeOperation({ "second.resource", "resource.replace", "file:data.win", "two", {} }) }),
    });
    Require(!hard.success && hard.issues[0].classification == SoulMod::ResolutionClass::HardReplacement, "hard replacements must never choose a winner");
}

void Test_unsupported_and_duplicate_operations_are_rejected() {
    const auto unsupported = Resolve({ MakePackage("code", { MakeOperation({ "code.replace", "code.replace", "code:draw", "new", {} }) }) });
    Require(!unsupported.success && unsupported.outcome == SoulMod::ResolutionOutcome::UnsupportedMerge, "unsupported operation must fail closed");

    const auto duplicate = Resolve({
        MakePackage("first", { MakeOperation({ "shared.id", "string.replace", "string:first", "new", "old" }) }),
        MakePackage("second", { MakeOperation({ "shared.id", "string.replace", "string:second", "new", "old" }) }),
    });
    Require(!duplicate.success && duplicate.outcome == SoulMod::ResolutionOutcome::DuplicateOperationId, "duplicate operation ids must be rejected");
}

void Test_read_write_overlap_and_invalid_preconditions_are_rejected() {
    SoulMod::ModOperation reader = MakeOperation({ "reader.text", "string.replace", "string:reader", "new", "old" });
    reader.reads = { "string:writer" };
    const auto overlap = Resolve({
        MakePackage("alpha", { MakeOperation({ "writer.text", "string.replace", "string:writer", "new", "old" }) }),
        MakePackage("beta", { reader }),
    });
    Require(!overlap.success && overlap.issues[0].classification == SoulMod::ResolutionClass::ReadWriteConflict, "read/write overlap must fail closed");

    const auto missing = Resolve({ MakePackage("broken", { MakeOperation({ "broken.text", "string.replace", "string:broken", "new", {} }) }) });
    Require(!missing.success && missing.outcome == SoulMod::ResolutionOutcome::InvalidOperation, "missing typed string precondition must be rejected");
}
}

int main() {
    Test_mergeable_strings_build_a_plan();
    Test_ordered_hooks_are_planned_in_package_order();
    Test_soft_properties_are_mergeable_when_keys_differ();
    Test_conflicting_preconditions_fail_closed();
    Test_exclusive_and_hard_replacements_fail_closed();
    Test_unsupported_and_duplicate_operations_are_rejected();
    Test_read_write_overlap_and_invalid_preconditions_are_rejected();
    if (failures != 0) return 1;
    std::cout << "RESULT: PASS (Operations resolver plan contract)\n";
    return 0;
}
