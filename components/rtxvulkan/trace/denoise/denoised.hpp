#pragma once

#include <components/rtx/renderer/channel.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    class Image;

    /// Where the light a filter can take ended up, for the composite: each filter's answer, or the
    /// channel the trace wrote where no filter ran over it.
    struct Denoised
    {
        const Image& mIndirect;
        const Image& mSpecular;
        const Image& mPane;

        /// What the shadow denoiser made of the sky's source's rays, or null where it did not run:
        /// a frame nothing filters, and a frame with no source in the sky to shadow.
        const Image* mShadow;

        /// The channels themselves, which is where the light is when nothing filtered it.
        static Denoised unfiltered(const GBuffer& channels)
        {
            return Denoised{ .mIndirect = channels.get(Channel::Indirect),
                .mSpecular = channels.get(Channel::Specular),
                .mPane = channels.get(Channel::Pane),
                .mShadow = nullptr };
        }
    };
}
