#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_FIELDS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_FIELDS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// Where a pixel of the frame is stored: the trace and its denoiser keep the frame as two
// checkerboard fields, each a dense half of the stored image, and one mapping says which pixel is
// where (`docs/rtx/architecture.md`, "The frame stored as two dense half-images").
// Quake II RTX's `flat_to_checker` and `checker_to_flat`, with the swap and an odd width spelt out.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Which checkerboard field a pixel of the frame is in: the parity of its column and row, which
    /// `swap` turns over every frame so that a pixel takes each field in turn.
    RTX_SHADER uint fieldOf(uvec2 pixel, uint swap)
    {
        return (pixel[0] + pixel[1] + swap) & 1u;
    }

    /// The swap a frame is stored under: its number's parity, so that the fields turn over with
    /// every frame the renderer traces. **From the frame's number and not handed to every pass on
    /// its own**, since nearly every pass is told the number already; a history is read under the
    /// swap of the frame that wrote it (`TemporalTurns::Step::mWrittenBefore`), which a frame closed
    /// untraced or a number a run holds still leaves where the history was written.
    RTX_SHADER uint fieldSwapOf(uint frame)
    {
        return frame & 1u;
    }

    /// How many columns a field's half of the stored image holds: half the frame's width, rounded
    /// up, so that a frame of an odd width has one slot a row that no pixel is stored in.
    RTX_SHADER uint fieldColumns(uint width)
    {
        return (width + 1u) / 2u;
    }

    /// How wide the stored image of a frame `width` wide is: both fields' halves.
    RTX_SHADER uint storedWidth(uint width)
    {
        return 2u * fieldColumns(width);
    }

    /// Where `field`'s half keeps the place of `pixel`, at half its column: the pixel itself where
    /// it is in that field, and its neighbour along the row where it is not.
    RTX_SHADER uvec2 storedInField(uvec2 pixel, uint field, uint width)
    {
        return uvec2(pixel[0] / 2u + field * fieldColumns(width), pixel[1]);
    }

    /// Where a pixel of the frame is stored: in its field's half, the first field's on the left, at
    /// half its column. The same row, so a field's row is the frame's row with every other pixel.
    RTX_SHADER uvec2 storedOf(uvec2 pixel, uint swap, uint width)
    {
        return storedInField(pixel, fieldOf(pixel, swap), width);
    }

    /// How many workgroups `groupWidth` columns wide cover one field's half, which a dispatch over
    /// the stored image runs twice, so that no workgroup holds pixels of both fields: a wave then
    /// runs one field's work, as a split pixel's path is.
    RTX_SHADER uint fieldGroupsAcross(uint width, uint groupWidth)
    {
        return (fieldColumns(width) + groupWidth - 1u) / groupWidth;
    }

    /// The pixel of the frame stored at `stored`, which `storedOf` inverts. **Past the frame's width
    /// at the slot an odd width leaves empty**, which a pass over the stored image skips.
    RTX_SHADER uvec2 pixelOf(uvec2 stored, uint swap, uint width)
    {
        const uint columns = fieldColumns(width);
        const uint field = stored[0] >= columns ? 1u : 0u;
        const uint column = stored[0] - field * columns;
        return uvec2(2u * column + ((field + stored[1] + swap) & 1u), stored[1]);
    }

#ifdef RTX_HOST
}
#endif

#endif
