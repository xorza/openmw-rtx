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

#include <components/crashcatcher/crashnote.hpp>
#include <components/misc/constants.hpp>
#include <components/nifosg/autotransform.hpp>
#include <components/rtx/common/finite.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/mirror/materialresolver.hpp>
#include <components/rtx/mirror/worlddescent.hpp>
#include <components/rtx/scene/meshtable.hpp>

#include "prepared.hpp"

namespace Rtx
{
    namespace
    {
        /// The modes child `at` of a day-night switch is shown in: each of the game's where
        /// `DayNightCallback` picks it, and `Authored` where it is the child the file opens on.
        NightDayModes shownIn(const osg::Switch& branches, const std::uint32_t at)
        {
            NightDayModes shown{ .mBits = 0 };
            if (branches.getValue(at))
                shown = NightDayModes::only(NightDayMode::Authored);

            for (const NightDayMode mode :
                { NightDayMode::Default, NightDayMode::ExteriorNight, NightDayMode::InteriorDay })
            {
                const auto index = static_cast<std::uint32_t>(mode);
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

    void TemplateWalk::read(
        const osg::Node& root, const osg::Node::NodeMask mask, PreparedModel& into, ThreadContent& content)
    {
        mInto = &into;
        mContent = &content;
        mHere = osg::Matrix();
        mSinceTurn = osg::Matrix();
        mTurnPath.clear();
        mModes = NightDayModes{};
        mShading.clear();
        if (mAbove != nullptr)
            pushShading(*mAbove);
        setTraversalMask(mask);

        // OSG's visitor API is non-const throughout, and this walk writes nothing: the cast happens
        // once, here.
        const_cast<osg::Node&>(root).accept(*this);

        mInto = nullptr;
        mContent = nullptr;
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
        // The visitor goes with it, as the frame's walk hands itself over. A visitor that is not a cull visitor takes
        // the branch a null one would in every transform, an `AutoTransform` included, which stands here at the
        // rotation its file was authored with: **a billboard is a turn** the ring makes on every walk, toward the eye
        // the frame's walk turns the graph's to (`CellPlacer`), so what is below it is kept apart from what is above.
        const osg::Matrix above = mHere;
        const osg::Matrix sinceAbove = mSinceTurn;
        node.computeLocalToWorldMatrix(mHere, this);

        auto* const billboard = as<NifOsg::AutoTransform>(mKinds.of(node), NodeKind::Billboard, node);
        if (billboard != nullptr)
        {
            mTurnPath.push_back(PreparedTurn{ .mBillboard = billboard, .mAbove = osg::Matrixf(mSinceTurn) });
            mSinceTurn = osg::Matrix();
        }
        else
            node.computeLocalToWorldMatrix(mSinceTurn, this);

        apply(static_cast<osg::Node&>(node));

        if (billboard != nullptr)
            mTurnPath.pop_back();
        mSinceTurn = sinceAbove;
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
        // Under the model's own note, so a fault in a part's read, fold or split names the part.
        const Crash::NoteScope noted("reading the part \"{}\"", drawable.getName());
        const DrawableRead read = readDrawable(drawable, mKinds.of(drawable), mShading);
        if (read.mGeometry == nullptr)
            return;

        MeshReading reading;
        const Result<MeshRead, std::string> readMesh
            = mMeshes.read(mContent->mPreprocessor, read, modelOf(getNodePath()), reading);
        if (!readMesh.isOk())
        {
            refuse(readMesh.error());
            return;
        }
        if (readMesh.value() == MeshRead::Empty)
            return;

        // Here on the reader's thread, and not at the adoption, which is inside the frame's walk.
        if (const Result<void, std::string> fits
            = MeshTable::checkFits(reading.mArrays.mPositions.size(), reading.mArrays.mIndices.size());
            !fits.isOk())
        {
            refuse(fits.error());
            return;
        }

        // **A place in the model that is not finite stands nothing**, as the frame's walk refuses one
        // (`SceneExtractor`): the reference's own the cell reader checks, and the product of the two is
        // what the top level is built from.
        if (!isFinite(osg::Matrixf(mHere)))
        {
            refuse("its place is not a finite number");
            return;
        }

        PreparedModel& into = *mInto;

        PreparedPart part;
        part.mDrawable = &drawable;
        part.mMaterial = MaterialResolver::read(mShading, *mContent);
        const auto chained = static_cast<std::uint32_t>(into.mChains.size());
        MaterialResolver::chainOf(mShading, into.mChains);
        part.mChain = Run{ .mOffset = chained, .mCount = static_cast<std::uint32_t>(into.mChains.size()) - chained };
        part.mLocal = osg::Matrixf(mHere);
        part.mClockwise = clockwiseThrough(mShading);
        part.mModes = mModes;
        if (!mTurnPath.empty())
        {
            const auto turned = static_cast<std::uint32_t>(into.mTurns.size());
            into.mTurns.insert(into.mTurns.end(), mTurnPath.begin(), mTurnPath.end());
            part.mTurns
                = PartTurns{ .mTurns = Run{ .mOffset = turned, .mCount = static_cast<std::uint32_t>(mTurnPath.size()) },
                      .mBelow = osg::Matrixf(mSinceTurn) };
        }
        into.mEmits = into.mEmits || (part.mMaterial.mDescribed.has_value() && part.mMaterial.mDescribed->emits());

        part.mRuns = into.mStreams.append(reading.mArrays, reading.mShape);

        into.mParts.push_back(std::move(part));
    }

    /// The frame the sequence stands on, and no step: a template's clock is nobody's to run, so what a distant fire
    /// shows is the frame its file was authored to open on, as the rasterizer's paging shows it. A day-night switch is
    /// the other way round: every branch, because the ring is told the mode and the template never is.
    void TemplateWalk::descend(osg::Node& node)
    {
        osg::Switch* const branches = node.asSwitch();
        if (branches == nullptr || branches->getName() != Constants::NightDayLabel)
        {
            descendInWorld(node, mKinds.of(node), *this, [](osg::Sequence&) {}, [](std::uint32_t) {});
            return;
        }

        const NightDayModes above = mModes;
        for (std::uint32_t at = 0; at < branches->getNumChildren(); ++at)
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
