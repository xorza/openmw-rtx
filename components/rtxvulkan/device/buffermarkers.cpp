#include "buffermarkers.hpp"

#include <cstring>

namespace Rtx
{
    namespace
    {
        using Words = std::array<std::uint32_t, 2>;
    }

    BufferMarkers::BufferMarkers(const Device& device, const PFN_vkCmdWriteBufferMarkerAMD write)
        : mWrite(write)
        , mWords(Buffer::readBack(device, sizeof(Words), VK_BUFFER_USAGE_TRANSFER_DST_BIT, "markers"))
    {
        // Nought before the first marker, which is what `MarkerRing::find` reads as none written.
        std::memset(mWords.map(), 0, sizeof(Words));
    }

    void BufferMarkers::mark(const VkCommandBuffer commands, const Checkpoint* const checkpoint)
    {
        const std::uint32_t marker = mRing.mark(checkpoint);
        mWords.nameForNext();
        mWrite(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mWords.getHandle(), 0, marker);
        mWrite(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mWords.getHandle(), sizeof(std::uint32_t), marker);
    }

    BufferMarkers::Passed BufferMarkers::read() const
    {
        Words words;
        std::memcpy(words.data(), mWords.map(), sizeof(words));
        return Passed{ .mTop = mRing.find(words[0]), .mBottom = mRing.find(words[1]) };
    }
}
