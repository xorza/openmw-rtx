#include "graveyard.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>

#include "device.hpp"
#include "timeline.hpp"

namespace Rtx
{
    void buryHandle(const Device& device, const EndHandle end, const std::uint64_t handle)
    {
        device.getGraveyard().bury(end, handle);
    }

    Graveyard::Graveyard(const Device& device)
        : mDevice(device)
    {
    }

    Graveyard::~Graveyard()
    {
        // What the device's own idle left: burials stamped for a submit nobody will make now, and
        // what the members declared after this one buried as the device came apart.
        collectIdle();
    }

    void Graveyard::bury(const EndHandle end, const std::uint64_t handle)
    {
        // The stamp is read under the lock, so the queue holds its burials in stamp order whatever
        // thread made them.
        const std::lock_guard<std::mutex> lock(mLock);
        mHeld.hold(mDevice.getTimeline().getNext(),
            Burial{ .mEnd = handle != 0 ? end : nullptr, .mHandle = handle, .mMemory = {} });
    }

    void Graveyard::bury(DeviceMemory&& memory)
    {
        const std::lock_guard<std::mutex> lock(mLock);
        mHeld.hold(
            mDevice.getTimeline().getNext(), Burial{ .mEnd = nullptr, .mHandle = 0, .mMemory = std::move(memory) });
    }

    void Graveyard::collect()
    {
        freeThrough(mDevice.getTimeline().getKnownFinished());
    }

    void Graveyard::collectIdle()
    {
        assert(mDevice.getTimeline().isIdle() && "everything held destroyed under a submit still on the queue");

        freeThrough(std::numeric_limits<std::uint64_t>::max());
    }

    std::size_t Graveyard::getHeldCount() const
    {
        const std::lock_guard<std::mutex> lock(mLock);
        return mHeld.size();
    }

    void Graveyard::freeThrough(const std::uint64_t finished)
    {
        const std::lock_guard<std::mutex> lock(mLock);

        mHeld.releaseThrough(finished, [&](Burial& burial) {
            if (burial.mEnd != nullptr)
                burial.mEnd(mDevice, burial.mHandle);
            burial.mMemory.releaseNow();
        });
    }
}
