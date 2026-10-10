#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace Rtx
{
    /// What the denoiser leaves of a frame, each stage's answer as the frame ends: what a run's
    /// digest takes of it beside the trace's channels (`FrameDigest::mDenoiser`), so a comparison
    /// that finds the composed frame moved names the first stage that moved under it. **In the
    /// order the stages run**: the temporal passes, the clamps and the shadow's tiles, the shadow
    /// filter's levels and the wavelet's, so the first of them that differs is where a difference
    /// began. The wavelet's levels by the image each ends in: its history the first, the narrow
    /// image the second — the fourth too, where the composite and not the level composes the frame
    /// — and the blend the third.
    enum class DenoiserImage : std::uint8_t
    {
        AccumulateMoments,
        PaneMean,
        SpecularMean,
        AccumulateFast,
        SkyShadowMoments,
        LampShadowMoments,
        PaneFast,
        SpecularFast,
        SkyShadowLevel0,
        LampShadowLevel0,
        SkyShadowLevel1,
        LampShadowLevel1,
        SkyShadowLevel2,
        LampShadowLevel2,
        WaveletHistory,
        WaveletFillHistory,
        WaveletNarrow,
        WaveletFillNarrow,
        WaveletBlend,
        WaveletFillBlend,

        Count,
    };

    inline constexpr std::size_t sDenoiserImageCount = static_cast<std::size_t>(DenoiserImage::Count);

    /// What a hashes file and the report call each.
    inline constexpr NamedEnum sDenoiserImageNames{ std::array{
        std::pair{ DenoiserImage::AccumulateMoments, std::string_view("accumulate-moments") },
        std::pair{ DenoiserImage::PaneMean, std::string_view("pane-mean") },
        std::pair{ DenoiserImage::SpecularMean, std::string_view("specular-mean") },
        std::pair{ DenoiserImage::AccumulateFast, std::string_view("accumulate-fast") },
        std::pair{ DenoiserImage::SkyShadowMoments, std::string_view("sky-shadow-moments") },
        std::pair{ DenoiserImage::LampShadowMoments, std::string_view("lamp-shadow-moments") },
        std::pair{ DenoiserImage::PaneFast, std::string_view("pane-fast") },
        std::pair{ DenoiserImage::SpecularFast, std::string_view("specular-fast") },
        std::pair{ DenoiserImage::SkyShadowLevel0, std::string_view("sky-shadow-level0") },
        std::pair{ DenoiserImage::LampShadowLevel0, std::string_view("lamp-shadow-level0") },
        std::pair{ DenoiserImage::SkyShadowLevel1, std::string_view("sky-shadow-level1") },
        std::pair{ DenoiserImage::LampShadowLevel1, std::string_view("lamp-shadow-level1") },
        std::pair{ DenoiserImage::SkyShadowLevel2, std::string_view("sky-shadow-level2") },
        std::pair{ DenoiserImage::LampShadowLevel2, std::string_view("lamp-shadow-level2") },
        std::pair{ DenoiserImage::WaveletHistory, std::string_view("wavelet-history") },
        std::pair{ DenoiserImage::WaveletFillHistory, std::string_view("wavelet-fill-history") },
        std::pair{ DenoiserImage::WaveletNarrow, std::string_view("wavelet-narrow") },
        std::pair{ DenoiserImage::WaveletFillNarrow, std::string_view("wavelet-fill-narrow") },
        std::pair{ DenoiserImage::WaveletBlend, std::string_view("wavelet-blend") },
        std::pair{ DenoiserImage::WaveletFillBlend, std::string_view("wavelet-fill-blend") },
    } };
}
