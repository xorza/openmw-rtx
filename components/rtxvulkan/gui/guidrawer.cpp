#include "guidrawer.hpp"

#include <cassert>
#include <vector>

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>

namespace Rtx
{
    GuiDrawer::GuiDrawer(const Device& device)
        : mDevice(device)
        , mPass(device)
        , mTextures(device)
    {
        // Allocated once and recorded into again.
        const std::vector<VkCommandBuffer> commands = mDevice.getPool().allocate(sFrameSlots);
        for (std::uint32_t slot = 0; slot < sFrameSlots; ++slot)
        {
            Slot& held = mSlots.at(FrameSlot{ slot });
            held.mCommands = commands[slot];
            held.mVertices
                = GrowableBuffer(device, BufferKind::HostWritten, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "gui vertices");
        }
    }

    void GuiDrawer::draw(std::span<const GuiVertex> vertices, std::span<const GuiBatch> batches, const Image& target)
    {
        // The interface drawn two draws ago drew out of this slot, and the vertices carry the
        // submit that bound them: that passed is what says they may be written over.
        Slot& slot = mSlots.at(FrameSlot{ static_cast<std::uint32_t>(mDrawn % sFrameSlots) });
        slot.mVertices.get().waitIdle("the interface drawn two frames ago");

        slot.mVertices.outgrow(vertices.size_bytes());
        slot.mVertices.get().write(vertices);

        mDraws.clear();
        mDraws.reserve(batches.size());
        for (const GuiBatch& batch : batches)
        {
            const VkImageView view = mTextures.getView(batch.mTexture);
            assert(view != VK_NULL_HANDLE && "a batch names a texture this renderer does not hold");

            // A slot nothing holds would be a null descriptor, which is undefined rather than
            // blank. The assert above is where a caller finds out; a release build drops the batch.
            if (view != VK_NULL_HANDLE)
                mDraws.push_back(GuiDraw{ view, batch.mFirstVertex, batch.mVertexCount,
                    batch.mBlend == GuiBlend::Additive ? Blend::Additive : Blend::Over,
                    mTextures.alphaOf(batch.mTexture) });
        }

        const VkCommandBuffer commands = slot.mCommands;
        mDevice.getPool().begin(commands);
        target.transition(commands, Use::sComputeWrite, Use::sColourAttachment);
        mPass.record(commands, target, slot.mVertices.get(), mDraws);
        target.transition(commands, Use::sColourAttachment, Use::sAnyGeneralRead);
        mDevice.getPool().submit(commands);

        ++mDrawn;
    }
}
