#include "meshreader.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Array>
#include <osg/Drawable>
#include <osg/Geometry>
#include <osg/NodeVisitor>
#include <osg/Transform>
#include <osg/TriangleIndexFunctor>
#include <osg/UserDataContainer>
#include <osg/Vec4f>

#include <components/misc/osguservalues.hpp>
#include <components/rtx/common/finite.hpp>
#include <components/rtx/image/colour.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/shape/creasesplit.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/shader/automaps.hpp>

namespace Rtx
{
    namespace
    {
        /// Whether `geometry` draws lines or points: an `NiLines`, or a cloud of points.
        bool drawsLines(const osg::Geometry& geometry)
        {
            return std::ranges::any_of(geometry.getPrimitiveSetList(), [](const osg::ref_ptr<osg::PrimitiveSet>& set) {
                const GLenum mode = set->getMode();
                return mode == GL_POINTS || mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP;
            });
        }

        /// What a drawable with no triangle reads as: nothing to place, or, where it draws lines or
        /// points, a refusal saying so — the rasterizer draws them, an `NiLines`, and a ray has no
        /// width of theirs to meet.
        Result<bool, std::string> noTriangle(const osg::Geometry& geometry)
        {
            if (drawsLines(geometry))
                return Err{ std::string("its lines and points have no width a ray can meet") };
            return false;
        }

        /// `values` with the vertices a split added on the end, each a copy of its source, laid into
        /// `scratch` — or `values` itself where nothing was added or there is nothing to copy.
        template <class T>
        std::span<const T> withCopies(
            std::span<const T> values, std::span<const std::uint32_t> sources, std::vector<T>& scratch)
        {
            if (sources.empty() || values.empty())
                return values;

            scratch.assign(values.begin(), values.end());
            for (const std::uint32_t source : sources)
                scratch.push_back(values[source]);

            return scratch;
        }

        /// `given` where every element is a finite number, or a copy of it in `scratch` with every element that is not
        /// read as none: the zero the mesh table holds for a normal, a tangent or a coordinate the mesh did not bring.
        /// **Content wrote them, so a number that is not finite is data.** A NaN normal posed by a skin would put a
        /// store that is not finite into the mesh, where one of none is one the rest already reads; a coordinate that
        /// is not finite samples whichever texel the driver picks, and nought is the one every driver picks. Copied
        /// only where one is, so a mesh written well is read in place.
        template <class T>
        std::span<const T> finiteOrNone(const std::vector<T>& given, std::vector<T>& scratch)
        {
            const auto finite = [](const T& value) { return isFinite(value); };
            if (std::ranges::all_of(given, finite))
                return given;

            scratch.clear();
            scratch.reserve(given.size());
            for (const T& value : given)
                scratch.push_back(finite(value) ? value : T());
            return scratch;
        }

        struct TriangleCollector
        {
            std::vector<std::uint32_t>* mIndices = nullptr;

            /// The largest index collected, which `NifOsg` hands on as the file wrote it: nothing
            /// between the file and here holds a triangle to the vertices it has.
            std::uint32_t mLargest = 0;

            void operator()(unsigned int a, unsigned int b, unsigned int c)
            {
                if (a == b || b == c || a == c)
                    return;

                mLargest = std::max({ mLargest, a, b, c });
                mIndices->push_back(a);
                mIndices->push_back(b);
                mIndices->push_back(c);
            }
        };

        struct VertexArrays
        {
            std::span<const osg::Vec3f> mPositions;

            /// Empty only where the geometry names no normal at all. A per-vertex array is taken as
            /// it stands and a single overall one is spread across the vertices, which is the same
            /// answer at every point of a flat surface; a normal that is not finite is read as none
            /// (`finiteOrNone`).
            std::span<const osg::Vec3f> mNormals;
        };

        /// The array as a `Vec3Array`, or null where it is anything else. `osg::Array` states its
        /// own type in a byte, which is what a `dynamic_cast` walks the class hierarchy to work out.
        const osg::Vec3Array* asVec3Array(const osg::Array* array)
        {
            if (array == nullptr || array->getType() != osg::Array::Vec3ArrayType)
                return nullptr;

            return static_cast<const osg::Vec3Array*>(array);
        }

