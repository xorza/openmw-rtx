#include "structuretightness.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Rtx
{
    void StructureTightness::add(
        const std::uint32_t triangles, const std::uint64_t builtBytes, const std::uint64_t tightBytes)
    {
        Octave& octave = mOctaves[octaveOf(triangles)];
        ++octave.mCount;
        octave.mTriangles += triangles;
        octave.mBuiltBytes += builtBytes;
        octave.mTightBytes += tightBytes;
    }

    std::string describe(const StructureTightness& tightness)
    {
        std::string text;
        for (std::size_t at = 0; at < StructureTightness::sOctaves; ++at)
        {
            const StructureTightness::Octave& octave = tightness.getOctaves()[at];
            if (octave.mCount == 0)
                continue;
            const std::uint64_t lowest = std::uint64_t{ 1 } << at;
            const std::string range = at + 1 < StructureTightness::sOctaves
                ? std::format("{}-{}", lowest, 2 * lowest - 1)
                : std::format("{}+", lowest);
            const auto perTriangle = [&](const std::uint64_t bytes) {
                return static_cast<double>(bytes) / static_cast<double>(octave.mTriangles);
            };
            text += std::format(
                "  {:>13} triangles  {:>6} structures  {:>7.1f} bytes a triangle built  {:>7.1f} tight"
                "  {:>5.1f}% kept\n",
                range, octave.mCount, perTriangle(octave.mBuiltBytes), perTriangle(octave.mTightBytes),
                100.0 * static_cast<double>(octave.mTightBytes) / static_cast<double>(octave.mBuiltBytes));
        }
        return text;
    }
}
