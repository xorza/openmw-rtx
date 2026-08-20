#include "scenedesc.hpp"

#include <algorithm>
#include <cassert>

namespace Rtx
{
    Index SceneDesc::addMesh(std::span<const osg::Vec3f> positions, std::span<const osg::Vec3f> normals,
        std::span<const osg::Vec2f> texCoords, std::span<const std::uint32_t> indices)
    {
        assert(!positions.empty());
        assert(normals.empty() || normals.size() == positions.size());
        assert(texCoords.empty() || texCoords.size() == positions.size());
        assert(indices.size() % 3 == 0);
        assert(std::all_of(indices.begin(), indices.end(), [&](std::uint32_t i) { return i < positions.size(); }));

        const MeshRange range{
            .mVertexOffset = static_cast<Index>(mPositions.size()),
            .mVertexCount = static_cast<Index>(positions.size()),
            .mIndexOffset = static_cast<Index>(mIndices.size()),
            .mIndexCount = static_cast<Index>(indices.size()),
        };

        mPositions.insert(mPositions.end(), positions.begin(), positions.end());
        mIndices.insert(mIndices.end(), indices.begin(), indices.end());

        // The attribute buffers stay parallel to the position buffer whether or not the mesh brought
        // the attribute, so a shader can index all of them with one vertex id.
        if (normals.empty())
            mNormals.resize(mPositions.size());
        else
            mNormals.insert(mNormals.end(), normals.begin(), normals.end());

        if (texCoords.empty())
            mTexCoords.resize(mPositions.size());
        else
            mTexCoords.insert(mTexCoords.end(), texCoords.begin(), texCoords.end());

        mMeshes.push_back(range);
        return static_cast<Index>(mMeshes.size() - 1);
    }

    Index SceneDesc::addMaterial(const Material& material)
    {
        mMaterials.push_back(material);
        return static_cast<Index>(mMaterials.size() - 1);
    }

    Index SceneDesc::addTexture(VFS::Path::NormalizedView path)
    {
        const auto known = mTextureIndex.find(path);
        if (known != mTextureIndex.end())
            return known->second;

        const Index index = static_cast<Index>(mTextures.size());
        mTextures.emplace_back(path);
        mTextureIndex.emplace(path, index);
        return index;
    }

    void SceneDesc::addInstance(const MeshInstance& instance)
    {
        assert(instance.mMesh < mMeshes.size());
        assert(instance.mMaterial == sNoIndex || instance.mMaterial < mMaterials.size());
        mInstances.push_back(instance);
    }

    void SceneDesc::clear()
    {
        mPositions.clear();
        mNormals.clear();
        mTexCoords.clear();
        mIndices.clear();
        mMeshes.clear();
        mInstances.clear();
        mMaterials.clear();
        mTextures.clear();
        mTextureIndex.clear();
    }

    std::span<const osg::Vec3f> SceneDesc::getMeshPositions(Index mesh) const
    {
        assert(mesh < mMeshes.size());
        const MeshRange& range = mMeshes[mesh];
        return std::span(mPositions).subspan(range.mVertexOffset, range.mVertexCount);
    }

    std::span<const std::uint32_t> SceneDesc::getMeshIndices(Index mesh) const
    {
        assert(mesh < mMeshes.size());
        const MeshRange& range = mMeshes[mesh];
        return std::span(mIndices).subspan(range.mIndexOffset, range.mIndexCount);
    }

    std::uint32_t SceneDesc::getTriangleCount() const
    {
        return static_cast<std::uint32_t>(mIndices.size() / 3);
    }

    std::size_t SceneDesc::getGeometryBytes() const
    {
        return mPositions.size() * sizeof(osg::Vec3f) + mNormals.size() * sizeof(osg::Vec3f)
            + mTexCoords.size() * sizeof(osg::Vec2f) + mIndices.size() * sizeof(std::uint32_t);
    }
}
