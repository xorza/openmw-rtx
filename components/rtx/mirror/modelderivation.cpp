#include "modelderivation.hpp"

#include <cassert>
#include <string>
#include <utility>

#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/Node>
#include <osg/NodeVisitor>
#include <osg/ref_ptr>

#include <components/contentstore/store.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>

#include "meshreader.hpp"
#include "modelfold.hpp"
#include "nodekind.hpp"

namespace Rtx
{
    struct ModelDerivation::Hand
    {
        explicit Hand(ContentStore::Store* store)
            : mContent(store, ContentStore::Reach::Disk)
        {
        }

        ContentPreprocessor mContent;
        MeshReader mReader;
        NodeKinds mKinds;
    };

    namespace
    {
        /// Every drawable of a model, every branch and every mask: what a reader may meet of it, however the game
        /// switches it, each folded as its reader would fold it, in the model its path names (`modelOf`).
        class FoldWalk final : public osg::NodeVisitor
        {
        public:
            FoldWalk(ContentPreprocessor& content, MeshReader& reader, const NodeKinds& kinds)
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
                , mContent(content)
                , mReader(reader)
                , mKinds(kinds)
            {
                setNodeMaskOverride(~0u);
            }

            void apply(osg::Drawable& drawable) override
            {
                const DrawableRead read = readDrawable(drawable, mKinds.of(drawable), {});
                if (read.mGeometry == nullptr || ModelFold::of(*read.mGeometry) != nullptr)
                    return;

                MeshReading reading;
                const Result<MeshRead, std::string> readMesh
                    = mReader.read(mContent, read, modelOf(getNodePath()), reading);
                // A face its reader refuses, or one with no triangle, is refused or skipped by every reader again,
                // and carries nothing.
                if (!readMesh.isOk() || readMesh.value() == MeshRead::Empty)
                    return;

                const osg::ref_ptr<ModelFold> fold = new ModelFold;
                fold->mInput = reading.mShapeKey;
                fold->mKept.assign(reading.mArrays.mIndices.begin(), reading.mArrays.mIndices.end());
                fold->mNormals.assign(reading.mSplitNormals.begin(), reading.mSplitNormals.end());
                fold->mSources.assign(reading.mSources.begin(), reading.mSources.end());
                fold->mShape = reading.mShape;
                // The template is this loader's alone until it is handed out, so its geometry is written here.
                ModelFold::keep(const_cast<osg::Geometry&>(*read.mGeometry), fold);
            }

        private:
            ContentPreprocessor& mContent;
            MeshReader& mReader;
            const NodeKinds& mKinds;
        };
    }

    ModelDerivation::ModelDerivation(ContentStore::Store* const store, const std::size_t threads)
        : mStore(store)
        , mThreads(threads)
    {
        assert(threads >= 1 && "no hand to derive with");
        mFree.reserve(threads);
    }

    ModelDerivation::~ModelDerivation()
    {
        assert(mFree.size() == mMade && "a derivation outlived by one still deriving");
    }

    void ModelDerivation::derive(osg::Node& model)
    {
        std::unique_ptr<Hand> hand;
        {
            std::unique_lock lock(mMutex);
            mFreed.wait(lock, [&] { return !mFree.empty() || mMade < mThreads; });
            if (!mFree.empty())
            {
                hand = std::move(mFree.back());
                mFree.pop_back();
            }
            else
                ++mMade;
        }
        if (hand == nullptr)
            hand = std::make_unique<Hand>(mStore);

        FoldWalk walk(hand->mContent, hand->mReader, hand->mKinds);
        model.accept(walk);

        const ContentStats stats = hand->mContent.takeStats();
        {
            const std::scoped_lock lock(mMutex);
            mStats += stats;
            mFree.push_back(std::move(hand));
        }
        mFreed.notify_one();
    }

    ContentStats ModelDerivation::takeStats()
    {
        const std::scoped_lock lock(mMutex);
        return std::exchange(mStats, ContentStats{});
    }
}
