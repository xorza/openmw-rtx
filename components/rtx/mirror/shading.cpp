#include "shading.hpp"

#include <string>

#include <osg/FrontFace>
#include <osg/StateAttribute>
#include <osg/StateSet>
#include <osg/Uniform>

#include <components/rtx/scene/surface.hpp>

#include "chainkeys.hpp"
#include "statereading.hpp"

namespace Rtx
{
    bool describeSurface(std::span<const Shading> shading, SurfaceDescription& material)
    {
        SurfaceLocks locks;
        bool said = false;
        for (const Shading& link : shading)
            said = describeStateSet(*link.mStateSet, material, locks) || said;

        return said;
    }

    Shading Shading::under(
        const std::span<const Shading> chain, const osg::StateSet& stateSet, const bool animated, ChainKeys* const keys)
    {
        const Shading* const above = chain.empty() ? nullptr : &chain.back();
        const bool states = describesAnything(stateSet);
        const osg::StateSet* const key = keys != nullptr
            ? keys->join(above != nullptr ? above->mMaterialKey : nullptr, stateSet, animated, states)
            : nullptr;

        Shading link{
            .mStateSet = &stateSet,
            .mFade = fadeThrough(stateSet, above != nullptr ? above->mFade : Fade{}),
            .mAnimated = animated,
            .mAnimatedThrough = animated || (above != nullptr && above->mAnimatedThrough),
            .mClockwise = above != nullptr && above->mClockwise,
            .mClockwiseLocked = above != nullptr && above->mClockwiseLocked,
            .mStates = states,
            .mMaterialKey = key,
        };

        if (const osg::StateSet::RefAttributePair* front = stateSet.getAttributePair(osg::StateAttribute::FRONTFACE);
            front != nullptr && (!link.mClockwiseLocked || (front->second & osg::StateAttribute::PROTECTED) != 0))
        {
            link.mClockwise
                = static_cast<const osg::FrontFace*>(front->first.get())->getMode() == osg::FrontFace::CLOCKWISE;
            link.mClockwiseLocked = link.mClockwiseLocked || (front->second & osg::StateAttribute::OVERRIDE) != 0;
        }

        return link;
    }

    Fade fadeThrough(const osg::StateSet& stateSet, const Fade& inherited)
    {
        // Asked of the list before the name, because nearly every state set in the world has no
        // uniform at all. `osg::StateSet::getUniform` searches a `std::map` keyed on
        // `std::string`, and this is called at every node and every drawable a walk enters — a
        // tree walk and a `memcmp` apiece, tens of thousands of times a frame. What writes the two
        // uniforms below is `MWRender::TransparencyUpdater`, on the handful of actors the game is
        // fading, so the empty answer is the answer almost every time and it costs one load.
        if (stateSet.getUniformList().empty())
            return inherited;

        // Named once for the process, and not a `std::string` built for every state set of every
        // drawable's chain, every frame.
        static const std::string sActorFade("actorFade");
        static const std::string sAlpha("alpha");

        const osg::Uniform* const fade = stateSet.getUniform(sActorFade);
        const osg::Uniform* const hidden = stateSet.getUniform(sAlpha);
        if (fade == nullptr)
            return hidden != nullptr ? Fade{ .mPlacement = inherited.mActor, .mActor = inherited.mActor } : inherited;

        float actorFade = 1.0f;
        float alpha = 1.0f;
        fade->get(actorFade);
        if (hidden != nullptr)
            hidden->get(alpha);

        return Fade{ .mPlacement = actorFade * alpha, .mActor = actorFade };
    }

    bool drawsIntoDistortion(const osg::StateSet& stateSet)
    {
        // The mode first, because nearly every state set inherits its bin and a name compared per
        // node a walk enters would be paid by all of them.
        return stateSet.getRenderBinMode() != osg::StateSet::INHERIT_RENDERBIN_DETAILS
            && stateSet.getBinName() == "Distortion";
    }
}