        /// A geometry's `what` array of `named` elements, against its `vertices`: an array of another
        /// length is one this cannot match to the vertices, and a guess at which belongs to which
        /// is a wrong picture rather than a missing one.
        Result<void, std::string> checkLength(std::string_view what, std::size_t named, std::size_t vertices)
        {
            if (named != vertices)
                return Err{ "it has " + std::to_string(named) + ' ' + std::string(what) + " for "
                    + std::to_string(vertices) + " vertices" };

            return {};
        }

        /// @param scratch for an overall normal spread across the vertices, or normals that are not
        ///        all finite. Refilled here and borrowed by the returned span, so it has to outlive
        ///        the read.
        Result<VertexArrays, std::string> readVertices(const osg::Geometry& geometry, std::vector<osg::Vec3f>& scratch)
        {
            VertexArrays arrays;

            // No array is no geometry, which the game draws nothing for either; an array of a type
            // this does not read is one it does.
            const osg::Array* vertices = geometry.getVertexArray();
            if (vertices == nullptr || vertices->getNumElements() == 0)
                return arrays;

            const osg::Vec3Array* positions = asVec3Array(vertices);
            if (positions == nullptr)
                return Err{ "its vertices are not three floats each" };

            arrays.mPositions = std::span(positions->asVector());

            const osg::Array* named = geometry.getNormalArray();
            if (named == nullptr || named->getNumElements() == 0)
                return arrays;

            const osg::Vec3Array* normals = asVec3Array(named);
            if (normals == nullptr)
                return Err{ "its normals are not three floats each" };

            // One normal for the whole drawable is a normal: `SceneUtil::createWaterGeometry` binds
            // exactly this, and dropping it made the sea flat black and took the exposure with it.
            // Element nought whatever the array's length, as OpenGL reads an overall array and as
            // `readColours` reads one.
            if (normals->getBinding() == osg::Array::BIND_OVERALL)
            {
                const osg::Vec3f& overall = normals->at(0);
                scratch.assign(positions->size(), isFinite(overall) ? overall : osg::Vec3f());
                arrays.mNormals = std::span(scratch);
                return arrays;
            }

            if (const Result<void, std::string> matched = checkLength("normals", normals->size(), positions->size());
                !matched.isOk())
                return Err{ matched.error() };

            arrays.mNormals = finiteOrNone(normals->asVector(), scratch);
            return arrays;
        }

        /// The array as a `Vec2Array`, or null where it is anything else. `asVec3Array` says why
        /// the type byte and not a `dynamic_cast`.
        const osg::Vec2Array* asVec2Array(const osg::Array* array)
        {
            if (array == nullptr || array->getType() != osg::Array::Vec2ArrayType)
                return nullptr;

            return static_cast<const osg::Vec2Array*>(array);
        }

        /// The coordinates bound at `unit`, or null where none are. An error where they are of a
        /// type this does not read, or where they do not match the vertices — `checkLength`.
        Result<const osg::Vec2Array*, std::string> readTexCoords(
            const osg::Geometry& geometry, unsigned int unit, std::size_t vertices)
        {
            const osg::Array* named = geometry.getTexCoordArray(unit);
            if (named == nullptr || named->getNumElements() == 0)
                return nullptr;

            const osg::Vec2Array* coords = asVec2Array(named);
            if (coords == nullptr)
                return Err{ "its texture coordinates are not two floats each" };

            if (const Result<void, std::string> matched = checkLength("texture coordinates", coords->size(), vertices);
                !matched.isOk())
                return Err{ matched.error() };

            return coords;
        }

        /// The tangents `Shader::MapVisitor` built at `Shader::sTangentUnit`, or none where it built
        /// none, and a tangent that is not finite read as none: the generator builds one from the
        /// content's normals. An error where they do not match the vertices — `checkLength`.
        ///
        /// @param scratch for tangents that are not all finite, borrowed as `readVertices` borrows
        ///        its own.
        Result<std::span<const osg::Vec4f>, std::string> readTangents(
            const osg::Geometry& geometry, std::size_t vertices, std::vector<osg::Vec4f>& scratch)
        {
            const osg::Array* named = geometry.getTexCoordArray(Shader::sTangentUnit);
            if (named == nullptr || named->getNumElements() == 0)
                return std::span<const osg::Vec4f>();

            if (named->getType() != osg::Array::Vec4ArrayType)
                return Err{ "its tangents are not four floats each" };

            const auto* tangents = static_cast<const osg::Vec4Array*>(named);
            if (const Result<void, std::string> matched = checkLength("tangents", tangents->size(), vertices);
                !matched.isOk())
                return Err{ matched.error() };

            return finiteOrNone(tangents->asVector(), scratch);
        }

