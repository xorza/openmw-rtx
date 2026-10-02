#pragma once

#include <span>

namespace osg
{
    class StateSet;
}

namespace Rtx
{
    struct SurfaceDescription;

    /// How much of an actor there is at a point of a chain, as the rasterizer's state stack resolves
    /// its two uniforms: each replaced by the nearest state set that states it, and a fragment
    /// multiplied by `alpha * actorFade`.
    struct Fade
    {
        /// What a placement here is faded by: the actor's `alpha * actorFade` where the actor's
        /// root is the nearest to state an `alpha`, and its `actorFade` alone under a nearer state
        /// set that states its own — a VFX root, an `NiAlphaController` — whose `alpha` the
        /// material reads.
        float mPlacement = 1.0f;

        /// The nearest `actorFade`, which a nearer `alpha` leaves in force.
        float mActor = 1.0f;
    };

    /// One state set in the chain that shades a drawable, nearest it last. Not simply a node's own,
    /// because OpenMW animates shading with a `SceneUtil::StateSetUpdater`'s state set that belongs
    /// to the traversal.
    struct Shading
    {
        const osg::StateSet* mStateSet = nullptr;

        /// How much of an actor there is at this point of the chain, resolved as the chain is built
        /// rather than per drawable, which would ask each state set for two uniforms by a string
        /// made on the spot.
        Fade mFade;

        /// Whether a controller rewrote this since the last frame, so `MaterialResolver::resolve`
        /// reads a known state set again instead of handing back the slot it already has.
        bool mAnimated = false;

        /// Whether this state set or any above it is a controller's, resolved as the chain is built
        /// the way `mFade` is, rather than counted beside it where the two could part.
        ///
        /// **What stands under a controller is animated by it.** `SceneUtil::addEnchantedGlow` hangs
        /// its sheet on an instance's root, above the shape the sheet is read into, and that shape's
        /// own state set is shared by every instance of the model — so a material keyed on it stands
        /// for the enchanted sword and the plain one beside it at once. `MaterialResolver::animate`
        /// is what this is asked for.
        bool mAnimatedThrough = false;
    };

    /// Whether a controller's state set stands anywhere on `shading` — `Shading::mAnimatedThrough`
    /// at the near end, which is that question resolved as the chain was built rather than asked of
    /// each link. What a material keyed on the near end has to know, and what says a sprite's
    /// reading is this frame's rather than the one held.
    inline bool animatedThrough(std::span<const Shading> shading)
    {
        return !shading.empty() && shading.back().mAnimatedThrough;
    }

    /// What the content said this surface is, folded out of the chain of state sets in force at it,
    /// root first and nearest last, which is how OpenGL resolves the same chain — a parent's
    /// `OVERRIDE` included, through one `SurfaceLocks` carried down it. False where no state set
    /// on the chain carried a material or a texture — the sky, the water, a debug line — and
    /// `material` is then the defaults.
    bool describeSurface(std::span<const Shading> shading, SurfaceDescription& material);

    /// How much of an actor there is under `stateSet`, `inherited` from above it: the pair of
    /// uniforms `MWRender::TransparencyUpdater` writes sets both, and an `alpha` alone replaces the
    /// actor's, which the material then reads (`describeStateSet`), and keeps its `actorFade`.
    Fade fadeThrough(const osg::StateSet& stateSet, const Fade& inherited);
}
