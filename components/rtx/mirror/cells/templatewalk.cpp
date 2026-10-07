#include "templatewalk.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <osg/Drawable>
#include <osg/Matrixf>
#include <osg/Sequence>
#include <osg/StateSet>
#include <osg/Switch>
#include <osg/Transform>

#include <components/misc/constants.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/mirror/materialresolver.hpp>
#include <components/rtx/mirror/worlddescent.hpp>
#include <components/rtx/scene/meshtable.hpp>

#include "prepared.hpp"

namespace Rtx
{
    namespace
    {
        /// Appends `values` to `into` and answers the run they landed in. One statement, because a
        /// run that named where it started and a count taken from another array is exactly what
        /// `Rtx::Run` exists to stop.
        template <class T>
        Run appended(std::vector<T>& into, std::span<const T> values)
        {
            const Run run{ .mOffset = static_cast<std::uint32_t>(into.size()),
                .mCount = static_cast<std::uint32_t>(values.size()) };
            into.insert(into.end(), values.begin(), values.end());

            return run;
        }

        /// The modes child `at` of a day-night switch is shown in: each of the game's where
        /// `DayNightCallback` picks it, and `Authored` where it is the child the file opens on.
        NightDayModes shownIn(const osg::Switch& branches, const unsigned int at)
        {
            NightDayModes shown{ .mBits = 0 };
            if (branches.getValue(at))
                shown = NightDayModes::only(NightDayMode::Authored);

            for (const NightDayMode mode :
                { NightDayMode::Default, NightDayMode::ExteriorNight, NightDayMode::InteriorDay })
            {
                const auto index = static_cast<unsigned int>(mode);
                if ((branches.getNumChildren() > index ? index : 0u) == at)
                    shown = shown | NightDayModes::only(mode);
            }

            return shown;
        }
    }

    TemplateWalk::TemplateWalk(const osg::StateSet* const above)
        : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
        , mAbove(above)
    {
    }

    void TemplateWalk::read(const osg::Node& root, const osg::Node::NodeMask mask, PreparedModel& into)
    {
        mInto = &into;
        mHere = osg::Matrix();
        mModes = NightDayModes{};
        mShading.clear();
        if (mAbove != nullptr)
            pushShading(*mAbove);
        setTraversalMask(mask);

        // OSG's visitor API is non-const throughout, and this walk writes nothing: the cast happens
        // once, here.
        const_cast<osg::Node&>(root).accept(*this);

        mInto = nullptr;
    }

    void TemplateWalk::refuse(std::string_view why)
    {
        if (mInto->mRefused.empty())
            mInto->mRefused.assign(why);
    }

    void TemplateWalk::pushShading(const osg::StateSet& stateSet)
    {
        mShading.push_back(Shading::under(mShading, stateSet, false, nullptr));
    }

    void TemplateWalk::apply(osg::Node& node)
    {
        // Left out as the frame's walk leaves it out, which says so where it is met.
        if (const osg::StateSet* own = node.getStateSet(); own != nullptr && drawsIntoDistortion(*own))
            return;

        const std::size_t held = mShading.size();

        if (const osg::StateSet* own = node.getStateSet())
            pushShading(*own);

        descend(node);

        mShading.resize(held);
    }

    void TemplateWalk::apply(osg::Transform& node)
    {
        // The visitor goes with it, as the frame's walk hands itself over. A visitor that is
        // not a cull visitor takes the branch a null one would in every transform this tree has —
        // an `AutoTransform` included, which the frame's walk turns toward its eye and this one
        // leaves at its base rotation: no vanilla static carries a billboard, so the ring has no
        // eye to turn one to.
        const osg::Matrix above = mHere;
        node.computeLocalToWorldMatrix(mHere, this);

        apply(static_cast<osg::Node&>(node));

        mHere = above;
    }

    void TemplateWalk::apply(osg::Drawable& drawable)
    {
        if (const osg::StateSet* own = drawable.getStateSet(); own != nullptr && drawsIntoDistortion(*own))
            return;

        const std::size_t held = mShading.size();

        if (const osg::StateSet* own = drawable.getStateSet())
            pushShading(*own);

        take(drawable);

        mShading.resize(held);
    }

    void TemplateWalk::take(const osg::Drawable& drawable)
    {
        const DrawableRead read = readDrawable(drawable, mKinds.of(drawable));
        if (read.mGeometry == nullptr)
            return;

        MeshReading reading;
        const Misc::Result<bool, std::string> readMesh = mMeshes.read(mContent.mPreprocessor, read, reading);
        if (!readMesh.isOk())
        {
            refuse(readMesh.error());
            return;
        }
        if (!readMesh.value())
            return;

        // Here on the reader's thread, and not at the adoption, which is inside the frame's walk.
        if (const Misc::Result<void, std::string> fits = MeshTable::checkFits(reading.mArrays); !fits.isOk())
        {
            refuse(fits.error());
            return;
        }

        PreparedModel& into = *mInto;

        PreparedPart part;
        part.mDrawable = &drawable;
        part.mMaterial = MaterialResolver::read(mShading, mContent);
        const auto chained = static_cast<std::uint32_t>(into.mChains.size());
        MaterialResolver::chainOf(mShading, into.mChains);
        part.mChain = Run{ .mOffset = chained, .mCount = static_cast<std::uint32_t>(into.mChains.size()) - chained };
        part.mLocal = osg::Matrixf(mHere);
        part.mShape = reading.mShape;
        part.mModes = mModes;
        into.mEmits = into.mEmits || (part.mMaterial.mDescribed.has_value() && part.mMaterial.mDescribed->emits());

        part.mVertices = appended(into.mPositions, reading.mArrays.mPositions);
        part.mNormals = appended(into.mNormals, reading.mArrays.mNormals);
        part.mTexCoords = appended(into.mTexCoords, reading.mArrays.mTexCoords);
        part.mSecondTexCoords = appended(into.mSecondTexCoords, reading.mArrays.mSecondTexCoords);
        part.mUnitStreams = reading.mArrays.mUnitStreams;
        part.mColours = appended(into.mColours, reading.mArrays.mColours);
        part.mTangents = appended(into.mTangents, reading.mArrays.mTangents);
        part.mIndices = appended(into.mIndices, reading.mArrays.mIndices);

        into.mParts.push_back(std::move(part));
    }

    /// The frame the sequence stands on, and no step. The frame's walk runs a flipbook's clock
    /// and then walks the frame it settled on; a template's clock is nobody's to run, so what a
    /// distant fire shows is the frame its file was authored to open on, which is what the
    /// rasterizer's paging shows of it too. A day-night switch is the other way round: every
    /// branch, because the ring is told the mode and the template never is.
    void TemplateWalk::descend(osg::Node& node)
    {
        osg::Switch* const branches = node.asSwitch();
        if (branches == nullptr || branches->getName() != Constants::NightDayLabel)
        {
            descendInWorld(
                node, mKinds.of(node), *this, [](osg::Sequence&) {}, [](unsigned int) {});
            return;
        }

        const NightDayModes above = mModes;
        for (unsigned int at = 0; at < branches->getNumChildren(); ++at)
        {
            // A branch no mode shows — past the third, and not the one the file opens on — would
            // be read to stand never.
            mModes = above & shownIn(*branches, at);
            if (!mModes.isNone())
                branches->getChild(at)->accept(*this);
        }
        mModes = above;
    }
}
