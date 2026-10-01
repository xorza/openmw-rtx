#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <format>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include <components/platform/thread.hpp>

namespace Rtx
{
    /// Runs `body(index)` for every index below `count`, over as many hands as there is work for, the
    /// calling thread among them, and comes back when the last of them is done. One shot, unlike
    /// `Rtx::Worker`. A hand takes the next index whenever it is free, so a turn that costs ten times
    /// its neighbour delays nothing but itself. The first exception is rethrown and every index is
    /// still attempted, because a caller that asked for a batch wants to know about the batch. Each
    /// hand gets its own copy of both callables, as the standard's parallel algorithms do, so a body
    /// with state of its own is not a data race.
    ///
    /// @param name what the hands' threads are called, numbered from one; the caller keeps its own.
    /// @param stop where a stop asked of the caller's thread reaches: no hand takes another index
    ///        once it is asked, and the batch comes back with what was done.
    /// @param equip what each hand holds for as long as it runs, built on that hand's own thread
    ///        and destroyed there — how the Vulkan backend files a validation message under the
    ///        thread that asked for the work. What it throws is kept as a body's is, and that hand
    ///        takes no index.
    template <class Equip, class Body>
    void runInParallel(
        std::string_view name, const std::size_t count, const Platform::StopToken& stop, Equip equip, Body body)
    {
        if (count == 0)
            return;

        std::atomic<std::size_t> next{ 0 };
        std::mutex kept;
        std::exception_ptr failed;

        const auto keep = [&] {
            const std::lock_guard<std::mutex> hold(kept);
            if (failed == nullptr)
                failed = std::current_exception();
        };

        const auto hand = [&next, &keep, &stop, count](Equip ownEquip, Body ownBody) {
            try
            {
                // Held for the hand's whole run and read by nothing: what it is for is its life.
                [[maybe_unused]] const auto held = ownEquip();

                for (std::size_t index = next++; index < count && !stop.stopRequested(); index = next++)
                {
                    try
                    {
                        ownBody(index);
                    }
                    catch (...)
                    {
                        keep();
                    }
                }
            }
            catch (...)
            {
                keep();
            }
        };

        {
            const std::size_t hands = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, count);

            std::vector<Platform::Thread> others;
            others.reserve(hands - 1);
            for (std::size_t at = 1; at < hands; ++at)
                others.emplace_back(std::format("{} {}", name, at), [&hand, equip, body] { hand(equip, body); });

            hand(equip, body);
        }

        if (failed != nullptr)
            std::rethrow_exception(failed);
    }
}
