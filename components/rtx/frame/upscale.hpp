#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

#include "frameextents.hpp"

namespace Rtx
{
    /// Which upscaler the player asked for: `[RTX] upscaler`. The quality mode stays `Upscale`; this
    /// chooses whose arithmetic turns the traced extent into the shown one.
    ///
    /// **`Auto` is a policy and not a third upscaler**: the backend resolves it once, where the
    /// device is known, to DLSS where it runs and to FSR everywhere else. What it came to is the
    /// backend's, which no frame asks twice.
    enum class UpscalerChoice
    {
        Auto,
        Fsr,
        Dlss,
    };

    /// How an `UpscalerChoice` is spelled in a setting file and in a report — the one list of the
    /// names, as `sUpscaleNames` is for the modes.
    inline constexpr NamedEnum sUpscalerNames{ std::array{
        std::pair{ UpscalerChoice::Auto, std::string_view("auto") },
        std::pair{ UpscalerChoice::Fsr, std::string_view("fsr") },
        std::pair{ UpscalerChoice::Dlss, std::string_view("dlss") },
    } };

    /// The upscalers the launcher and the settings window list, in the order both list them:
    /// `auto` first, then the two concrete choices. Every value `sUpscalerNames` spells, in its
    /// order, so a value added there stops the build until each menu gives it a label.
    inline constexpr std::array<UpscalerChoice, sUpscalerNames.mNames.size()> sUpscalerMenu = sUpscalerNames.values();
    static_assert(sUpscalerMenu.back() == UpscalerChoice::Dlss, "DLSS last, so a menu without it is a prefix");

    /// The upscalers a menu offers: every one of `sUpscalerMenu`, DLSS only where `dlss` holds. A
    /// prefix of the menu, so an interface's labels for it are the same prefix of its labels.
    constexpr std::span<const UpscalerChoice> offeredUpscalers(const bool dlss)
    {
        return std::span(sUpscalerMenu).first(dlss ? sUpscalerMenu.size() : sUpscalerMenu.size() - 1);
    }

    /// Whether the trace writes `CHANNEL_UPSCALE_MASKS`: what the upscaler that runs reads, which the
    /// trace's kernels are specialized on rather than branching on it per pixel.
    enum class UpscaleMasks
    {
        Skipped,
        Written,
    };

    /// Where the near plane of the reversed, infinite depth both upscalers are handed stands, in
    /// world units. A reversed float depth is precise in proportion at every distance, so the choice
    /// moves no precision; a surface nearer than it reads as standing on it, which
    /// `fsrcallbacks.glsl` and `rrguides.comp` clamp to.
    inline constexpr float sUpscaleNear = 1.0f;

    /// What the launcher selects and saves for a stored kind: an explicit `Dlss` whose library is
    /// not installed becomes `Auto`, so a missing library cannot leave a setting the game must
    /// refuse to start.
    constexpr UpscalerChoice launcherUpscaler(const UpscalerChoice saved, const bool installed)
    {
        return saved == UpscalerChoice::Dlss && !installed ? UpscalerChoice::Auto : saved;
    }

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

    /// The modes the launcher and the settings window offer, in the order both list them: no
    /// upscaler first, then fewest pixels traced to every pixel. Every mode `sUpscaleNames` spells,
    /// in its order, so a mode added there stops the build until each menu gives it a label, and
    /// whatever `[RTX] upscale` holds is one the menus offer.
    inline constexpr std::array<Upscale, sUpscaleNames.mNames.size()> sUpscaleMenu = sUpscaleNames.values();

    /// Whether a mode runs the upscaler at all, which every mode but `Off` does — `Native` too.
    constexpr bool upscales(Upscale mode)
    {
        return mode != Upscale::Off;
    }

    /// What the upscaler that runs takes from the trace: a frame the renderer's own filter put
    /// together, as FSR does, or the noisy light it filters itself, as a denoising upscaler does.
    /// **A capability and not a vendor**: the backend's upscaler answers it, and the core decides
    /// everything that follows from it (`Reconstruction::resolve`).
    enum class UpscalerInput
    {
        Filtered,
        Noisy,
    };

    /// Whether the renderer's own filter can run under `mode` beside an upscaler that takes `input`:
    /// wherever nothing upscales, and under an upscaler that takes a filtered frame. **The one rule
    /// both a frame's filter and the chain's history are read off**, so a frame cannot ask the filter
    /// of a chain that keeps no history for it.
    constexpr bool rendererFilters(const Upscale mode, const UpscalerInput input)
    {
        return !upscales(mode) || input == UpscalerInput::Filtered;
    }

    /// What the output is divided by to give the extent a frame is traced at, per axis: FSR 3.1's
    /// fixed ratios (`ffxFsr3UpscalerGetUpscaleRatioFromQualityMode`) — 3, 2, 1.7 and 1.5 from ultra
    /// performance to quality, and one where every pixel is traced, natively or with no upscaler.
    ///
    /// **FSR's arithmetic, stated in the core**, because FSR's sizes are arithmetic: a table a
    /// backend answered was a question a test needed a device for. DLSS's are NGX's to give, and
    /// the backend asks them (`Upscaler::extentsFor`).
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

    /// **The smallest side a frame is shown at**: the one every mode traces a pixel or more of, the
    /// largest ratio rounded up. A side under it truncates to a traced extent of nought, which no
    /// image can be made at.
    inline constexpr std::uint32_t sSmallestFrameSide = [] {
        float largest = 1.0f;
        for (const Upscale mode : sUpscaleMenu)
            largest = std::max(largest, upscaleRatio(mode));
        const auto whole = static_cast<std::uint32_t>(largest);
        return static_cast<float>(whole) < largest ? whole + 1 : whole;
    }();

    /// What a frame shown at `outputWidth` by `outputHeight` is traced at under `mode`: each axis the
    /// output over `upscaleRatio`, truncated, in floats — `ffxFsr3UpscalerGetRenderResolutionFromQualityMode`
    /// to the bit, so the extent FSR was written against is the one it is handed.
    FrameExtents extentsFor(std::uint32_t outputWidth, std::uint32_t outputHeight, Upscale mode);

    /// How many jitter phases the reconstruction cycles through before a pixel's samples repeat:
    /// `8 * (output / render)²`, truncated — `ffxFsr3UpscalerGetJitterPhaseCount`. Eight at native,
    /// eighteen at quality: enough phases that every output pixel is sampled at eight places.
    std::uint32_t jitterPhasesFor(std::uint32_t renderWidth, std::uint32_t outputWidth);

}
