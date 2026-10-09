#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace Rtx
{
    /// A stretch of a frame the device times (`GpuSpan`), one a pass. A name and not a number in
    /// the report, the capture's labels and the checkpoints a lost device leaves; a value here,
    /// so a reader that wants one zone asks for it by what it is.
    enum class FrameZone : std::uint8_t
    {
        Blas,
        Compact,
        Refit,
        Tlas,
        Skin,
        Ripples,
        Waves,
        Shelter,
        Emitters,
        Air,
        Column,
        Trace,
        Puffs,
        Sprites,
        Shade,
        Accumulate,
        Clamp,
        Shadow,
        Specular,
        Pane,

        /// The wavelet, a zone a level (`AtrousPass`): the wide first level, which writes the history,
        /// and the narrow three, so a change to one is measured apart from the others.
        Filter0,
        Filter1,
        Filter2,
        Filter3,

        Composite,
        Upscale,
        Bloom,
        Exposure,
        Glare,
        Tone,
        Lines,
        Digest,

        /// The queue held after the trace (`RenderProfile::mStressOverlapMs`), which the harness's
        /// `QueueHeld` check reads back.
        Stress,

        Count,
    };

    inline constexpr std::size_t sFrameZoneCount = static_cast<std::size_t>(FrameZone::Count);

    /// What the report and a capture's label call each zone. Literals, so a name's `data()` is a C
    /// string.
    inline constexpr NamedEnum sFrameZoneNames{ std::array{
        std::pair{ FrameZone::Blas, std::string_view("blas") },
        std::pair{ FrameZone::Compact, std::string_view("compact") },
        std::pair{ FrameZone::Refit, std::string_view("refit") },
        std::pair{ FrameZone::Tlas, std::string_view("tlas") },
        std::pair{ FrameZone::Skin, std::string_view("skin") },
        std::pair{ FrameZone::Ripples, std::string_view("ripples") },
        std::pair{ FrameZone::Waves, std::string_view("waves") },
        std::pair{ FrameZone::Shelter, std::string_view("shelter") },
        std::pair{ FrameZone::Emitters, std::string_view("emitters") },
        std::pair{ FrameZone::Air, std::string_view("air") },
        std::pair{ FrameZone::Column, std::string_view("column") },
        std::pair{ FrameZone::Trace, std::string_view("trace") },
        std::pair{ FrameZone::Puffs, std::string_view("puffs") },
        std::pair{ FrameZone::Sprites, std::string_view("sprites") },
        std::pair{ FrameZone::Shade, std::string_view("shade") },
        std::pair{ FrameZone::Accumulate, std::string_view("accumulate") },
        std::pair{ FrameZone::Clamp, std::string_view("clamp") },
        std::pair{ FrameZone::Shadow, std::string_view("shadow") },
        std::pair{ FrameZone::Specular, std::string_view("specular") },
        std::pair{ FrameZone::Pane, std::string_view("pane") },
        std::pair{ FrameZone::Filter0, std::string_view("filter0") },
        std::pair{ FrameZone::Filter1, std::string_view("filter1") },
        std::pair{ FrameZone::Filter2, std::string_view("filter2") },
        std::pair{ FrameZone::Filter3, std::string_view("filter3") },
        std::pair{ FrameZone::Composite, std::string_view("composite") },
        std::pair{ FrameZone::Upscale, std::string_view("upscale") },
        std::pair{ FrameZone::Bloom, std::string_view("bloom") },
        std::pair{ FrameZone::Exposure, std::string_view("exposure") },
        std::pair{ FrameZone::Glare, std::string_view("glare") },
        std::pair{ FrameZone::Tone, std::string_view("tone") },
        std::pair{ FrameZone::Lines, std::string_view("lines") },
        std::pair{ FrameZone::Digest, std::string_view("digest") },
        std::pair{ FrameZone::Stress, std::string_view("stress") },
    } };
}