        /// A geometry's per-vertex colours, decoded into `scratch` and spanned from it. Empty where the geometry names
        /// none. Two array types, because `NifOsg` builds a `Vec4Array` from a `NiGeometryData` and a `Vec4ubArray`
        /// from a `BSTriShape`. An overall colour is spread across the vertices, as `readVertices` spreads an overall
        /// normal. A colour that is not a finite number is read as white, the table's "no tint". The alpha is not read:
        /// three shapes in the whole of vanilla carry one below opaque, and reading it would put a fetch on every
        /// candidate of every shadow ray.
        ///
        /// @param vertices how many the geometry holds, which an array that is not overall has to match —
        ///        `checkLength`.
        Result<std::span<const osg::Vec3f>, std::string> readColours(
            const osg::Geometry& geometry, const std::size_t vertices, std::vector<osg::Vec3f>& scratch)
        {
            const osg::Array* colours = geometry.getColorArray();
            if (colours == nullptr || colours->getNumElements() == 0)
                return std::span<const osg::Vec3f>();

            const bool overall = colours->getBinding() == osg::Array::BIND_OVERALL;
            if (!overall)
            {
                const Result<void, std::string> matched = checkLength("colours", colours->getNumElements(), vertices);
                if (!matched.isOk())
                    return Err{ matched.error() };
            }

            const auto decoded = [](const auto& encoded) {
                const osg::Vec3f colour = decodeColour(encoded);
                return isFinite(colour) ? colour : osg::Vec3f(1.0f, 1.0f, 1.0f);
            };
            const auto decodeAll = [&](const auto& array) {
                if (overall)
                {
                    scratch.assign(vertices, decoded(array[0]));
                    return;
                }

                scratch.clear();
                scratch.reserve(vertices);
                for (std::size_t at = 0; at < vertices; ++at)
                    scratch.push_back(decoded(array[at]));
            };

            switch (colours->getType())
            {
                case osg::Array::Vec4ArrayType:
                    decodeAll(static_cast<const osg::Vec4Array&>(*colours));
                    break;
                case osg::Array::Vec4ubArrayType:
                    decodeAll(static_cast<const osg::Vec4ubArray&>(*colours));
                    break;
                default:
                    return Err{ "its colours are neither four floats nor four bytes each" };
            }

            return std::span<const osg::Vec3f>(scratch);
        }
    }

    DrawableRead readDrawable(const osg::Drawable& drawable, const NodeKind kind)
    {
        if (const osg::Geometry* geometry = drawable.asGeometry())
            return DrawableRead{ .mGeometry = geometry };

        if (const auto* rig = as<const SceneUtil::RigGeometry>(kind, NodeKind::RigGeometry, drawable))
        {
            // **Skinned by its content, not by whether an update found its skeleton yet**, so a template,
            // which no update reaches, and its clone in the scene read as one shape under one key.
            const bool skinned = rig->getInfluenceData() != nullptr;
            return DrawableRead{ .mGeometry = rig->getSourceGeometry().get(),
                .mDeform = skinned ? Deform::Rig : Deform::None,
                .mRig = rig };
        }

        if (const auto* morph = as<const SceneUtil::MorphGeometry>(kind, NodeKind::MorphGeometry, drawable))
        {
            const bool moving = morph->getMorphTargetList().size() > 1;
            return DrawableRead{ .mGeometry = morph->getSourceGeometry().get(),
                .mDeform = moving ? Deform::Morph : Deform::None,
                .mMorph = morph };
        }

        return DrawableRead{};
    }

    std::span<const osg::Vec3f> morphBase(const SceneUtil::MorphGeometry& morph)
    {
        // The source geometry's own array is what `NifOsg` built the drawable from and the two agree
        // in every file it builds, so the length is asserted where it is read against the source.
        const osg::Vec3Array* base = morph.getMorphTarget(0).getOffsets();
        assert(base != nullptr && "a morph whose base is no array");
        return std::span(base->asVector());
    }

