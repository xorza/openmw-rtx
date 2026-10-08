#include "shading.hpp"

#include <osg/FrontFace>
#include <osg/StateAttribute>
#include <osg/StateSet>
#include <osg/Uniform>

#include <components/rtx/mirror/surfacedescription.hpp>

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
        // Called at every node and every drawable a walk enters, tens of thousands of times a
        // frame. What writes the two uniforms is `MWRender::TransparencyUpdater`, on the handful of
        // actors the game is fading, so nearly every list is empty and `findUniform` ends at once.
        const osg::StateSet::RefUniformPair* const fadeSet = findUniform(stateSet, "actorFade");
        const osg::StateSet::RefUniformPair* const hiddenSet = findUniform(stateSet, "alpha");
        const osg::Uniform* const fade = fadeSet != nullptr ? fadeSet->first.get() : nullptr;
        const osg::Uniform* const hidden = hiddenSet != nullptr ? hiddenSet->first.get() : nullptr;
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
