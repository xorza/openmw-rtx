#pragma once

#include <cstdint>
#include <optional>

namespace Rtx
{
    /// How long one copy of the sprite tile list is, and how much of it a bin may fill, both off
    /// one high-water mark that grows and never shrinks. A frame the sizing misjudges is slow and
    /// not wrong: the tiles past the room fall back to `SPRITE_TILE_UNBINNED`, and the next frame
    /// is sized to what the device reported.
    class SpriteListSize
    {
    public:
        /// The most room the runs are ever given, so a report the device wrote cannot ask for an
        /// allocation no card has: sixty-four megabytes, room for four hundred thousand drops of
        /// rain over Balmora.
        static constexpr std::uint32_t sMostEntries = 16u << 20;

        /// What share of the frame's tiles a sprite is given room for before any bin has said what
        /// it needs: one tile in this many. A share and not a count, because a sprite's rectangle
        /// grows with the tile count.
        static constexpr std::uint32_t sFloorShare = 64;

        /// Moves the mark for the frame about to be binned: `tiles` is what `Shaders::spriteTilesIn`
        /// says the camera covers, and `reported` what the last bin into this copy came to, whether
        /// or not it fit, where its report has been waited for.
        ///
        /// **Twice the report, and the share of the tiles only until the first report lands**, the
        /// one time there is no figure. Applied on every frame, the share gave a storm a thousand
        /// times the entries it used, and the mark keeps what it gives. What it gave in return is
        /// room on a storm's first frame, which a report two frames old does not: that frame walks
        /// its overflowing tiles unbinned, and the next is sized to what it reported.
        void sizeFor(std::uint32_t tiles, std::uint32_t sprites, std::optional<std::uint32_t> reported);

        /// What the pass is told it has room for after the starts.
        std::uint32_t getCapacity() const { return mCapacity; }

        /// Entries the buffer must hold: the starts and the capacity together.
        std::uint64_t getEntries() const { return std::uint64_t{ mTiles } + 1 + mCapacity; }

        /// What that comes to in bytes, which is what the buffer is grown to.
        std::uint64_t getBytes() const { return getEntries() * sizeof(std::uint32_t); }

    private:
        std::uint32_t mTiles = 0;
        std::uint32_t mCapacity = 0;

        /// Whether a report has landed, after which the share of the tiles is not given again.
        bool mReported = false;
    };
}
