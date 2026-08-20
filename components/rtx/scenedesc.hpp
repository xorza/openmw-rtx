#ifndef OPENMW_COMPONENTS_RTX_SCENEDESC_H
#define OPENMW_COMPONENTS_RTX_SCENEDESC_H

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    /// An index into one of `SceneDesc`'s tables, or `sNoIndex` for "none".
    using Index = std::uint32_t;

    inline constexpr Index sNoIndex = ~Index{ 0 };

    /// Where one mesh's vertices and indices sit in the scene's shared buffers.
    ///
    /// The buffers are shared rather than per-mesh because a cell holds thousands of meshes and a
    /// `vector` of `vector`s would pay an allocation for each one — and because the GPU wants one
    /// buffer anyway, so a per-mesh vector would only have to be flattened again on the way up.
    struct MeshRange
    {
        Index mVertexOffset = 0;
        Index mVertexCount = 0;
        Index mIndexOffset = 0;
        Index mIndexCount = 0;

        Index getTriangleCount() const { return mIndexCount / 3; }
    };

    /// How the alpha channel of a surface's diffuse texture is meant to be read.
    enum class AlphaMode
    {
        /// Ignore it. The overwhelming majority of Morrowind's geometry.
        Opaque,

        /// Test against `Material::mAlphaRef`. Foliage, grates, chains — the case opacity micromaps
        /// exist for, and the case that costs an any-hit shader until they are built.
        Cutout,

        /// Blend. Glass, water planes placed as objects, magic effects.
        Blend,
    };

    /// How a surface is shaded, as recovered from the model.
    ///
    /// Vanilla textures are pre-lit, so `mDiffuse` is not an albedo yet; recovering one is M9. What
    /// is here is what the file says.
    struct Material
    {
        Index mDiffuse = sNoIndex;
        Index mNormal = sNoIndex;
        Index mEmissive = sNoIndex;

        osg::Vec4f mDiffuseColour{ 1.0f, 1.0f, 1.0f, 1.0f };
        osg::Vec3f mEmissiveColour{ 0.0f, 0.0f, 0.0f };

        float mAlphaRef = 0.0f;
        AlphaMode mAlphaMode = AlphaMode::Opaque;

        /// Sheet geometry lit and hit from both faces. Morrowind leans on this heavily and a ray
        /// tracer has to be told, because back-face culling is not free the way a rasterizer's is.
        bool mTwoSided = false;
    };

    /// One mesh placed in the world: a row of the top-level acceleration structure.
    ///
    /// Not `Instance`, which in this namespace is the `VkInstance` a device comes from.
    struct MeshInstance
    {
        /// Object space to world space.
        osg::Matrixf mTransform;

        Index mMesh = sNoIndex;
        Index mMaterial = sNoIndex;
    };

    /// Everything the renderer needs to know about a world, with no Vulkan and no scene graph in it.
    ///
    /// No lights yet. `NifOsg` never reads `NiLight`, so a model carries none; the world's lights come
    /// from ESM `Light` records, which the content loader does not read either. Both are M4's.
    ///
    /// Deliberately dumb: it appends and it dedups paths, and nothing else. Deciding that two
    /// drawables are the same mesh belongs to whoever is reading the scene graph, which knows what
    /// identity means there; this type would have to guess.
    class SceneDesc
    {
    public:
        /// Copies the vertex data into the shared buffers and returns the new mesh's index.
        ///
        /// `normals` and `texCoords` may be empty; when they are not they must match `positions` in
        /// length, and `indices` must be a whole number of triangles addressing only those vertices.
        /// All three are contracts on the caller, so they are asserted rather than reported.
        Index addMesh(std::span<const osg::Vec3f> positions, std::span<const osg::Vec3f> normals,
            std::span<const osg::Vec2f> texCoords, std::span<const std::uint32_t> indices);

        Index addMaterial(const Material& material);

        /// Returns the index of `path`, adding it only if it is not already known.
        Index addTexture(VFS::Path::NormalizedView path);

        void addInstance(const MeshInstance& instance);

        /// Empties every table while keeping the capacity, so rebuilding a scene does not go back to
        /// the allocator for buffers it already had.
        void clear();

        std::span<const osg::Vec3f> getPositions() const { return mPositions; }
        std::span<const osg::Vec3f> getNormals() const { return mNormals; }
        std::span<const osg::Vec2f> getTexCoords() const { return mTexCoords; }
        std::span<const std::uint32_t> getIndices() const { return mIndices; }
        std::span<const MeshRange> getMeshes() const { return mMeshes; }
        std::span<const MeshInstance> getInstances() const { return mInstances; }
        std::span<const Material> getMaterials() const { return mMaterials; }
        std::span<const VFS::Path::Normalized> getTextures() const { return mTextures; }

        /// The vertices of one mesh, for a test or a build that wants to read back what it appended.
        std::span<const osg::Vec3f> getMeshPositions(Index mesh) const;
        std::span<const std::uint32_t> getMeshIndices(Index mesh) const;

        std::uint32_t getTriangleCount() const;

        /// The world-space extent of everything placed. Invalid when nothing is.
        ///
        /// Computed from each mesh's local box carried through its instances rather than from every
        /// vertex of every instance, which is the difference between eight transforms per instance
        /// and several hundred.
        osg::BoundingBoxf getBounds() const;

        /// Bytes held by the vertex and index buffers. What the upload at M3 will cost.
        std::size_t getGeometryBytes() const;

    private:
        std::vector<osg::Vec3f> mPositions;
        std::vector<osg::Vec3f> mNormals;
        std::vector<osg::Vec2f> mTexCoords;
        std::vector<std::uint32_t> mIndices;
        std::vector<MeshRange> mMeshes;
        std::vector<MeshInstance> mInstances;
        std::vector<Material> mMaterials;
        std::vector<VFS::Path::Normalized> mTextures;

        // The scan this replaces was O(materials x textures). A cell is a hundred of each and would
        // never have noticed; a worldspace is thousands of both, and load time is not the place to
        // find that out. `VFS::Path::Hash` is transparent, so a lookup by view costs no string.
        std::unordered_map<VFS::Path::Normalized, Index, VFS::Path::Hash, std::equal_to<>> mTextureIndex;
    };
}

#endif
