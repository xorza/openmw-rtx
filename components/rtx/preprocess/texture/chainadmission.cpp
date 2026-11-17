#include "chainadmission.hpp"

#include <cstdint>
#include <format>
#include <optional>

#include <osg/Image>

#include <components/debug/debuglog.hpp>
#include <components/platform/memory.hpp>
#include <components/rtx/common/mebibytes.hpp>
#include <components/sceneutil/embeddedimage.hpp>

namespace Rtx
{
    bool admitsChain(const ChainScratch& scratch, const osg::Image& file)
    {
        // **A chain whose making needs more than the system has left is not made**, and its file's own chain
        // draws: allocated on this thread, it ended the game. Asked as it starts and not as it is queued, since
        // what the system has left moves with the world.
        const std::optional<std::uint64_t> available = Platform::availableMemory();
        if (!available.has_value() || scratch.total() <= *available)
            return true;

        Log(Debug::Warning) << "Ray tracing keeps the chain of \"" << SceneUtil::EmbeddedImage::nameOf(file)
                            << "\" as it stands: making it again needs "
                            << std::format("{:.1f} MiB, and the system has {:.1f} MiB left", mebibytes(scratch.total()),
                                   mebibytes(*available));
        return false;
    }
}
