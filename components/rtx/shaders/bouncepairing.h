#ifndef OPENMW_COMPONENTS_RTX_SHADERS_BOUNCEPAIRING_H
#define OPENMW_COMPONENTS_RTX_SHADERS_BOUNCEPAIRING_H

#include "hosttypes.h"
#include "portable.h"

// The pairings the bounce's spatial reuse takes its neighbours from: the textures' sizes, the disc
// their steps match, and how a pixel finds its texel and its partner. Included verbatim by both
// sides, for the reason `visibility.h` is: `Rtx::BouncePairing` makes the textures and the device
// reads them.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The disc whose mean distance the pairings' steps match (`BouncePairing`, a normal
    /// distribution of deviation `√(8 / 9π)` times the radius), as a share of the traced height, and
    /// never under a few pixels. **A share and not RTXDI's thirty-two pixels**, which are three per
    /// cent of a 1080p frame: a disc stated in pixels covers more of the scene the fewer pixels an
    /// upscaler traces, and a neighbour far across a surface reconnects at a length unlike the
    /// pixel's own. Held at thirty-two pixels over a corner traced at 128, a quarter of the frame,
    /// the spatial half raised the frame's error over the temporal half's by a quarter; at eight it
    /// lowered it by a tenth.
    const float BOUNCE_RADIUS_SHARE = 0.03f;
    const float BOUNCE_RADIUS_LEAST = 3.0f;

    /// The two pairing textures the spatial reuse takes its neighbours from, one a neighbour, of
    /// different sizes so their repeats do not line up (`BouncePairing`; ReSTIR PT Enhanced §3.2).
    const uint BOUNCE_PAIRING_SIZE_0 = 254u;
    const uint BOUNCE_PAIRING_SIZE_1 = 230u;

    /// The texel of a pairing texture `size` texels square that pixel `pixel` reads in a frame turned
    /// by `turn` and moved by `offset`: bit 0 reflects the texture across, bit 1 down, and bit 2
    /// transposes it. **A self-inverting texture must change every frame**, or a pixel and its
    /// partner would reuse each other and nobody else for ever; reflected, transposed and moved, the
    /// pairs stay pairs (`pairedStep`).
    RTX_SHADER uvec2 pairingTexel(uvec2 pixel, uint turn, uvec2 offset, uint size)
    {
        const uint across = ((turn & 4u) != 0u ? pixel[1] : pixel[0]) % size;
        const uint down = ((turn & 4u) != 0u ? pixel[0] : pixel[1]) % size;
        const uint x = (turn & 1u) != 0u ? size - 1u - across : across;
        const uint y = (turn & 2u) != 0u ? size - 1u - down : down;
        return uvec2((x + offset[0]) % size, (y + offset[1]) % size);
    }

    /// The step from a pixel to its partner under `turn`, for its texel's step `step`. A pixel's step
    /// `e` moves its texel by `e` transposed and then negated along each reflected axis; so the
    /// partner, whose texel is `step` away, stands `step` negated along each reflected axis and then
    /// transposed back. Its own texel's step is `-step`, which leads back.
    RTX_SHADER ivec2 pairedStep(ivec2 step, uint turn)
    {
        const int across = (turn & 1u) != 0u ? -step[0] : step[0];
        const int down = (turn & 2u) != 0u ? -step[1] : step[1];
        return (turn & 4u) != 0u ? ivec2(down, across) : ivec2(across, down);
    }

#ifdef RTX_HOST
}
#endif

#endif
