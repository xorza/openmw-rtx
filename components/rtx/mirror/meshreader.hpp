#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <osg/BoundingBox>
#include <osg/Geometry>
#include <osg/Matrix>
#include <osg/Node>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/ref_ptr>

#include <components/misc/result.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/preprocess/shape/shapefold.hpp>
#include <components/rtx/scene/mesh.hpp>

#include "nodekind.hpp"

namespace osg
{
    class Drawable;
    class Geometry;
}

namespace SceneUtil
{
    class MorphGeometry;
    class RigGeometry;
}

namespace Rtx
{
    class ContentPreprocessor;

    /// What of a drawable there is to mirror: the geometry its triangles and attributes are read
    /// from, and what poses it. A skinned body's geometry is its source — the bind pose, which a
    /// pose is computed from on the device — and so is a morphed face's, with the base target for
    /// its positions. Neither is the double-buffered copy a cull writes, which nothing runs here.
    struct DrawableRead
    {
        const osg::Geometry* mGeometry = nullptr;
        Deform mDeform = Deform::None;
        const SceneUtil::RigGeometry* mRig = nullptr;
        const SceneUtil::MorphGeometry* mMorph = nullptr;
    };

    /// Reads what a drawable is, in one virtual call for nearly everything in a cell. A skinned
    /// body and a morphed face are an `osg::Drawable` over a source geometry, and the source is
    /// what this reads. A rig no update traversal resolved is read as it stands, because its bones
    /// are what `RigGeometry::updateBounds` finds, and the rasterizer draws it in its bind pose
    /// too. A morph with no target past its base is a static mesh whose positions are the base.
    DrawableRead readDrawable(const osg::Drawable& drawable, NodeKind kind);

    /// A morph's base target, which `MorphGeometry::cull` reads its positions from.
    std::span<const osg::Vec3f> morphBase(const SceneUtil::MorphGeometry& morph);

    /// The model a walk standing at the end of `path` is inside: the nearest node the loader marked
    /// as a file's root (`Misc::OsgUserValues::sFileHash`), or null where none is. A clone carries
    /// its template's mark, so a template and every clone of it answer alike.
    const osg::Node* modelOf(std::span<osg::Node* const> path);

    /// What one drawable's triangles come to once read and shaped — `ShapePass`: spans into the
    /// geometry's own arrays and into the reader's scratch, valid until the reader reads again.
    struct MeshReading
    {
        /// The vertices with any the split added, and the triangles the fold kept. An attribute the
        /// geometry carries none of — or one of another length — comes back empty.
        MeshArrays mArrays;

        FoldedShape mShape;
    };

    /// Turns a drawable into a `MeshReading`: the half of a mesh's arrival that reads and shapes,
    /// apart from the half that inserts, because the ring does the first on a thread of its own
    /// and what a shape is has to be one answer. Not thread-safe, and one instance a thread.
    class MeshReader
    {
    public:
        MeshReader();
        ~MeshReader();

        MeshReader(const MeshReader&) = delete;
        MeshReader& operator=(const MeshReader&) = delete;

        /// Reads `read` into `into`, shaped by `content`, this thread's, answering whether the
        /// drawable held a triangle to read, and an error for a face this cannot build, saying why:
        /// lines and points, which a ray has no width of to meet,
        /// a morph's base that is not the length of its source, a triangle naming a vertex the
        /// drawable does not have, an array of normals, coordinates or colours of another length
        /// than the vertices, or an array of a type this does not read.
        ///
        /// @param model the model the drawable stands in (`modelOf`), whose other shapes the split
        ///        cuts its normals across — `CreaseSplit::Beside` — or null for none.
        Misc::Result<bool, std::string> read(
            ContentPreprocessor& content, const DrawableRead& read, const osg::Node* model, MeshReading& into);

    private:
        class BesideWalk;

        /// Where one geometry of a model stands in it, and whether a controller or a callback
        /// moves it there.
        struct ModelShape
        {
            const osg::Geometry* mGeometry = nullptr;
            osg::Matrix mPlace;
            bool mMoved = false;
        };

        /// Collects `geometry`'s triangles into `into`, degenerate ones left out, and answers
        /// whether any is left. An error where one names a vertex at or past `vertices`.
        static Misc::Result<bool, std::string> collectTriangles(
            const osg::Geometry& geometry, std::size_t vertices, std::vector<std::uint32_t>& into);