    /// Every geometry under a model, with where it stands in the model's space and whether anything
    /// moves it there: a transform with an update callback, as a controller is, or a drawable with
    /// one. Every node, whatever its mask and whichever child a switch, a level of detail or a
    /// sequence shows, so a template and a clone the game has set otherwise answer alike.
    class MeshReader::BesideWalk final : public osg::NodeVisitor
    {
    public:
        BesideWalk()
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
        {
            setNodeMaskOverride(~0u);
        }

        void walk(const osg::Node& model, std::vector<ModelShape>& into)
        {
            mInto = &into;
            mHere = osg::Matrix();
            mMoved = false;

            // OSG's visitor API is non-const throughout, and this walk writes nothing.
            const_cast<osg::Node&>(model).accept(*this);

            mInto = nullptr;
        }

        void apply(osg::Transform& node) override
        {
            const osg::Matrix above = mHere;
            const bool moved = mMoved;
            node.computeLocalToWorldMatrix(mHere, this);
            mMoved = mMoved || node.getUpdateCallback() != nullptr;

            traverse(node);

            mHere = above;
            mMoved = moved;
        }

        void apply(osg::Drawable& drawable) override
        {
            if (const osg::Geometry* geometry = drawable.asGeometry())
                mInto->push_back(ModelShape{ .mGeometry = geometry,
                    .mPlace = mHere,
                    .mMoved = mMoved || drawable.getUpdateCallback() != nullptr });
        }

    private:
        std::vector<ModelShape>* mInto = nullptr;
        osg::Matrix mHere;
        bool mMoved = false;
    };

    MeshReader::MeshReader()
        : mBesideWalk(new BesideWalk)
    {
    }

    MeshReader::~MeshReader() = default;

    const osg::Node* modelOf(const std::span<osg::Node* const> path)
    {
        for (auto at = path.rbegin(); at != path.rend(); ++at)
        {
            const osg::UserDataContainer* data = (*at)->getUserDataContainer();
            if (data != nullptr && data->getUserObject(Misc::OsgUserValues::sFileHash) != nullptr)
                return *at;
        }

        return nullptr;
    }

    const MeshReader::ShapeFaces& MeshReader::facesOf(const osg::Geometry& geometry)
    {
        const auto [known, fresh] = mFaceIndex.try_emplace(&geometry, mFaces.size());
        if (!fresh)
            return mFaces[known->second];

        ShapeFaces& faces = mFaces.emplace_back();
        faces.mGeometry = &geometry;

        const Result<VertexArrays, std::string> vertices = readVertices(geometry, mBesideReadScratch);
        if (!vertices.isOk() || vertices.value().mNormals.empty())
            return faces;

        const VertexArrays& arrays = vertices.value();
        if (!std::ranges::all_of(arrays.mPositions, [](const osg::Vec3f& position) { return isFinite(position); }))
            return faces;

        const Result<bool, std::string> collected
            = collectTriangles(geometry, arrays.mPositions.size(), mBesideCollectScratch);
        if (!collected.isOk() || !collected.value())
            return faces;

        faces.mVertices = Run{ .mOffset = static_cast<std::uint32_t>(mFacePositions.size()),
            .mCount = static_cast<std::uint32_t>(arrays.mPositions.size()) };
        mFacePositions.insert(mFacePositions.end(), arrays.mPositions.begin(), arrays.mPositions.end());
        mFaceNormals.insert(mFaceNormals.end(), arrays.mNormals.begin(), arrays.mNormals.end());
        faces.mTriangles = Run{ .mOffset = static_cast<std::uint32_t>(mFaceTriangles.size()),
            .mCount = static_cast<std::uint32_t>(mBesideCollectScratch.size()) };
        mFaceTriangles.insert(mFaceTriangles.end(), mBesideCollectScratch.begin(), mBesideCollectScratch.end());
        for (const osg::Vec3f& position : arrays.mPositions)
            faces.mBox.expandBy(position);
        faces.mUsable = true;

        return faces;
    }

