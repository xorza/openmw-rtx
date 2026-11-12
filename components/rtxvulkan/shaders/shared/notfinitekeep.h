#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_NOTFINITEKEEP_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_NOTFINITEKEEP_H

#include <components/rtx/shaders/digest.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

#include "channeldigest.h"

// What a counting device keeps of the first frame whose census counted a store that was not finite
// by the time the digest's images were read (`NotFiniteKeep`): the frame's images around the first
// texel that held one, read by the host once the run stops on it. The census's report comes back
// frames late, after later frames wrote over every one of them, so the device decides as the frame
// runs: `notfinitegate.comp` reads the census and sizes the two dispatches after it, nought on every
// frame but the one kept.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where the three modules bind what they read and write in set 0: the frame's images in the digest's
    /// runs (`DIGEST_FLOAT_SLOTS`, `DIGEST_WORD_SLOTS`), and the kept block.
    const uint KEEP_BIND_IMAGES = 0;
    const uint KEEP_BIND_WORDS = 1;
    const uint KEEP_BIND_KEPT = 2;

    /// How far the kept window reaches from its centre, and its side: what a filter's taps around the texel
    /// read, the widest wavelet level's included, is wider, and a window this size is the texel's
    /// neighbourhood at a glance.
    const uint KEEP_RADIUS = 4u;
    const uint KEEP_SIDE = 2u * KEEP_RADIUS + 1u;
    const uint KEEP_TEXELS = KEEP_SIDE * KEEP_SIDE;

    /// How many images a frame keeps: the digest's, every channel and every image of the denoiser
    /// (`Rtx::DenoiserImage`, which the host holds this to).
    const uint KEEP_DENOISER_IMAGES = 20u;
    const uint KEEP_IMAGES = DIGEST_IMAGES + KEEP_DENOISER_IMAGES;

    /// The texels of one `notfinitefind.comp` workgroup, a square on a side.
    const uint KEEP_FIND_WORKGROUP = 16u;

    /// What the device keeps, zeroed as it is made. The two dispatches' sizes first, where an indirect
    /// dispatch reads them (`VkDispatchIndirectCommand`).
    struct NotFiniteKept
    {
        uint mFind[3];
        uint mCopy[3];

        /// One once a frame was kept, which no later frame then replaces: the first is where a value not
        /// finite began, and every later one may be its spread.
        uint mKept;

        /// The kept frame's number, as the renderer counts its frames (`FrameResult::mFrame`), low word
        /// first, and the extent its images were read over.
        uint mFrameLow;
        uint mFrameHigh;
        uint mWidth;
        uint mHeight;

        /// The texel the window stands around, as `censusFirst` keys it: the first image's that held one,
        /// in the frame's order. Nought where no image held one: the stores the census counted were all
        /// written over by the digest's step, or were made by a pass that writes none of these images.
        uint mCentre;

        /// Each image's first texel that held one, as `censusFirst` keys it, nought for none; and one where
        /// the frame had the image. Only an image of floats is searched: an image of words holds whatever
        /// its pass packed.
        uint mFirst[KEEP_IMAGES];
        uint mTaken[KEEP_IMAGES];

        /// Each image's window, a row at a time from its top left, four words a texel: a float's bits as
        /// the shaders load the texel, or the words an image of words holds. Nought past the frame's edge.
        uint mTexels[KEEP_IMAGES * KEEP_TEXELS * 4u];
    };

    /// What `notfinitegate.comp` is told: the extent the images are read over, and the frame's number.
    struct NotFiniteGateConstants
    {
        uint mWidth;
        uint mHeight;
        uint mFrameLow;
        uint mFrameHigh;
    };

    /// What `notfinitefind.comp` and `notfinitekeep.comp` are told of one run of the digest's slots: the
    /// extent, which slots are bound and which float slots are stored in fields, as the digest is told
    /// (`DigestConstants`), the frame's sample number, which says how its fields lie, and which kept image
    /// each slot holds, the float slots' first.
    struct NotFiniteKeepConstants
    {
        uint mWidth;
        uint mHeight;
        uint mFloats;
        uint mFloatsInFields;
        uint mWords;
        uint mFrame;
        uint mKeptAt[DIGEST_SLOTS];
    };

#ifdef RTX_HOST
    static_assert(sizeof(NotFiniteGateConstants) == 16, "NotFiniteGateConstants must be scalar-packed on every side");
    static_assert(sizeof(NotFiniteKeepConstants) <= 128, "a push past what every device takes");
    static_assert(sizeof(NotFiniteKept) == 4 * (12 + 2 * KEEP_IMAGES + KEEP_IMAGES * KEEP_TEXELS * 4),
        "NotFiniteKept must be scalar-packed on every side");
    static_assert(sizeof(NotFiniteKeepConstants) == 4 * (6 + DIGEST_SLOTS),
        "NotFiniteKeepConstants must be scalar-packed on every side");
}
#endif

#endif
