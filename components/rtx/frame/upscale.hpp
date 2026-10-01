#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

#include "frameextents.hpp"

namespace Rtx
{
    /// How the frame gets from the size it is traced at to the size it is shown at — a quality
    /// level rather than a ratio, because the ratio is the upscaler's: `upscaleRatio`.
    enum class Upscale
    {
        /// Trace and present at the same size, with no upscaler in the frame at all: the denoisers
        /// and nothing after them, and no anti-aliasing — what every test and every reference
        /// render uses.
        Off,

        /// The fewest pixels traced for a given output.
        UltraPerformance,

        Performance,
        Balanced,
        Quality,

        /// No upscaling, but still the upscaler: render and output are the same size and it only
        /// reconstructs across frames. What separates the upscale from the reconstruction, when a
        /// frame comes out softer than the reference and the question is which softened it.
        Native,
    };

    /// How an `Upscale` is spelled on a command line, in a setting file and in a report — the one
    /// list of the names, so a mode added here reaches the parser, the report and every line of
    /// prose that offers the modes at once.
    inline constexpr NamedEnum sUpscaleNames{ std::array{
        std::pair{ Upscale::Off, std::string_view("off") },
        std::pair{ Upscale::UltraPerformance, std::string_view("ultraperformance") },
        std::pair{ Upscale::Performance, std::string_view("performance") },
        std::pair{ Upscale::Balanced, std::string_view("balanced") },
        std::pair{ Upscale::Quality, std::string_view("quality") },
        std::pair{ Upscale::Native, std::string_view("native") },
    } };

    /// The modes the launcher and the settings window offer, in the order both list them, spelled
    /// as `[RTX] upscale` takes them: no upscaler first, then fewest pixels traced to every pixel.
    /// Every mode `sUpscaleNames` spells, in its order, so a mode added there stops the build until
    /// each menu gives it a label.
    inline constexpr std::array<std::string_view, sUpscaleNames.mNames.size()> sUpscaleMenu = sUpscaleNames.spellings();

    /// Whether a mode runs the upscaler at all, which every mode but `Off` does — `Native` too.
    constexpr bool upscales(Upscale mode)
    {
        return mode != Upscale::Off;
    }

    /// What the output is divided by to give the extent a frame is traced at, per axis: FSR 3.1's
    /// fixed ratios (`ffxFsr3UpscalerGetUpscaleRatioFromQualityMode`) — 3, 2, 1.7 and 1.5 from ultra
    /// performance to quality, and one where every pixel is traced, natively or with no upscaler.
    ///
    /// **The upscaler's arithmetic, stated in the core**, because the upscaler is FSR and FSR's
    /// sizes are arithmetic: a table a backend answered was a question a test needed a device for.
    constexpr float upscaleRatio(Upscale mode)
    {
        switch (mode)
        {
            case Upscale::UltraPerformance:
                return 3.0f;
            case Upscale::Performance:
                return 2.0f;
            case Upscale::Balanced:
                return 1.7f;
            case Upscale::Quality:
                return 1.5f;
            case Upscale::Off:
            case Upscale::Native:
                break;
        }

        return 1.0f;
    }

    /// What a frame shown at `outputWidth` by `outputHeight` is traced at under `mode`: each axis the
    /// output over `upscaleRatio`, truncated, in floats — `ffxFsr3UpscalerGetRenderResolutionFromQualityMode`
    /// to the bit, so the extent FSR was written against is the one it is handed.
    FrameExtents extentsFor(std::uint32_t outputWidth, std::uint32_t outputHeight, Upscale mode);

    /// How many jitter phases the reconstruction cycles through before a pixel's samples repeat:
    /// `8 * (output / render)²`, truncated — `ffxFsr3UpscalerGetJitterPhaseCount`. Eight at native,
    /// eighteen at quality: enough phases that every output pixel is sampled at eight places.
    std::uint32_t jitterPhasesFor(std::uint32_t renderWidth, std::uint32_t outputWidth);

}