    void MeshReader::gatherBeside(const osg::Node& model, const osg::Geometry& self, const osg::BoundingBoxf& reach)
    {
        mBesidePositions.clear();
        mBesideNormals.clear();
        mBesideTriangles.clear();
        mModelShapes.clear();
        mBesideWalk->walk(model, mModelShapes);

        if (mFacesOf.get() != &model)
        {
            mFacesOf = &model;
            mFaces.clear();
            mFaceIndex.clear();
            mFacePositions.clear();
            mFaceNormals.clear();
            mFaceTriangles.clear();
        }

        // One set of normals for two places in the model is a split that is right for one of them.
        const auto standing = std::ranges::find(mModelShapes, &self, &ModelShape::mGeometry);
        if (standing == mModelShapes.end() || standing->mMoved
            || std::ranges::count(mModelShapes, &self, &ModelShape::mGeometry) != 1)
            return;

        for (const ModelShape& shape : mModelShapes)
        {
            if (shape.mGeometry == &self || shape.mMoved || shape.mPlace != standing->mPlace)
                continue;

            // The box first, which a shape across the model from this one fails at once.
            const ShapeFaces& faces = facesOf(*shape.mGeometry);
            if (!faces.mUsable || !faces.mBox.intersects(reach))
                continue;

            const std::span<const osg::Vec3f> positions
                = std::span(mFacePositions).subspan(faces.mVertices.mOffset, faces.mVertices.mCount);
            if (!std::ranges::any_of(positions, [&](const osg::Vec3f& position) { return reach.contains(position); }))
                continue;

            const std::span<const osg::Vec3f> normals
                = std::span(mFaceNormals).subspan(faces.mVertices.mOffset, faces.mVertices.mCount);
            const std::span<const std::uint32_t> triangles
                = std::span(mFaceTriangles).subspan(faces.mTriangles.mOffset, faces.mTriangles.mCount);
            const auto offset = static_cast<std::uint32_t>(mBesidePositions.size());
            mBesidePositions.insert(mBesidePositions.end(), positions.begin(), positions.end());
            mBesideNormals.insert(mBesideNormals.end(), normals.begin(), normals.end());
            for (const std::uint32_t vertex : triangles)
                mBesideTriangles.push_back(offset + vertex);
        }
    }

    Result<bool, std::string> MeshReader::collectTriangles(
        const osg::Geometry& geometry, const std::size_t vertices, std::vector<std::uint32_t>& into)
    {
        into.clear();

        osg::TriangleIndexFunctor<TriangleCollector> collector;
        collector.mIndices = &into;
        geometry.accept(collector);

        if (into.empty())
            return false;

        if (collector.mLargest >= vertices)
            return Err{ "its triangles name vertex " + std::to_string(collector.mLargest) + " of "
                + std::to_string(vertices) };

        return true;
    }

