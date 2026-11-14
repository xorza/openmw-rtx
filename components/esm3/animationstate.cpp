#include "animationstate.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{
    bool AnimationState::empty() const
    {
        return mScriptedAnims.empty();
    }

    void AnimationState::load(ESMReader& esm)
    {
        mScriptedAnims.clear();

        while (esm.isNextSub("ANIS"))
        {
            ScriptedAnimation anim;

            anim.mGroup = esm.getHString();
            esm.getHNOT(anim.mTime, "TIME");
            esm.getHNOT(anim.mAbsolute, "ABST");
            esm.getHNT(anim.mLoopCount, "COUN");

            mScriptedAnims.push_back(std::move(anim));
        }
    }

    void AnimationState::save(ESMWriter& esm) const
    {
        for (const auto& mScriptedAnim : mScriptedAnims)
        {
            esm.writeHNString("ANIS", mScriptedAnim.mGroup);
            if (mScriptedAnim.mTime > 0)
                esm.writeHNT("TIME", mScriptedAnim.mTime);
            if (mScriptedAnim.mAbsolute)
                esm.writeHNT("ABST", mScriptedAnim.mAbsolute);
            esm.writeHNT("COUN", mScriptedAnim.mLoopCount);
        }
    }
}
