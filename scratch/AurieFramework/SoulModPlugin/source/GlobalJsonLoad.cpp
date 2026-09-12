#include "GlobalJsonLoad.hpp"

#include <vector>

namespace soulmod
{
    GlobalJsonLoadEpoch::GlobalJsonLoadEpoch(const GlobalJsonLoad& operation)
        : state_(operation.condition ? GlobalJsonLoadState::Inactive : GlobalJsonLoadState::Pending)
    {
    }

    GlobalJsonLoadState GlobalJsonLoadEpoch::State() const
    {
        return state_.load();
    }

    GlobalJsonLoadApplyOutcome ApplyGlobalJsonLoad(
        const GlobalJsonLoad& operation,
        GlobalJsonLoadHost& host,
        GlobalJsonLoadEpoch& epoch,
        std::string& error)
    {
        error.clear();
        bool epochStarted = false;
        if (operation.condition)
        {
            std::string value;
            const GlobalJsonLoadConditionReadiness readiness = host.ReadConditionGlobal(operation.condition->global, value, error);
            if (readiness == GlobalJsonLoadConditionReadiness::Missing || (readiness == GlobalJsonLoadConditionReadiness::String && value != operation.condition->equals))
            {
                const GlobalJsonLoadState previous = epoch.state_.exchange(GlobalJsonLoadState::Inactive);
                return { previous == GlobalJsonLoadState::Inactive ? GlobalJsonLoadApplyResult::ConditionNotMatched : GlobalJsonLoadApplyResult::ConditionDeactivated, false, previous != GlobalJsonLoadState::Inactive, false };
            }
            if (readiness == GlobalJsonLoadConditionReadiness::Failure)
            {
                const GlobalJsonLoadState previous = epoch.state_.exchange(GlobalJsonLoadState::Failed);
                return { GlobalJsonLoadApplyResult::TerminalFailure, false, false, previous != GlobalJsonLoadState::Failed };
            }

            GlobalJsonLoadState inactive = GlobalJsonLoadState::Inactive;
            if (epoch.state_.compare_exchange_strong(inactive, GlobalJsonLoadState::Pending)) epochStarted = true;
        }

        GlobalJsonLoadState expected = GlobalJsonLoadState::Pending;
        if (!epoch.state_.compare_exchange_strong(expected, GlobalJsonLoadState::Applying))
        {
            if (expected == GlobalJsonLoadState::Applied) return { GlobalJsonLoadApplyResult::AlreadyApplied, false, false, false };
            if (expected == GlobalJsonLoadState::Failed) return { GlobalJsonLoadApplyResult::TerminalFailure, false, false, false };
            return { GlobalJsonLoadApplyResult::AlreadyApplying, false, false, false };
        }

        std::vector<std::string_view> names;
        names.reserve(operation.mirrorGlobals.size() + 1);
        names.push_back(operation.targetGlobal);
        for (const std::string& mirror : operation.mirrorGlobals) names.push_back(mirror);

        for (const std::string_view name : names)
        {
            const GlobalJsonLoadReadiness readiness = host.CheckGlobalReady(name, error);
            if (readiness == GlobalJsonLoadReadiness::Ready) continue;
            host.Clear();
            if (readiness == GlobalJsonLoadReadiness::NotReady)
            {
                epoch.state_.store(GlobalJsonLoadState::Pending);
                return { GlobalJsonLoadApplyResult::NotReady, epochStarted, false, false };
            }
            epoch.state_.store(GlobalJsonLoadState::Failed);
            return { GlobalJsonLoadApplyResult::TerminalFailure, epochStarted, false, true };
        }

        std::string bytes;
        if (!host.ReadVerifiedAsset(operation, bytes, error) || !host.DecodeJson(bytes, error))
        {
            host.Clear();
            epoch.state_.store(GlobalJsonLoadState::Failed);
            return { GlobalJsonLoadApplyResult::TerminalFailure, epochStarted, false, true };
        }

        for (const std::string_view name : names)
        {
            if (!host.SnapshotGlobal(name, error))
            {
                host.Clear();
                epoch.state_.store(GlobalJsonLoadState::Failed);
                return { GlobalJsonLoadApplyResult::TerminalFailure, epochStarted, false, true };
            }
        }

        std::vector<std::string_view> written;
        for (const std::string_view name : names)
        {
            if (host.SetDecodedGlobal(name, error))
            {
                written.push_back(name);
                continue;
            }

            std::string rollbackError;
            for (auto writtenName = written.rbegin(); writtenName != written.rend(); ++writtenName)
            {
                if (!host.RestoreSnapshot(*writtenName, rollbackError) && error.empty())
                    error = "global JSON rollback failed: " + rollbackError;
            }
            host.Clear();
            epoch.state_.store(GlobalJsonLoadState::Failed);
            return { GlobalJsonLoadApplyResult::TerminalFailure, epochStarted, false, true };
        }

        host.Clear();
        epoch.state_.store(GlobalJsonLoadState::Applied);
        return { GlobalJsonLoadApplyResult::Applied, epochStarted, false, false };
    }
}
