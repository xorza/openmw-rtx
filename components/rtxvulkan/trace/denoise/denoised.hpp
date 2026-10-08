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
        const Image& mFill;
        const Image& mSpecular;
        const Image& mPane;

        /// What the shadow denoiser made of each field's rays (`ShadowField`), or null where it did
        /// not run: a frame nothing filters, and a frame with no source in the sky to shadow, or no
        /// lamp.
        const Image* mSkyShadow;
        const Image* mLampShadow;

        /// The channels themselves, which is where the light is when nothing filtered it.
        static Denoised unfiltered(const GBuffer& channels)
        {
            return Denoised{ .mIndirect = channels.get(Channel::Indirect),
                .mFill = channels.get(Channel::Fill),
                .mSpecular = channels.get(Channel::Specular),
                .mPane = channels.get(Channel::Pane),
                .mSkyShadow = nullptr,
                .mLampShadow = nullptr };
        }
    };
}