    Result<bool, std::string> MeshReader::read(
        ContentPreprocessor& content, const DrawableRead& read, const osg::Node* const model, MeshReading& into)
    {
        const osg::Geometry& geometry = *read.mGeometry;

        const Result<VertexArrays, std::string> vertices = readVertices(geometry, mReadNormalScratch);
        if (!vertices.isOk())
            return Err{ vertices.error() };

        VertexArrays arrays = vertices.value();

        // A morph starts from its base target and not from the source's array, because that is
        // what `MorphGeometry::cull` starts from. The normals and everything else are the source's.
        if (read.mDeform == Deform::Morph)
        {
            const std::span<const osg::Vec3f> base = morphBase(*read.mMorph);
            if (base.size() != arrays.mPositions.size())
                return Err{ "its base target has " + std::to_string(base.size()) + " vertices for "
                    + std::to_string(arrays.mPositions.size()) };

            arrays.mPositions = base;
        }

        if (arrays.mPositions.empty())
            return noTriangle(geometry);

        // **A vertex that is not a finite number refuses the face.** On the device a NaN in a vertex's first component
        // makes its triangle inactive, in any other component it is undefined, and a skin or a morph would pose it into
        // a store that is not finite and a refit that changes what is active.
        if (!std::ranges::all_of(arrays.mPositions, [](const osg::Vec3f& position) { return isFinite(position); }))
            return Err{ std::string("its vertices are not finite numbers") };

        const std::size_t count = arrays.mPositions.size();

        // Every array asked before the fold, so a face this refuses costs no fold.
        const Result<const osg::Vec2Array*, std::string> texCoords = readTexCoords(geometry, 0, count);
        if (!texCoords.isOk())
            return Err{ texCoords.error() };

        // **A second set is the first coordinates bound at any unit that are not unit nought's**, and the units that
        // bind it are noted for the material to look up its dark and glow maps' streams by. Told apart by what the
        // arrays hold and not by which array they are: `NifOsg` gives every unit a fresh array, so the durzog's dark
        // map, bound on the first set at unit one, is a second array holding the first set. No vanilla shape carries a
        // third. Compared by their bits, because a copy is the same bits and a coordinate that is not a number equals
        // nothing.
        const auto holdsSame = [](const osg::Vec2Array* left, const osg::Vec2Array* right) {
            return left == right
                || (left != nullptr && right != nullptr && left->size() == right->size()
                    && std::memcmp(left->getDataPointer(), right->getDataPointer(), left->getTotalDataSize()) == 0);
        };
        const osg::Vec2Array* second = nullptr;
        std::uint32_t unitStreams = 0;
        for (unsigned int unit = 1; unit < geometry.getNumTexCoordArrays() && unit < 32; ++unit)
        {
            if (unit == Shader::sTangentUnit)
                continue;

            const Result<const osg::Vec2Array*, std::string> bound = readTexCoords(geometry, unit, count);
            if (!bound.isOk())
                return Err{ bound.error() };
            if (bound.value() == nullptr || holdsSame(bound.value(), texCoords.value()))
                continue;
            if (second == nullptr)
                second = bound.value();
            if (holdsSame(bound.value(), second))
                unitStreams |= 1u << unit;
        }

        // The source geometry's colours even for a morph, whose positions came from its base
        // target: a morph moves vertices and does not repaint them.
        const Result<std::span<const osg::Vec3f>, std::string> colours = readColours(geometry, count, mColourScratch);
        if (!colours.isOk())
            return Err{ colours.error() };

        const Result<std::span<const osg::Vec4f>, std::string> tangents
            = readTangents(geometry, count, mReadTangentScratch);
        if (!tangents.isOk())
            return Err{ tangents.error() };

        // Shaped before the mesh is written, so the copy the content drew for a card's back never
        // reaches a structure. Once per drawable and never for a pose: a rig moves the two copies
        // together, so the pairs found in the bind pose are the pairs.
        const Result<bool, std::string> collected = collectTriangles(geometry, count, mTriangleScratch);
        if (!collected.isOk())
            return Err{ collected.error() };
        if (!collected.value())
            return noTriangle(geometry);

        const bool splits = read.mDeform == Deform::None;
        CreaseSplit::Beside beside;
        if (model != nullptr && splits && !arrays.mNormals.empty())
        {
            osg::BoundingBoxf reach;
            for (const osg::Vec3f& position : arrays.mPositions)
                reach.expandBy(position);

            gatherBeside(*model, geometry, reach);
            beside = CreaseSplit::Beside{
                .mPositions = mBesidePositions, .mNormals = mBesideNormals, .mTriangles = mBesideTriangles
            };
        }

        const ShapePass::Input shape{
            .mPositions = arrays.mPositions,
            .mNormals = arrays.mNormals,
            .mTriangles = mTriangleScratch,
            .mSplits = splits,
            .mBeside = beside,
        };
        ShapePass::Output shaped{ .mKept = mIndexScratch, .mNormals = mNormalScratch, .mSources = mSourceScratch };
        content.shape(shape, shaped);
        into.mShape = shaped.mShape;

        const std::span<const std::uint32_t> sources = mSourceScratch;
        into.mArrays = MeshArrays{
            .mPositions = withCopies(arrays.mPositions, sources, mPositionScratch),
            .mNormals = mNormalScratch.empty() ? arrays.mNormals : std::span<const osg::Vec3f>(mNormalScratch),
            .mTexCoords = withCopies(texCoords.value() != nullptr
                    ? finiteOrNone(texCoords.value()->asVector(), mReadTexCoordScratch)
                    : std::span<const osg::Vec2f>(),
                sources, mTexCoordScratch),
            .mSecondTexCoords
            = withCopies(second != nullptr ? finiteOrNone(second->asVector(), mReadSecondTexCoordScratch)
                                           : std::span<const osg::Vec2f>(),
                sources, mSecondTexCoordScratch),
            .mUnitStreams = unitStreams,
            .mColours = withCopies(colours.value(), sources, mCopiedColourScratch),
            .mTangents = withCopies(tangents.value(), sources, mTangentScratch),
            .mIndices = mIndexScratch,
        };

        return true;
    }
}