        /// Fills the beside scratch with the triangles of `model`'s other shapes that stand where
        /// `self` stands and reach into `reach`, `self`'s own box: positions and normals are matched
        /// bit for bit, which only shapes in one space can share. Nothing where `self` moves, or
        /// stands in the model more than once.
        void gatherBeside(const osg::Node& model, const osg::Geometry& self, const osg::BoundingBoxf& reach);

        /// What `geometry`, a shape of the model `gatherBeside` stands in, comes to as faces beside
        /// another: its runs of `mFacePositions` and `mFaceNormals`, and of `mFaceTriangles` in its own
        /// numbering, and the box of its vertices.
        /// Not usable where it cannot be read, names no normal, holds a vertex that is not a finite
        /// number, or no triangle: a shape that cuts nothing is only beside.
        struct ShapeFaces
        {
            osg::ref_ptr<const osg::Geometry> mGeometry;
            Run mVertices;
            Run mTriangles;
            osg::BoundingBoxf mBox;
            bool mUsable = false;
        };

        /// The faces of `geometry`, read the first time a read of the model the faces are of asks.
        /// Good until the next call, which may move every entry.
        const ShapeFaces& facesOf(const osg::Geometry& geometry);

        /// The triangles as the drawable names them, and the ones the fold kept. Refilled per
        /// drawable rather than reallocated, because a cell is tens of thousands of them.
        std::vector<std::uint32_t> mTriangleScratch;
        std::vector<std::uint32_t> mIndexScratch;

        /// What the shape pass split: the normals where any changed, and the vertex each added one
        /// copies — `ShapePass::Output`.
        std::vector<osg::Vec3f> mNormalScratch;
        std::vector<std::uint32_t> mSourceScratch;

        /// A mesh's attributes with the split's added vertices on the end, for a mesh that has any:
        /// the geometry's own arrays cannot grow. Apart from `mColourScratch`, which the copies are
        /// read from.
        std::vector<osg::Vec3f> mPositionScratch;
        std::vector<osg::Vec2f> mTexCoordScratch;
        std::vector<osg::Vec2f> mSecondTexCoordScratch;
        std::vector<osg::Vec3f> mCopiedColourScratch;
        std::vector<osg::Vec4f> mTangentScratch;

        /// Where an overall normal is spread across a drawable's vertices, and where normals,
        /// tangents or coordinates with one that is not finite are copied (`finiteOrNone`).
        std::vector<osg::Vec3f> mReadNormalScratch;
        std::vector<osg::Vec4f> mReadTangentScratch;
        std::vector<osg::Vec2f> mReadTexCoordScratch;
        std::vector<osg::Vec2f> mReadSecondTexCoordScratch;

        /// The model's shapes, and what stands beside the drawable read: the faces the split cuts
        /// across (`CreaseSplit::Beside`). Refilled per drawable read, like the rest.
        osg::ref_ptr<BesideWalk> mBesideWalk;
        std::vector<ModelShape> mModelShapes;
        std::vector<osg::Vec3f> mBesidePositions;
        std::vector<osg::Vec3f> mBesideNormals;
        std::vector<std::uint32_t> mBesideTriangles;
        std::vector<osg::Vec3f> mBesideReadScratch;
        std::vector<std::uint32_t> mBesideCollectScratch;

        /// **Each shape of a model read once as faces, however many of its drawables read beside
        /// it** (`facesOf`): a model of S shapes was S reads of up to S shapes each. The model's
        /// shapes are walked again for every drawable, which costs a visit a node and sees a shape
        /// the model gained. A shape's arrays are read once because nothing writes them after the
        /// loader: what deforms is a rig or a morph, which is no `osg::Geometry` and which the walk
        /// never collects. Kept for the model of the last read and dropped for the next one, held
        /// so that no address in them is another's while they stand.
        osg::ref_ptr<const osg::Node> mFacesOf;
        std::vector<ShapeFaces> mFaces;
        boost::unordered_flat_map<const osg::Geometry*, std::size_t> mFaceIndex;
        std::vector<osg::Vec3f> mFacePositions;
        std::vector<osg::Vec3f> mFaceNormals;
        std::vector<std::uint32_t> mFaceTriangles;

        /// Where a drawable's colours are decoded to. Always scratch, where the other
        /// attributes are usually the geometry's own arrays: what the file holds is display-encoded
        /// bytes and what a hit interpolates is linear light, so there is nothing to point at.
        std::vector<osg::Vec3f> mColourScratch;
    };
}
