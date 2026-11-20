#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <osg/CopyOp>
#include <osg/Geometry>
#include <osg/Object>
#include <osg/Vec3f>

#include <components/contentstore/key.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtx/scene/foldedshape.hpp>

namespace Rtx
{
    /// **A geometry's fold and split as its model's derivation made them** (`ModelDerivation`): the triangles the fold
    /// kept, the normals where the split changed any and what each added vertex copies (`ShapePass::Output`), kept
    /// among the geometry's user objects, which a clone shares, so every reader of the drawable reads its shape off it
    /// and asks nothing of the store. Made once, before any reader sees its template, and never changed.
    class ModelFold final : public Resource::DerivedBytes
    {
    public:
        ModelFold() = default;

        /// The copy `META_Object`'s `clone` makes, as OpenSceneGraph copies every object; not assigned, as no
        /// referenced object is.
        ModelFold(const ModelFold& copy, const osg::CopyOp& op = osg::CopyOp::SHALLOW_COPY);
        ModelFold& operator=(const ModelFold&) = delete;

        META_Object(Rtx, ModelFold)

        /// The fold `geometry` carries, or null where its model derived none.
        static const ModelFold* of(const osg::Geometry& geometry);

        /// Keeps `fold` on `geometry`, which carries none yet.
        static void keep(osg::Geometry& geometry, const osg::ref_ptr<ModelFold>& fold);

        std::uint64_t getBytes() const override;

        /// The key of what the fold read (`ShapePass::digest`): a reader whose read keys otherwise folds it itself.
        ContentStore::Key mInput{};

        std::vector<std::uint32_t> mKept;
        std::vector<osg::Vec3f> mNormals;
        std::vector<std::uint32_t> mSources;
        FoldedShape mShape;

    protected:
        ~ModelFold() override = default;
    };
}
