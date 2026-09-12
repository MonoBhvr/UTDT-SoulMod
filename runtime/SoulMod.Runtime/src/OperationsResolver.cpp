#include "SoulMod/OperationsResolver.hpp"

#include "SoulMod/DependencyGraph.hpp"
#include "SoulMod/FontManager.hpp"
#include "SoulMod/HookManager.hpp"
#include "SoulMod/Logger.hpp"
#include "SoulMod/StringManager.hpp"
#include "SoulMod/YYToolkitAdapter.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <utility>

namespace SoulMod {
namespace {

ResolutionClass Classify(const std::string& operationType) {
    if (operationType == "string.replace" || operationType == "string.add") return ResolutionClass::MergeableString;
    if (operationType == "hook.before" || operationType == "hook.after") return ResolutionClass::OrderedHook;
    if (operationType == "font.redirect" || operationType == "font.register" || operationType == "resource.redirect" || operationType == "global.json.load") return ResolutionClass::SoftProperty;
    if (operationType == "hook.replace" || operationType == "code.replace" || operationType == "resource.replace" || operationType == "sprite.replace" || operationType == "sprite.frame.replace" || operationType == "texture.replace" || operationType == "audio.replace" || operationType == "call.argument.replace") return ResolutionClass::HardReplacement;
    return ResolutionClass::UnsupportedMerge;
}

bool IsExecutable(const std::string& operationType) {
    return operationType == "string.replace" || operationType == "string.add" || operationType == "font.redirect" ||
        operationType == "font.register" || operationType == "resource.redirect" || operationType == "global.json.load" ||
        operationType == "hook.before" || operationType == "hook.after" || operationType == "hook.replace";
}

std::string TargetKey(const ModOperation& operation) {
    return operation.target.type + ":" + operation.target.name;
}

std::string ExpectedValue(const ModOperation& operation) {
    return operation.precondition && operation.precondition->expectedValue ? *operation.precondition->expectedValue : operation.expectedValue;
}

bool IsValid(const ModOperation& operation) {
    if (operation.id.empty() || operation.modId.empty() || operation.target.type.empty() || operation.target.name.empty() || operation.reads.empty() || operation.writes.empty()) return false;
    if (operation.operationType == "string.replace" && (!operation.precondition || !operation.precondition->expectedValue)) return false;
    return true;
}

void AddIssue(ResolutionReport& report, ResolutionOutcome outcome, ResolutionClass classification,
    const ResolvedOperation& current, const ResolvedOperation& first, const std::string& targetKey, const std::string& reason) {
    ++report.directConflicts;
    report.issues.push_back({ outcome, classification, current.packageId, current.operation.id, first.packageId, first.operation.id, targetKey, reason });
}

void ApplyOperation(const ResolvedOperation& planned, ResolutionReport& report) {
    const ModOperation& operation = planned.operation;
    if (operation.operationType == "string.replace" || operation.operationType == "string.add") {
        StringManager::Instance().RegisterStringReplacement(operation.target.name, operation.value, operation.modId);
        ++report.stringsRegistered;
    } else if (operation.operationType == "font.redirect") {
        FontManager::Instance().RegisterFontRedirect(operation.target.name, operation.value, operation.modId);
        ++report.fontsRegistered;
    } else if (operation.operationType == "resource.redirect") {
        YYToolkitAdapter::Instance().RegisterFileRedirect(operation.target.name, std::filesystem::path(planned.packagePath) / operation.assetPath);
    } else if (operation.operationType == "font.register") {
        FontManager::Instance().RegisterVirtualFont(operation.target.name, (std::filesystem::path(planned.packagePath) / operation.assetPath).string(), operation.modId,
            operation.fontSize, operation.fontBold, operation.fontItalic, operation.fontFirst, operation.fontLast);
        ++report.fontsRegistered;
    } else if (operation.operationType == "hook.before") {
        HookManager::Instance().RegisterBeforeHook(operation.target.name, operation.modId, operation.priority, nullptr);
    } else if (operation.operationType == "hook.after") {
        HookManager::Instance().RegisterAfterHook(operation.target.name, operation.modId, operation.priority, nullptr);
    } else if (operation.operationType == "hook.replace") {
        HookManager::Instance().RegisterReplaceHook(operation.target.name, operation.modId, operation.priority, nullptr);
    }
}
}

OperationsResolver& OperationsResolver::Instance() {
    static OperationsResolver instance;
    return instance;
}

ResolutionReport OperationsResolver::ResolvePackages(const std::vector<LoadedModPackage>& packages) {
    ResolutionReport report;
    const DependencyResolution dependencies = DependencyGraph::BuildPlan(packages);
    if (!dependencies.success) {
        const DependencyIssue& issue = dependencies.issues.front();
        report.success = false;
        report.outcome = issue.outcome == DependencyResolutionOutcome::MissingDependency ? ResolutionOutcome::MissingDependency :
            issue.outcome == DependencyResolutionOutcome::Cycle ? ResolutionOutcome::Cycle : ResolutionOutcome::DuplicatePackageId;
        report.issues.push_back({ report.outcome, ResolutionClass::DependencyProblem, issue.packageId, {}, issue.relatedPackageId, {}, {}, issue.reason });
        return report;
    }

    report.orderedPackages = dependencies.packages;
    report.totalModsLoaded = report.orderedPackages.size();
    std::set<std::string> operationIds;
    std::map<std::string, ResolvedOperation> firstWriters;

    for (const LoadedModPackage& package : report.orderedPackages) {
        for (const ModOperation& operation : package.operations) {
            ++report.totalOperations;
            const ResolvedOperation current { package.manifest.id, package.manifest.packagePath, operation, Classify(operation.operationType) };
            if (!IsValid(operation)) {
                report.success = false;
                report.outcome = ResolutionOutcome::InvalidOperation;
                report.issues.push_back({ report.outcome, ResolutionClass::InvalidOperation, current.packageId, current.operation.id, {}, {}, TargetKey(operation), "missing required typed operation metadata" });
                continue;
            }
            if (!operationIds.insert(operation.id).second) {
                report.success = false;
                report.outcome = ResolutionOutcome::DuplicateOperationId;
                report.issues.push_back({ report.outcome, ResolutionClass::InvalidOperation, current.packageId, current.operation.id, {}, {}, TargetKey(operation), "duplicate enabled operation id" });
                continue;
            }

            bool duplicate = false;
            for (const auto& [key, first] : firstWriters) {
                if (std::find(operation.writes.begin(), operation.writes.end(), key) != operation.writes.end()) {
                    if (current.classification == ResolutionClass::OrderedHook && first.classification == ResolutionClass::OrderedHook) continue;
                    if (current.classification == ResolutionClass::MergeableString && first.classification == ResolutionClass::MergeableString) {
                        if (ExpectedValue(first.operation) != ExpectedValue(operation)) {
                            report.success = false; report.outcome = ResolutionOutcome::Conflict;
                            AddIssue(report, report.outcome, ResolutionClass::PreconditionConflict, current, first, key, "conflicting string preconditions");
                        } else if (first.operation.value == operation.value) {
                            duplicate = true;
                        } else {
                            report.success = false; report.outcome = ResolutionOutcome::Conflict;
                            AddIssue(report, report.outcome, ResolutionClass::HardReplacement, current, first, key, "conflicting string replacement values");
                        }
                    } else {
                        report.success = false; report.outcome = ResolutionOutcome::Conflict;
                        AddIssue(report, report.outcome, ResolutionClass::HardReplacement, current, first, key, "exclusive write target already claimed");
                    }
                } else if (std::find(operation.reads.begin(), operation.reads.end(), key) != operation.reads.end()) {
                    report.success = false; report.outcome = ResolutionOutcome::Conflict;
                    AddIssue(report, report.outcome, ResolutionClass::ReadWriteConflict, current, first, key, "operation reads a key written by another enabled operation");
                }
            }
            if (!report.success) continue;
            if (duplicate) {
                ++report.duplicatesResolved;
                continue;
            }
            report.plan.push_back(current);
            for (const std::string& key : operation.writes) firstWriters.emplace(key, current);
        }
    }

    if (report.success) {
        const auto unsupported = std::find_if(report.plan.begin(), report.plan.end(), [](const ResolvedOperation& planned) {
            return !IsExecutable(planned.operation.operationType);
        });
        if (unsupported != report.plan.end()) {
            report.success = false;
            report.outcome = ResolutionOutcome::UnsupportedMerge;
            report.issues.push_back({ report.outcome, ResolutionClass::UnsupportedMerge, unsupported->packageId, unsupported->operation.id, {}, {}, TargetKey(unsupported->operation), "operation has no runtime merge implementation" });
        }
    }
    if (!report.success) report.plan.clear();
    return report;
}

ResolutionReport OperationsResolver::ApplyModPackages(const std::vector<LoadedModPackage>& packages) {
    ResolutionReport report = ResolvePackages(packages);
    if (!report.success) {
        for (const ResolutionIssue& issue : report.issues) Logger::Instance().Error("Resolution failed for " + issue.packageId + "/" + issue.operationId + ": " + issue.reason);
        return report;
    }
    for (const ResolvedOperation& planned : report.plan) ApplyOperation(planned, report);
    Logger::Instance().Info("Resolution complete: " + std::to_string(report.totalModsLoaded) + " packages, " + std::to_string(report.plan.size()) + " planned operations.");
    return report;
}
}
