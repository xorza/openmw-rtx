#pragma once

#include <cstdint>

#include "device.hpp"
#include "timeline.hpp"

namespace Rtx
{
    /// The submits that name a resource, as the values they signal on the queue's timeline — what
    /// a host write or read of the resource waits for, because a host write over a submit still
    /// reading is the one hazard the layers cannot see. Held by a buffer, an image and a
    /// descriptor set.
    ///
    /// Stamped at the hand-out and nowhere else: an address, descriptor, barrier, copy or bound
    /// set names its resource for the next submit as it is handed out, so the stamp is exact for
    /// every reader on the queue whatever carried it.
    ///
    /// Two words, because the newest naming alone reads idle for a resource named by a submit in
    /// flight and again by the pending one. Idleness is decided by the newest naming for a submit
    /// already made (the queue finishes in order), which is the newest where it is made and the one
    /// before it where the newest is pending.
    ///
    /// Mutable throughout: naming is not a change to the bytes and every caller holds the resource const.
    class ReadStamp
    {
    public:
        /// Says a submit signalling `value` names the resource. `value` is `Timeline::getNext`
        /// at every caller, so values never fall.
        void nameFor(const std::uint64_t value) const
        {
            if (value == mNewest)
                return;

            mPrevious = mNewest;
            mNewest = value;
        }

        /// The last value a submit naming the resource signals, or nought where nothing has.
        std::uint64_t getNamedUntil() const { return mNewest; }

        /// Whether the host has waited past every submit that names the resource.
        ///
        /// A stamp for the next submit is not a hazard: that submit has not been made, and a host
        /// write before it is what it sees, as a placement writes one mesh's rows and then the
        /// next's. True of a resource nothing has named.
        bool isIdle(const Device& device) const { return device.getTimeline().hasFinished(newestMade(device)); }

        /// Blocks until `isIdle`, where a submit naming the resource is still on the queue. `what`
        /// names the wait in the error a device that stops answering produces.
        void waitIdle(const Device& device, const char* what) const
        {
            if (!isIdle(device))
                device.waitFor(newestMade(device), what);
        }

    private:
        /// The newest naming for a submit already made: the newest where it is, and the one before
        /// it where the newest is the pending submit.
        std::uint64_t newestMade(const Device& device) const
        {
            return mNewest < device.getTimeline().getNext() ? mNewest : mPrevious;
        }

        /// The two newest distinct namings. Nought is a naming nothing has to wait for.
        mutable std::uint64_t mPrevious = 0;
        mutable std::uint64_t mNewest = 0;
    };
}
