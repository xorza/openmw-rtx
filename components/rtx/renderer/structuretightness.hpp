#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Rtx
{
    /// **What structures came to once made tight against as first built, by their meshes' triangles**, an octave each:
    /// the sizes a structure's slot classes are cut by, which a driver states only once a structure is built.
    class StructureTightness
    {
    public:
        /// How many octaves are kept: from one triangle up, the last holding every mesh of 2^19 triangles or more.
        static constexpr std::size_t sOctaves = 20;

        struct Octave
        {
            std::uint32_t mCount = 0;
            std::uint64_t mTriangles = 0;
            std::uint64_t mBuiltBytes = 0;
            std::uint64_t mTightBytes = 0;

            bool operator==(const Octave& other) const = default;
        };

        /// The octave a mesh of `triangles` falls in: the one whose lowest count is the largest power of two at or
        /// under it.
        static constexpr std::size_t octaveOf(std::uint32_t triangles)
        {
            const auto octave = static_cast<std::size_t>(std::bit_width(triangles == 0 ? 1u : triangles)) - 1;
            return octave < sOctaves ? octave : sOctaves - 1;
        }

        /// One structure of a mesh of `triangles`, built at `builtBytes` and answered at `tightBytes`.
        void add(std::uint32_t triangles, std::uint64_t builtBytes, std::uint64_t tightBytes);

        const std::array<Octave, sOctaves>& getOctaves() const { return mOctaves; }

        bool operator==(const StructureTightness& other) const = default;

    private:
        std::array<Octave, sOctaves> mOctaves{};
    };

    /// The octaves any structure was answered in, a line each, as a report prints them: the triangles' range, how many,
    /// the bytes a triangle as built and as tight, and the share tight keeps. Empty where none was.
    std::string describe(const StructureTightness& tightness);
}
