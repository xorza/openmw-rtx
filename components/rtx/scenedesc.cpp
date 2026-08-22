#include "scenedesc.hpp"

#include <algorithm>
#include <cassert>

namespace Rtx
{
    namespace
    {
        /// What a blended material is tested against when it named no threshold of its own.
        ///
        /// Half, because the alpha it is standing in for is very nearly binary already: Morrowind's
        /// masks are painted, not anti-aliased, and the fringe a filter puts on them is a texel wide.
        constexpr float sBlendCutoff = 0.5f;
    }

    float Material::getAlphaCutoff() const
    {
        switch (mAlphaMode)
        {
            case AlphaMode::Opaque:
                return 0.0f;
            case AlphaMode::Cutout:
                return mAlphaRef;
            case AlphaMode::Blend:
                return mAlphaRef > 0.0f ? mAlphaRef : sBlendCutoff;
        }

        return 0.0f;
    }

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

        ++mRevision;
        mMeshes.push_back(range);
        return static_cast<Index>(mMeshes.size() - 1);
    }

    void SceneDesc::updateMesh(Index mesh, std::span<const osg::Vec3f> positions, std::span<const osg::Vec3f> normals)
    {
        assert(mesh < mMeshes.size());

        const MeshRange& range = mMeshes[mesh];
        assert(positions.size() == range.mVertexCount);
        assert(normals.empty() || normals.size() == range.mVertexCount);

        std::copy(positions.begin(), positions.end(), mPositions.begin() + range.mVertexOffset);
        if (!normals.empty())
            std::copy(normals.begin(), normals.end(), mNormals.begin() + range.mVertexOffset);

        // Named once however many callers reach it, because a backend builds one structure per mesh
        // and building it twice in a frame is the same answer for twice the cost. Linear over a list
        // that is the frame's moving meshes — a crowd, not a cell.
        if (std::find(mDeformed.begin(), mDeformed.end(), mesh) == mDeformed.end())
            mDeformed.push_back(mesh);
    }

    Index SceneDesc::addMaterial(const Material& material)
    {
        ++mRevision;
        mMaterials.push_back(material);
        return static_cast<Index>(mMaterials.size() - 1);
    }

    Index SceneDesc::addMask(std::span<const float> weights)
    {
        ++mRevision;
        const auto offset = static_cast<Index>(mMasks.size());
        mMasks.insert(mMasks.end(), weights.begin(), weights.end());
        return offset;
    }

    void SceneDesc::addLayer(const MaterialLayer& layer)
    {
        ++mRevision;
        mLayers.push_back(layer);
    }

    void SceneDesc::addLight(const Light& light)
    {
        mLights.push_back(light);
    }

    void SceneDesc::addEmitter(std::span<const Sprite> sprites, Index texture, bool additive)
    {
        if (sprites.empty())
            return;

        // The centre of the sprites' own bounding box rather than their mean, and the reach measured
        // off it: a plume is a handful of parcels strung along one axis, and a mean sits where most
        // of them happen to be at this instant rather than where the extent is.
        osg::BoundingBoxf box;
        for (const Sprite& sprite : sprites)
        {
            const osg::Vec3f rim(sprite.mRadius, sprite.mRadius, sprite.mRadius);
            box.expandBy(sprite.mPosition - rim);
            box.expandBy(sprite.mPosition + rim);
        }

        const osg::Vec3f centre = box.center();
        float reach = 0.0f;
        for (const Sprite& sprite : sprites)
            reach = std::max(reach, (sprite.mPosition - centre).length() + sprite.mRadius);

        mEmitters.push_back(SpriteEmitter{
            .mCentre = centre,
            .mReach = reach,
            .mFirst = static_cast<Index>(mSprites.size()),
            .mCount = static_cast<Index>(sprites.size()),
            .mTexture = texture,
            .mAdditive = additive,
        });

        mSprites.insert(mSprites.end(), sprites.begin(), sprites.end());
    }

    Index SceneDesc::addTexture(VFS::Path::NormalizedView path)
    {
        const auto known = mTextureIndex.find(path);
        if (known != mTextureIndex.end())
            return known->second;

        ++mRevision;
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

    void SceneDesc::clearPlacement()
    {
        mInstances.clear();
        mLights.clear();
        mDeformed.clear();
        mSprites.clear();
        mEmitters.clear();
    }

    namespace
    {
        /// A byte per entry, set for everything `keep` names. Duplicates and any order are fine.
        std::vector<char> keepFlags(std::size_t count, std::span<const Index> keep)
        {
            std::vector<char> flags(count, 0);
            for (const Index index : keep)
            {
                assert(index < count);
                flags[index] = 1;
            }

            return flags;
        }

        /// Where `old` went, or itself where nothing moved.
        Index through(const std::vector<Index>& remap, Index old)
        {
            return old == sNoIndex ? sNoIndex : remap[old];
        }
    }

    bool SceneDesc::retain(
        std::span<const Index> meshes, std::span<const Index> materials, std::span<const Index> textures, Remap& remap)
    {
        // **The ordinary frame, and it costs two comparisons.** Both keep sets come from an identity
        // map keyed one-to-one on what produced the entry, so a set as large as the table is the
        // whole table. The textures are not counted here: several emitters share one sprite, so that
        // set has duplicates in it — and a texture can only be orphaned by a material or a mesh
        // going, which is the case below.
        assert(meshes.size() <= mMeshes.size());
        assert(materials.size() <= mMaterials.size());
        if (meshes.size() == mMeshes.size() && materials.size() == mMaterials.size())
            return false;

        // Allocated here rather than kept, because this runs on the frame a cell left and on no
        // other. What it is not allowed to do is allocate on the frames in between, which is what
        // the test above is for.
        const std::vector<char> keptMesh = keepFlags(mMeshes.size(), meshes);
        const std::vector<char> keptMaterial = keepFlags(mMaterials.size(), materials);

        // **Compacted in place, left to right.** The survivors keep their relative order, so what is
        // being written to is always at or behind what is being read from — which is what makes a
        // second copy of a worldspace's geometry unnecessary.
        remap.mMeshes.assign(mMeshes.size(), sNoIndex);
        Index meshWrite = 0;
        Index vertexWrite = 0;
        Index indexWrite = 0;
        for (std::size_t old = 0; old < mMeshes.size(); ++old)
        {
            if (keptMesh[old] == 0)
                continue;

            MeshRange range = mMeshes[old];
            std::copy_n(mPositions.begin() + range.mVertexOffset, range.mVertexCount, mPositions.begin() + vertexWrite);
            std::copy_n(mNormals.begin() + range.mVertexOffset, range.mVertexCount, mNormals.begin() + vertexWrite);
            std::copy_n(mTexCoords.begin() + range.mVertexOffset, range.mVertexCount, mTexCoords.begin() + vertexWrite);
            std::copy_n(mIndices.begin() + range.mIndexOffset, range.mIndexCount, mIndices.begin() + indexWrite);

            range.mVertexOffset = vertexWrite;
            range.mIndexOffset = indexWrite;
            vertexWrite += range.mVertexCount;
            indexWrite += range.mIndexCount;

            remap.mMeshes[old] = meshWrite;
            mMeshes[meshWrite++] = range;
        }

        mMeshes.resize(meshWrite);
        mPositions.resize(vertexWrite);
        mNormals.resize(vertexWrite);
        mTexCoords.resize(vertexWrite);
        mIndices.resize(indexWrite);

        // **The layers and the masks before the materials that own them**, and while those materials
        // are still where they were: a layer run is found through the material's own offset, and
        // rewriting the material first would lose it. Layers were appended in material order and
        // masks in layer order, so the same left-to-right argument holds for both.
        Index layerWrite = 0;
        Index maskWrite = 0;
        for (std::size_t old = 0; old < mMaterials.size(); ++old)
        {
            if (keptMaterial[old] == 0)
                continue;

            Material& material = mMaterials[old];
            const Index first = material.mLayerOffset;
            material.mLayerOffset = layerWrite;

            for (Index k = 0; k < material.mLayerCount; ++k)
            {
                MaterialLayer layer = mLayers[first + k];

                // A chunk of a single ground type carries no mask, and there is nothing to move.
                const Index weights = Index{ layer.mMaskWidth } * layer.mMaskHeight;
                if (weights > 0)
                {
                    std::copy_n(mMasks.begin() + layer.mMaskOffset, weights, mMasks.begin() + maskWrite);
                    layer.mMaskOffset = maskWrite;
                    maskWrite += weights;
                }

                mLayers[layerWrite++] = layer;
            }
        }

        mLayers.resize(layerWrite);
        mMasks.resize(maskWrite);

        // A texture lives while anything still names it. The caller's set is what speaks for the
        // ones nothing else can: a particle emitter's sprite hangs off no material at all.
        std::vector<char> keptTexture = keepFlags(mTextures.size(), textures);
        const auto name = [&](Index texture) {
            if (texture != sNoIndex)
                keptTexture[texture] = 1;
        };

        for (std::size_t old = 0; old < mMaterials.size(); ++old)
        {
            if (keptMaterial[old] == 0)
                continue;

            name(mMaterials[old].mDiffuse);
            name(mMaterials[old].mNormal);
            name(mMaterials[old].mEmissive);
        }

        for (const MaterialLayer& layer : mLayers)
            name(layer.mDiffuse);

        remap.mTextures.assign(mTextures.size(), sNoIndex);
        Index textureWrite = 0;
        for (std::size_t old = 0; old < mTextures.size(); ++old)
        {
            if (keptTexture[old] == 0)
                continue;

            remap.mTextures[old] = textureWrite;

            // **Only where it is actually moving.** Everything before the first casualty is written
            // to the slot it is already in, and a path long enough to be on the heap does not
            // survive being move-assigned to itself: the assignment takes the source's buffer and
            // then sets the source's length to zero, and the source is the same object. An emptied
            // path is a texture nothing can load, which reaches the screen as a world with no
            // textures on it at all.
            if (textureWrite != old)
                mTextures[textureWrite] = std::move(mTextures[old]);

            ++textureWrite;
        }

        mTextures.resize(textureWrite);

        // Rebuilt rather than walked and patched: the map is keyed on the path, and every surviving
        // path is still one of these.
        mTextureIndex.clear();
        for (Index at = 0; at < textureWrite; ++at)
            mTextureIndex.emplace(mTextures[at], at);

        for (MaterialLayer& layer : mLayers)
            layer.mDiffuse = through(remap.mTextures, layer.mDiffuse);

        remap.mMaterials.assign(mMaterials.size(), sNoIndex);
        Index materialWrite = 0;
        for (std::size_t old = 0; old < mMaterials.size(); ++old)
        {
            if (keptMaterial[old] == 0)
                continue;

            Material material = mMaterials[old];
            material.mDiffuse = through(remap.mTextures, material.mDiffuse);
            material.mNormal = through(remap.mTextures, material.mNormal);
            material.mEmissive = through(remap.mTextures, material.mEmissive);

            remap.mMaterials[old] = materialWrite;
            mMaterials[materialWrite++] = material;
        }

        mMaterials.resize(materialWrite);

        // Every one of these names an index that has just moved, and the walk that comes next is
        // what puts them back.
        clearPlacement();

        ++mRevision;
        return true;
    }

    void SceneDesc::clear()
    {
        ++mRevision;
        mPositions.clear();
        mNormals.clear();
        mTexCoords.clear();
        mIndices.clear();
        mMeshes.clear();
        mDeformed.clear();
        mInstances.clear();
        mMaterials.clear();
        mLayers.clear();
        mMasks.clear();
        mLights.clear();
        mSprites.clear();
        mEmitters.clear();
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

    osg::BoundingBoxf SceneDesc::getBounds() const
    {
        std::vector<osg::BoundingBoxf> local(mMeshes.size());
        for (std::size_t i = 0; i < mMeshes.size(); ++i)
            for (const osg::Vec3f& position : getMeshPositions(static_cast<Index>(i)))
                local[i].expandBy(position);

        osg::BoundingBoxf bounds;
        for (const MeshInstance& instance : mInstances)
        {
            const osg::BoundingBoxf& box = local[instance.mMesh];
            if (!box.valid())
                continue;

            for (unsigned int corner = 0; corner < 8; ++corner)
                bounds.expandBy(box.corner(corner) * instance.mTransform);
        }

        return bounds;
    }

    std::size_t SceneDesc::getGeometryBytes() const
    {
        return mPositions.size() * sizeof(osg::Vec3f) + mNormals.size() * sizeof(osg::Vec3f)
            + mTexCoords.size() * sizeof(osg::Vec2f) + mIndices.size() * sizeof(std::uint32_t);
    }
}
