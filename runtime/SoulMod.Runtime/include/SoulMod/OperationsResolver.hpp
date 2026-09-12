#pragma once

#include "SoulMod/ModPackage.hpp"
#include <vector>
#include <string>

namespace SoulMod {

enum class ResolutionOutcome {
    Resolved,
    MissingDependency,
    Cycle,
    DuplicatePackageId,
    DuplicateOperationId,
    InvalidOperation,
    Conflict,
    UnsupportedMerge,
};

enum class ResolutionClass {
    DependencyProblem,
    InvalidOperation,
    MergeableString,
    OrderedHook,
    SoftProperty,
    HardReplacement,
    PreconditionConflict,
    ReadWriteConflict,
    UnsupportedMerge,
};

struct ResolvedOperation {
    std::string packageId;
    std::string packagePath;
    ModOperation operation;
    ResolutionClass classification;
};

struct ResolutionIssue {
    ResolutionOutcome outcome;
    ResolutionClass classification;
    std::string packageId;
    std::string operationId;
    std::string otherPackageId;
    std::string otherOperationId;
    std::string targetKey;
    std::string reason;
};

struct ResolutionReport {
    size_t totalModsLoaded = 0;
    size_t totalOperations = 0;
    size_t duplicatesResolved = 0;
    size_t directConflicts = 0;
    size_t stringsRegistered = 0;
    size_t fontsRegistered = 0;
    bool success = true;
    ResolutionOutcome outcome = ResolutionOutcome::Resolved;
    std::vector<LoadedModPackage> orderedPackages;
    std::vector<ResolvedOperation> plan;
    std::vector<ResolutionIssue> issues;
};

class OperationsResolver {
public:
    static OperationsResolver& Instance();

    static ResolutionReport ResolvePackages(const std::vector<LoadedModPackage>& packages);
    ResolutionReport ApplyModPackages(const std::vector<LoadedModPackage>& packages);

private:
    OperationsResolver() = default;
    ~OperationsResolver() = default;
};

} // namespace SoulMod
