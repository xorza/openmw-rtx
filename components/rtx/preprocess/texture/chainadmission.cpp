#include "chainadmission.hpp"

#include <cassert>
#include <cstdint>
#include <format>
#include <optional>
#include <utility>

#include <osg/Image>

#include <components/debug/debuglog.hpp>
#include <components/platform/memory.hpp>
#include <components/rtx/common/mebibytes.hpp>
#include <components/sceneutil/embeddedimage.hpp>

namespace Rtx
{
    ChainAdmission::Ticket::Ticket(ChainAdmission& owner, const std::uint64_t bytes)
        : mOwner(&owner)
        , mBytes(bytes)
    {
    }

    ChainAdmission::Ticket::Ticket(Ticket&& other) noexcept
        : mOwner(std::exchange(other.mOwner, nullptr))
        , mBytes(std::exchange(other.mBytes, 0))
    {
    }

    ChainAdmission::Ticket& ChainAdmission::Ticket::operator=(Ticket&& other) noexcept
    {
        if (this != &other)
        {
            if (mOwner != nullptr)
                mOwner->release(mBytes);
            mOwner = std::exchange(other.mOwner, nullptr);
            mBytes = std::exchange(other.mBytes, 0);
        }
        return *this;
    }

    ChainAdmission::Ticket::~Ticket()
    {
        if (mOwner != nullptr)
            mOwner->release(mBytes);
    }

    ChainAdmission::~ChainAdmission()
    {
        assert(mHeld == 0 && "an admission that goes while a making it admitted holds its bytes");
    }

    std::optional<ChainAdmission::Ticket> ChainAdmission::admit(const ChainScratch& scratch, const osg::Image& file)
    {
        // **A chain whose making needs more than the system has left is not made**, and its file's own chain draws:
        // allocated on this thread, it ended the game. Asked as it starts and not as it is queued, since what the
        // system has left moves with the world.
        const std::uint64_t bytes = scratch.total();
        const std::optional<std::uint64_t> available = Platform::availableMemory();
        std::uint64_t held = 0;
        {
            const std::scoped_lock lock(mMutex);
            held = mHeld;
            if (!available.has_value() || held + bytes <= *available)
            {
                mHeld += bytes;
                return Ticket(*this, bytes);
            }
        }

        Log(Debug::Warning) << "Ray tracing keeps the chain of \"" << SceneUtil::EmbeddedImage::nameOf(file)
                            << "\" as it stands: making it again needs "
                            << std::format(
                                   "{:.1f} MiB, and the system has {:.1f} MiB left, {:.1f} MiB of it held by "
                                   "the chains being made",
                                   mebibytes(bytes), mebibytes(*available), mebibytes(held));
        return std::nullopt;
    }

    std::uint64_t ChainAdmission::getHeld()
    {
        const std::scoped_lock lock(mMutex);
        return mHeld;
    }

    void ChainAdmission::release(const std::uint64_t bytes)
    {
        const std::scoped_lock lock(mMutex);
        assert(mHeld >= bytes && "a making gives back more than it held");
        mHeld -= bytes;
    }
}
