#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "chainscratch.hpp"

namespace osg
{
    class Image;
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

        ChainAdmission() = default;
        ChainAdmission(const ChainAdmission&) = delete;
        ChainAdmission& operator=(const ChainAdmission&) = delete;
        ~ChainAdmission();

        /// `file`'s making, which holds `scratch` at most, admitted where it fits what the system has left
        /// (`Platform::availableMemory`) less what the makings admitted hold; nothing where it does not, which is
        /// logged, the file's own chain drawing.
        std::optional<Ticket> admit(const ChainScratch& scratch, const osg::Image& file);

        /// What the makings admitted hold.
        std::uint64_t getHeld();

    private:
        void release(std::uint64_t bytes);

        std::mutex mMutex;
        std::uint64_t mHeld = 0;
    };
}
