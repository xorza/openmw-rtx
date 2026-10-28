#pragma once

namespace Rtx
{
    class Buffer;
    class DeviceScene;
    class TraceMedia;

    /// What one trace is of and where its results go but for the chain's own images: the scene
    /// and the media, and the census and glare counts it sums into. What `TraceMedia::describe`
    /// answers, the same for a frame and a picture inside the interface but for the arguments.
    struct TraceSubject
    {
        /// What the rays meet, at the copy of its tables the scene's last placement wrote: the
        /// structure, the tables a hit reads through the frame block, and the texture array. Read
        /// where it is used, so no two launches of a trace can be handed two copies of it.
        const DeviceScene* mScene = nullptr;

        /// What every trace reads beside its scene: the sea, the wake in it and the fog's field. One
        /// for everything traced.
        const TraceMedia* mMedia = nullptr;

        /// What the trace sums its census into: the frame's own, or for a picture inside the
        /// interface one nothing reads — bound because the shader writes it regardless.
        const Buffer* mCounts = nullptr;

        /// Whether this camera draws sprites. One that draws none still bins its tiles, with no
        /// sprite in them, so each learns which media and additive surfaces a ray through it can
        /// meet (`TraceChain::record`).
        bool mDrawsSprites = true;

        /// The two counts the eye's launch adds to: the frame's, `SunGlarePass::getCounts`, or
        /// for a picture inside the interface its own that nothing reads, as with the census.
        const Buffer* mSunGlare = nullptr;

        /// Whether this trace has a sea: a water surface in the scene the eye can meet, or a level
        /// the camera can be under, which a cell with no surface can still submerge the eye in.
        /// The one answer the synthesis, the ripple step and `HAS_SEA` all read, so the kernel
        /// never samples tiles that nothing wrote this frame.
        bool mSea = false;

        /// Whether the scene places a material that wears a map (`InstanceCounts::mMapped`), which
        /// is the only way a surface comes to have a lobe. The one answer `HAS_MAPS` and the glossy
        /// filter both read, so the filter runs over every frame whose kernel can reflect.
        bool mMapped = false;

        /// Whether the scene holds a lamp, which the shadow denoiser reads: a lamp's light is one
        /// of the two it shadows, the sky's source the other.
        bool mLamps = false;
    };
}
