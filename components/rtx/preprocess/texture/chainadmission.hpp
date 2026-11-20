#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "chainscratch.hpp"

namespace osg
{
    class Image;
}

namespace Resource
{
    class MemoryArbiter;
}

namespace Rtx
{
    /// **Whether a chain is made, against what the system has left less what every making admitted before it holds**:
    /// each making's most (`ChainScratch::total`) summed over the hands making one at once, where each alone once
    /// passed against the whole of it and together they took the game down. The one rule the loading screen's
    /// preparation and play's chains are both held to, asked only where a chain is to be made and not found. Asked
    /// from every hand's thread at once.
    class ChainAdmission
    {
    public:
        /// A making admitted: what it holds against the rest until it goes.
        class Ticket
        {
        public:
            Ticket() = default;
            Ticket(Ticket&& other) noexcept;
            Ticket& operator=(Ticket&& other) noexcept;
            Ticket(const Ticket&) = delete;
            Ticket& operator=(const Ticket&) = delete;
            ~Ticket();

        private:
            friend class ChainAdmission;
            Ticket(ChainAdmission& owner, std::uint64_t bytes);

            ChainAdmission* mOwner = nullptr;
            std::uint64_t mBytes = 0;
        };

        /// @param memory the one sampler of the system's memory, whose last figure a making is admitted against, or
        /// null
        ///        for none, where every making is admitted.
        explicit ChainAdmission(const Resource::MemoryArbiter* memory);
        ChainAdmission(const ChainAdmission&) = delete;
        ChainAdmission& operator=(const ChainAdmission&) = delete;
        ~ChainAdmission();

        /// `file`'s making, which holds `scratch` at most, admitted where it fits what the system had left at the
        /// arbiter's last tick (`Resource::MemoryArbiter::getAvailable`) less what the makings admitted hold; nothing
        /// where it does not, which is logged, the file's own chain drawing.
        std::optional<Ticket> admit(const ChainScratch& scratch, const osg::Image& file);

        /// What the makings admitted hold.
        std::uint64_t getHeld();

    private:
        void release(std::uint64_t bytes);

        const Resource::MemoryArbiter* const mMemory;

        std::mutex mMutex;
        std::uint64_t mHeld = 0;
    };
}
