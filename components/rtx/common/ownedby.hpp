#pragma once

#include <cassert>
#include <thread>

namespace Rtx
{
    /// Which thread a member belongs to, asserted rather than written down. The check is the
    /// assert and nothing else, so release pays for no comparison; the id stays a member in both
    /// builds, because a class that changed shape between builds is a worse trade than one word.
    class OwnedBy
    {
    public:
        /// Belongs to whoever built it, which for a member is whoever built the class.
        OwnedBy() = default;

        /// Fires where this is not that thread.
        void check() const { assert(mOwner == std::this_thread::get_id() && "a member touched from the wrong thread"); }

        /// Hands it to the calling thread, for the one case the design allows: an owner built on
        /// one thread and driven from another.
        void adopt() { mOwner = std::this_thread::get_id(); }

    private:
        std::thread::id mOwner = std::this_thread::get_id();
    };
}
