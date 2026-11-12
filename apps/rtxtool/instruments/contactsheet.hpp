#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Rtx
{
    struct TextureData;
    struct TextureRow;
}

namespace RtxTool
{
    /// A drawn contact sheet, before anything has decided what to do with it.
    struct ContactSheet
    {
        std::vector<std::uint8_t> mPixels;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;

        /// How many textures were drawn, which is how many pairs the grid holds.
        std::uint32_t mCount = 0;

        /// Where a pair's left thumbnail starts, in pixels from the top left of the sheet.
        ///
        /// Exposed so a test can read what was drawn rather than trusting the picture. The de-lit
        /// half of the same pair starts `getStride()` further right.
        std::uint32_t getLeftOf(std::uint32_t index) const;
        std::uint32_t getTopOf(std::uint32_t index) const;

        /// How far apart the two halves of a pair stand.
        static std::uint32_t getStride();

        /// How wide one thumbnail is drawn.
        static std::uint32_t getThumbnail();
    };

    /// Draws every texture a cell uses, vanilla beside de-lit, as one image.
    ///
    /// De-lighting is the one thing in this renderer a number cannot judge: an estimate that removes the
    /// light painted into a texture and one that removes its detail move every metric the same way, and
    /// only a side by side look tells them apart. The thumbnails are nearest-neighbour on purpose, as a
    /// smoothed one would hide the loss of detail this exists to find. A painted colour is delit as its
    /// chain is (`ChainBuild`); every other texture stands beside itself.
    ContactSheet drawContactSheet(std::span<const Rtx::TextureData> textures);

    /// Appends what each pair of the sheet shows, in the order `drawContactSheet` draws them: the sheet's
    /// legend, which it carries no lettering for (each slot's `TextureRow::getName`). By the slot each
    /// description names, because a slot the table freed is described as nothing and a legend counted along
    /// the table named every later thumbnail one slot too far on.
    ///
    /// @param rows the table the descriptions were made from, which the views point into.
    void listSheetNames(
        std::span<const Rtx::TextureData> drawn, std::span<const Rtx::TextureRow> rows, std::vector<std::string>& into);
}
