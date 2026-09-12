#include "SoulMod/DependencyGraph.hpp"
#include "SoulMod/Logger.hpp"
#include <map>
#include <set>
#include <sstream>

namespace SoulMod {

DependencyResolution DependencyGraph::BuildPlan(const std::vector<LoadedModPackage>& packages) {
    DependencyResolution result;
    std::map<std::string, LoadedModPackage> modMap;
    std::map<std::string, int> inDegree;
    std::map<std::string, std::set<std::string>> adjList;

    for (const auto& pkg : packages) {
        if (!modMap.emplace(pkg.manifest.id, pkg).second) {
            result.outcome = DependencyResolutionOutcome::DuplicatePackageId;
            result.success = false;
            result.issues.push_back({ result.outcome, pkg.manifest.id, pkg.manifest.id, "duplicate enabled package id" });
            return result;
        }
        inDegree[pkg.manifest.id] = 0;
        adjList[pkg.manifest.id] = {};
    }

    for (const auto& pkg : packages) {
        std::string currentId = pkg.manifest.id;
        for (const auto& dep : pkg.manifest.dependencies) {
            if (modMap.find(dep) == modMap.end()) {
                result.outcome = DependencyResolutionOutcome::MissingDependency;
                result.success = false;
                result.issues.push_back({ result.outcome, currentId, dep, "Missing required dependency" });
                return result;
            }
            adjList[dep].insert(currentId);
        }
        for (const auto& afterMod : pkg.manifest.loadAfter) {
            if (modMap.find(afterMod) != modMap.end()) {
                adjList[afterMod].insert(currentId);
            }
        }
        for (const auto& beforeMod : pkg.manifest.loadBefore) {
            if (modMap.find(beforeMod) != modMap.end()) {
                adjList[currentId].insert(beforeMod);
            }
        }
    }

    for (const auto& [node, edges] : adjList) {
        for (const auto& neighbor : edges) {
            inDegree[neighbor]++;
        }
    }

    std::set<std::string> ready;
    for (const auto& [id, count] : inDegree) {
        if (count == 0) ready.insert(id);
    }

    while (!ready.empty()) {
        const std::string current = *ready.begin();
        ready.erase(ready.begin());
        result.packages.push_back(modMap.at(current));

        for (const auto& neighbor : adjList[current]) {
            inDegree[neighbor]--;
            if (inDegree[neighbor] == 0) ready.insert(neighbor);
        }
    }

    if (result.packages.size() != packages.size()) {
        result.outcome = DependencyResolutionOutcome::Cycle;
        result.success = false;
        result.issues.push_back({ result.outcome, {}, {}, "Circular dependency or load-order edge" });
        result.packages.clear();
        return result;
    }

    return result;
}

bool DependencyGraph::SortModPackages(std::vector<LoadedModPackage>& packages, std::string& outErrorMessage) {
    const DependencyResolution result = BuildPlan(packages);
    if (!result.success) {
        outErrorMessage = result.issues.front().reason;
        return false;
    }
    packages = result.packages;
    outErrorMessage.clear();
    Logger::Instance().Info("Mod load order topological sorting completed successfully.");
    return true;
}

} // namespace SoulMod
