#pragma once

#include <atomic>
#include <span>

#include "GlobalJsonLoad.hpp"

namespace soulmod
{
    inline bool AreSelectedGlobalJsonLoadsApplied(std::span<const GlobalJsonLoadState> states)
    {
        bool hasApplied = false;
        for (const GlobalJsonLoadState state : states)
        {
            if (state == GlobalJsonLoadState::Inactive)
                continue;
            if (state != GlobalJsonLoadState::Applied)
                return false;
            hasApplied = true;
        }
        return hasApplied;
    }

    class RestartCoordinator
    {
    public:
        void MarkFontReady()
        {
            fontReady_.store(true, std::memory_order_release);
        }

        bool TryClaimRestart(bool globalJsonReadyNow)
        {
            if (!globalJsonReadyNow || !fontReady_.load(std::memory_order_acquire))
                return false;

            bool expected = false;
            return claimed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
        }

    private:
        std::atomic<bool> fontReady_{ false };
        std::atomic<bool> claimed_{ false };
    };
}
