#pragma once

#include <cstdint>

#include <components/rtx/frame/upscale.hpp>

namespace Rtx
{
    /// Whether a chain keeps what a trace leaves for the next one to read: the denoisers' histories
    /// and the air's.
    ///
    /// **A picture keeps none**: every picture is traced with its past lost, so no trace reads a
    /// history and one image of each pair holds everything a trace writes. The world's chain keeps
    /// both halves, the last trace's and this one's.
    enum class TracePast : std::uint8_t
    {
        Kept,
        Dropped,
    };

    /// Whether the renderer's own filter can run on a chain at its extent (`Rtx::rendererFilters`),
    /// which decides whether the chain holds the filter's images at all: its histories and scratch
    /// (`DenoiseHistory`) and the frame before's surfaces the filters hold them to
    /// (`GBuffer::getHeld`).
    ///
    /// **Absent under an upscaler that filters the light itself**, where no frame runs the filter:
    /// 128 bytes a traced pixel of history and held surfaces, and 65 of scratch, that nothing reads.
    /// A picture's filter always runs.
    enum class RendererFilter : std::uint8_t
    {
        Runs,
        Absent,
    };

    /// Whether the renderer's filter can run on the world's chain under `mode`, beside an upscaler
    /// that takes `input`: `Rtx::rendererFilters`, the rule a frame's filter is read off too.
    constexpr RendererFilter rendererFilterUnder(const Upscale mode, const UpscalerInput input)
    {
        return rendererFilters(mode, input) ? RendererFilter::Runs : RendererFilter::Absent;
    }
}
