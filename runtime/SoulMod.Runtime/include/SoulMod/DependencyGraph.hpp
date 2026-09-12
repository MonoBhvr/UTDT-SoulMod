#pragma once

#include "SoulMod/ModPackage.hpp"
#include <vector>
#include <string>

namespace SoulMod {

enum class DependencyResolutionOutcome {
    Resolved,
    MissingDependency,
    Cycle,
    DuplicatePackageId,
};

struct DependencyIssue {
    DependencyResolutionOutcome outcome;
    std::string packageId;
    std::string relatedPackageId;
    std::string reason;
};

struct DependencyResolution {
    DependencyResolutionOutcome outcome = DependencyResolutionOutcome::Resolved;
    std::vector<LoadedModPackage> packages;
    std::vector<DependencyIssue> issues;
    bool success = true;
};

class DependencyGraph {
public:
    static DependencyResolution BuildPlan(const std::vector<LoadedModPackage>& packages);
    static bool SortModPackages(std::vector<LoadedModPackage>& packages, std::string& outErrorMessage);
};

} // namespace SoulMod
