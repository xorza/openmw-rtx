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
#include <osg/TriangleIndexFunctor>
#include <osg/Vec4f>

#include <components/rtx/common/finite.hpp>
#include <components/rtx/image/colour.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
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
        Misc::Result<bool, std::string> noTriangle(const osg::Geometry& geometry)
        {
            if (drawsLines(geometry))
                return Misc::Err{ std::string("its lines and points have no width a ray can meet") };
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

        /// `given` where every element is a finite number, or a copy of it in `scratch` with every
        /// element that is not read as none: the zero the mesh table holds for a normal, a tangent
        /// or a coordinate the mesh did not bring. **Content wrote them, so a number that is not
        /// finite is data.** A skin posed a NaN normal into a store that was not finite, where one of
        /// none is one the rest already reads: the crease split leaves it out of every group, and a
        /// hit whose normal comes to no length takes the triangle's plane. A coordinate that is not
        /// finite samples whichever texel the driver picks, and nought is the one every driver
        /// picks. Copied only where one is, so a mesh written well is read in place.
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
        Misc::Result<void, std::string> checkLength(std::string_view what, std::size_t named, std::size_t vertices)
        {
            if (named != vertices)
                return Misc::Err{ "it has " + std::to_string(named) + ' ' + std::string(what) + " for "
                    + std::to_string(vertices) + " vertices" };

            return {};
        }

        /// @param scratch for an overall normal spread across the vertices, or normals that are not
        ///        all finite. Refilled here and borrowed by the returned span, so it has to outlive
        ///        the read.
        Misc::Result<VertexArrays, std::string> readVertices(
            const osg::Geometry& geometry, std::vector<osg::Vec3f>& scratch)
        {
            VertexArrays arrays;

            // No array is no geometry, which the game draws nothing for either; an array of a type
            // this does not read is one it does.
            const osg::Array* vertices = geometry.getVertexArray();
            if (vertices == nullptr || vertices->getNumElements() == 0)
                return arrays;

            const osg::Vec3Array* positions = asVec3Array(vertices);
            if (positions == nullptr)
                return Misc::Err{ "its vertices are not three floats each" };

            arrays.mPositions = std::span(positions->asVector());

            const osg::Array* named = geometry.getNormalArray();
            if (named == nullptr || named->getNumElements() == 0)
                return arrays;

            const osg::Vec3Array* normals = asVec3Array(named);
            if (normals == nullptr)
                return Misc::Err{ "its normals are not three floats each" };

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

            if (const Misc::Result<void, std::string> matched
                = checkLength("normals", normals->size(), positions->size());
                !matched.isOk())
                return Misc::Err{ matched.error() };

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
        Misc::Result<const osg::Vec2Array*, std::string> readTexCoords(
            const osg::Geometry& geometry, unsigned int unit, std::size_t vertices)
        {
            const osg::Array* named = geometry.getTexCoordArray(unit);
            if (named == nullptr || named->getNumElements() == 0)
                return nullptr;

            const osg::Vec2Array* coords = asVec2Array(named);
            if (coords == nullptr)
                return Misc::Err{ "its texture coordinates are not two floats each" };

            if (const Misc::Result<void, std::string> matched
                = checkLength("texture coordinates", coords->size(), vertices);
                !matched.isOk())
                return Misc::Err{ matched.error() };

            return coords;
        }

        /// The tangents `Shader::MapVisitor` built at `Shader::sTangentUnit`, or none where it built
        /// none, and a tangent that is not finite read as none: the generator builds one from the
        /// content's normals. An error where they do not match the vertices — `checkLength`.
        ///
        /// @param scratch for tangents that are not all finite, borrowed as `readVertices` borrows
        ///        its own.
        Misc::Result<std::span<const osg::Vec4f>, std::string> readTangents(
            const osg::Geometry& geometry, std::size_t vertices, std::vector<osg::Vec4f>& scratch)
        {
            const osg::Array* named = geometry.getTexCoordArray(Shader::sTangentUnit);
            if (named == nullptr || named->getNumElements() == 0)
                return std::span<const osg::Vec4f>();

            if (named->getType() != osg::Array::Vec4ArrayType)
                return Misc::Err{ "its tangents are not four floats each" };

            const auto* tangents = static_cast<const osg::Vec4Array*>(named);
            if (const Misc::Result<void, std::string> matched = checkLength("tangents", tangents->size(), vertices);
                !matched.isOk())
                return Misc::Err{ matched.error() };

            return finiteOrNone(tangents->asVector(), scratch);
        }

        /// A geometry's per-vertex colours, decoded into `scratch` and spanned from it. Empty where
        /// the geometry names none. Two array types, because `NifOsg` builds a `Vec4Array` from a
        /// `NiGeometryData` and a `Vec4ubArray` from a `BSTriShape`. An overall colour is spread
        /// across the vertices, as `readVertices` spreads an overall normal. A colour that is not a
        /// finite number is read as white, the table's "no tint", because it is multiplied into the
        /// light. The alpha is not read: three shapes in the whole of vanilla carry one below
        /// opaque, and reading it would put a fetch on every candidate of every shadow ray.
        ///
        /// @param vertices how many the geometry holds, which an array that is not overall has to
        ///        match — `checkLength`.
        Misc::Result<std::span<const osg::Vec3f>, std::string> readColours(
            const osg::Geometry& geometry, const std::size_t vertices, std::vector<osg::Vec3f>& scratch)
        {
            const osg::Array* colours = geometry.getColorArray();
            if (colours == nullptr || colours->getNumElements() == 0)
                return std::span<const osg::Vec3f>();

            const bool overall = colours->getBinding() == osg::Array::BIND_OVERALL;
            if (!overall)
            {
                const Misc::Result<void, std::string> matched
                    = checkLength("colours", colours->getNumElements(), vertices);
                if (!matched.isOk())
                    return Misc::Err{ matched.error() };
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
                    return Misc::Err{ "its colours are neither four floats nor four bytes each" };
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
            const bool skinned = rig->getInfluenceData() != nullptr && !rig->getBones().empty();
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

    Misc::Result<bool, std::string> MeshReader::collectTriangles(
        const osg::Geometry& geometry, const std::size_t vertices)
    {
        mTriangleScratch.clear();

        osg::TriangleIndexFunctor<TriangleCollector> collector;
        collector.mIndices = &mTriangleScratch;
        geometry.accept(collector);

        if (mTriangleScratch.empty())
            return false;

        if (collector.mLargest >= vertices)
            return Misc::Err{ "its triangles name vertex " + std::to_string(collector.mLargest) + " of "
                + std::to_string(vertices) };

        return true;
    }

    Misc::Result<bool, std::string> MeshReader::read(
        ContentPreprocessor& content, const DrawableRead& read, MeshReading& into)
    {
        const osg::Geometry& geometry = *read.mGeometry;

        const Misc::Result<VertexArrays, std::string> vertices = readVertices(geometry, mReadNormalScratch);
        if (!vertices.isOk())
            return Misc::Err{ vertices.error() };

        VertexArrays arrays = vertices.value();

        // A morph starts from its base target and not from the source's array, because that is
        // what `MorphGeometry::cull` starts from. The normals and everything else are the source's.
        if (read.mDeform == Deform::Morph)
        {
            const std::span<const osg::Vec3f> base = morphBase(*read.mMorph);
            if (base.size() != arrays.mPositions.size())
                return Misc::Err{ "its base target has " + std::to_string(base.size()) + " vertices for "
                    + std::to_string(arrays.mPositions.size()) };

            arrays.mPositions = base;
        }

        if (arrays.mPositions.empty())
            return noTriangle(geometry);

        // **A vertex that is not a finite number refuses the face**, as a place that is not one
        // stands nothing: nothing stands in for where a vertex is. On the device a NaN in a
        // vertex's first component makes its triangle inactive, in any other component it is
        // undefined, and a skin or a morph would pose it into a store that is not finite and a
        // refit that changes what is active.
        if (!std::ranges::all_of(arrays.mPositions, [](const osg::Vec3f& position) { return isFinite(position); }))
            return Misc::Err{ std::string("its vertices are not finite numbers") };

        const std::size_t count = arrays.mPositions.size();

        // Every array asked before the fold, so a face this refuses costs no fold.
        const Misc::Result<const osg::Vec2Array*, std::string> texCoords = readTexCoords(geometry, 0, count);
        if (!texCoords.isOk())
            return Misc::Err{ texCoords.error() };

        // **A second set is the first coordinates bound at any unit that are not unit nought's**,
        // and the units that bind it are noted for the material to look up its dark and glow maps'
        // streams by. Told apart by what the arrays hold and not by which array they are:
        // `NifOsg` gives every unit a fresh array, so the durzog's dark map, bound on the first
        // set at unit one, is a second array holding the first set. No vanilla shape carries a
        // third, and the unit the tangents are at carries no coordinates. Compared by their bits,
        // because a copy is the same bits and a coordinate that is not a number equals nothing.
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

            const Misc::Result<const osg::Vec2Array*, std::string> bound = readTexCoords(geometry, unit, count);
            if (!bound.isOk())
                return Misc::Err{ bound.error() };
            if (bound.value() == nullptr || holdsSame(bound.value(), texCoords.value()))
                continue;
            if (second == nullptr)
                second = bound.value();
            if (holdsSame(bound.value(), second))
                unitStreams |= 1u << unit;
        }

        // The source geometry's colours even for a morph, whose positions came from its base
        // target: a morph moves vertices and does not repaint them.
        const Misc::Result<std::span<const osg::Vec3f>, std::string> colours
            = readColours(geometry, count, mColourScratch);
        if (!colours.isOk())
            return Misc::Err{ colours.error() };

        const Misc::Result<std::span<const osg::Vec4f>, std::string> tangents
            = readTangents(geometry, count, mReadTangentScratch);
        if (!tangents.isOk())
            return Misc::Err{ tangents.error() };

        // Shaped before the mesh is written, so the copy the content drew for a card's back never
        // reaches a structure. Once per drawable and never for a pose: a rig moves the two copies
        // together, so the pairs found in the bind pose are the pairs.
        const Misc::Result<bool, std::string> collected = collectTriangles(geometry, count);
        if (!collected.isOk())
            return Misc::Err{ collected.error() };
        if (!collected.value())
            return noTriangle(geometry);

        const ShapePass::Input shape{
            .mPositions = arrays.mPositions,
            .mNormals = arrays.mNormals,
            .mTriangles = mTriangleScratch,
            .mSplits = read.mDeform == Deform::None,
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
