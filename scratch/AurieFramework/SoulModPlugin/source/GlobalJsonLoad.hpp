#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include "RuntimeOperations.hpp"

namespace soulmod
{
    enum class GlobalJsonLoadState : unsigned char { Inactive, Pending, Applying, Applied, Failed };
    enum class GlobalJsonLoadReadiness : unsigned char { Ready, NotReady, Failure };
    enum class GlobalJsonLoadConditionReadiness : unsigned char { String, Missing, Failure };
    enum class GlobalJsonLoadApplyResult : unsigned char { Applied, NotReady, AlreadyApplied, AlreadyApplying, TerminalFailure, ConditionNotMatched, ConditionDeactivated };

    class GlobalJsonLoadHost;

    struct GlobalJsonLoadApplyOutcome
    {
        GlobalJsonLoadApplyResult result;
        bool epochStarted = false;
        bool deactivated = false;
        bool failureStarted = false;
    };

    class GlobalJsonLoadEpoch
    {
    public:
        explicit GlobalJsonLoadEpoch(const GlobalJsonLoad& operation);
        GlobalJsonLoadState State() const;

    private:
        std::atomic<GlobalJsonLoadState> state_;

        friend GlobalJsonLoadApplyOutcome ApplyGlobalJsonLoad(const GlobalJsonLoad&, GlobalJsonLoadHost&, GlobalJsonLoadEpoch&, std::string&);
    };

    class GlobalJsonLoadHost
    {
    public:
        virtual ~GlobalJsonLoadHost() = default;
        virtual GlobalJsonLoadConditionReadiness ReadConditionGlobal(std::string_view name, std::string& value, std::string& error) = 0;
        virtual GlobalJsonLoadReadiness CheckGlobalReady(std::string_view name, std::string& error) = 0;
        virtual bool ReadVerifiedAsset(const GlobalJsonLoad& operation, std::string& bytes, std::string& error) = 0;
        virtual bool DecodeJson(std::string_view bytes, std::string& error) = 0;
        virtual bool SnapshotGlobal(std::string_view name, std::string& error) = 0;
        virtual bool SetDecodedGlobal(std::string_view name, std::string& error) = 0;
        virtual bool RestoreSnapshot(std::string_view name, std::string& error) = 0;
        virtual void Clear() = 0;
    };

    GlobalJsonLoadApplyOutcome ApplyGlobalJsonLoad(
        const GlobalJsonLoad& operation,
        GlobalJsonLoadHost& host,
        GlobalJsonLoadEpoch& epoch,
        std::string& error);
}
