// DependencyGraph contract tests — topological load-order resolver.
//
// Exercises SoulMod::DependencyGraph::SortModPackages over the promoted
// LoadedModPackage graph: explicit dependencies, loadBefore/loadAfter rules,
// missing dependencies, and circular-dependency rejection. Windows-lane only
// (transitively links Logger, which consumes the Win32 API).

#include "SoulMod/DependencyGraph.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int g_failureCount = 0;

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failureCount;
        }
    }

    SoulMod::LoadedModPackage MakePackage(const std::string& id)
    {
        SoulMod::LoadedModPackage pkg;
        pkg.manifest.id = id;
        return pkg;
    }

    SoulMod::LoadedModPackage MakePackageWithDeps(
        const std::string& id,
        const std::vector<std::string>& dependencies)
    {
        SoulMod::LoadedModPackage pkg = MakePackage(id);
        pkg.manifest.dependencies = dependencies;
        return pkg;
    }

    std::vector<std::string> IdsInOrder(const std::vector<SoulMod::LoadedModPackage>& packages)
    {
        std::vector<std::string> ids;
        ids.reserve(packages.size());
        for (const auto& pkg : packages)
            ids.push_back(pkg.manifest.id);
        return ids;
    }
}

// Given: an empty package set
// When:  SortModPackages is called
// Then:  it succeeds (no ordering work to do) without error.
void Test_empty_set_succeeds()
{
    std::vector<SoulMod::LoadedModPackage> packages;
    std::string error;
    Require(SoulMod::DependencyGraph::SortModPackages(packages, error), "empty set must succeed");
    Require(error.empty(), "empty set must not report an error");
}

// Given: a single package
// When:  SortModPackages is called
// Then:  it succeeds without reordering.
void Test_single_package_succeeds()
{
    std::vector<SoulMod::LoadedModPackage> packages = { MakePackage("only") };
    std::string error;
    Require(SoulMod::DependencyGraph::SortModPackages(packages, error), "single package must succeed");
    Require(packages.size() == 1 && packages.front().manifest.id == "only", "single package must be preserved");
}

// Given: B depends on A (declared in reverse input order)
// When:  SortModPackages resolves the order
// Then:  A precedes B in the sorted output.
void Test_explicit_dependency_orders_before_dependent()
{
    std::vector<SoulMod::LoadedModPackage> packages = {
        MakePackageWithDeps("b", { "a" }),
        MakePackage("a"),
    };
    std::string error;
    Require(SoulMod::DependencyGraph::SortModPackages(packages, error), "dependency must resolve");
    Require(error.empty(), "valid dependency must not error");
    const auto ids = IdsInOrder(packages);
    Require(ids == std::vector<std::string>{ "a", "b" }, "dependency must load before its dependent");
}

// Given: A declares loadAfter = [B]
// When:  SortModPackages resolves the order
// Then:  B precedes A.
void Test_load_after_orders_target_first()
{
    std::vector<SoulMod::LoadedModPackage> packages = {
        MakePackage("a"),
        MakePackage("b"),
    };
    packages[0].manifest.loadAfter.push_back("b");
    std::string error;
    Require(SoulMod::DependencyGraph::SortModPackages(packages, error), "loadAfter must resolve");
    const auto ids = IdsInOrder(packages);
    Require(ids == std::vector<std::string>{ "b", "a" }, "loadAfter target must load first");
}

// Given: A declares loadBefore = [B]
// When:  SortModPackages resolves the order
// Then:  A precedes B.
void Test_load_before_orders_subject_first()
{
    std::vector<SoulMod::LoadedModPackage> packages = {
        MakePackage("a"),
        MakePackage("b"),
    };
    packages[0].manifest.loadBefore.push_back("b");
    std::string error;
    Require(SoulMod::DependencyGraph::SortModPackages(packages, error), "loadBefore must resolve");
    const auto ids = IdsInOrder(packages);
    Require(ids == std::vector<std::string>{ "a", "b" }, "loadBefore subject must load first");
}

// Given: B depends on a missing mod "ghost"
// When:  SortModPackages resolves the order
// Then:  it fails with a message naming the missing dependency.
void Test_missing_dependency_is_rejected()
{
    std::vector<SoulMod::LoadedModPackage> packages = {
        MakePackageWithDeps("b", { "ghost" }),
    };
    std::string error;
    Require(!SoulMod::DependencyGraph::SortModPackages(packages, error), "missing dependency must fail");
    Require(error.find("Missing required dependency") != std::string::npos, "error must name the missing dependency");
    const SoulMod::DependencyResolution resolution = SoulMod::DependencyGraph::BuildPlan(packages);
    Require(resolution.outcome == SoulMod::DependencyResolutionOutcome::MissingDependency, "missing dependency must be structured");
    Require(resolution.issues.size() == 1 && resolution.issues[0].relatedPackageId == "ghost", "missing dependency report must identify the requirement");
}

// Given: A depends on B and B depends on A (cycle)
// When:  SortModPackages resolves the order
// Then:  it fails with a circular-dependency message.
void Test_circular_dependency_is_rejected()
{
    std::vector<SoulMod::LoadedModPackage> packages = {
        MakePackageWithDeps("a", { "b" }),
        MakePackageWithDeps("b", { "a" }),
    };
    std::string error;
    Require(!SoulMod::DependencyGraph::SortModPackages(packages, error), "circular dependency must fail");
    Require(error.find("Circular") != std::string::npos, "error must name the circular dependency");
    const SoulMod::DependencyResolution resolution = SoulMod::DependencyGraph::BuildPlan(packages);
    Require(resolution.outcome == SoulMod::DependencyResolutionOutcome::Cycle, "cycle must be structured");
}

void Test_independent_packages_use_lexical_tie_break()
{
    const std::vector<SoulMod::LoadedModPackage> packages = {
        MakePackage("zeta"),
        MakePackage("alpha"),
        MakePackage("middle"),
    };
    const SoulMod::DependencyResolution resolution = SoulMod::DependencyGraph::BuildPlan(packages);
    Require(resolution.success, "independent packages must resolve");
    Require(IdsInOrder(resolution.packages) == std::vector<std::string>{ "alpha", "middle", "zeta" },
        "independent packages must have lexical tie-break order");
}

void Test_duplicate_package_id_is_rejected()
{
    const SoulMod::DependencyResolution resolution = SoulMod::DependencyGraph::BuildPlan({ MakePackage("same"), MakePackage("same") });
    Require(!resolution.success && resolution.outcome == SoulMod::DependencyResolutionOutcome::DuplicatePackageId,
        "duplicate enabled package ids must not be silently overwritten");
}

int main()
{
    Test_empty_set_succeeds();
    Test_single_package_succeeds();
    Test_explicit_dependency_orders_before_dependent();
    Test_load_after_orders_target_first();
    Test_load_before_orders_subject_first();
    Test_missing_dependency_is_rejected();
    Test_circular_dependency_is_rejected();
    Test_independent_packages_use_lexical_tie_break();
    Test_duplicate_package_id_is_rejected();

    if (g_failureCount != 0)
    {
        std::cerr << "RESULT: FAIL (" << g_failureCount << " assertion(s) failed)\n";
        return 1;
    }
    std::cout << "RESULT: PASS (DependencyGraph topological load-order contract)\n";
    return 0;
}
