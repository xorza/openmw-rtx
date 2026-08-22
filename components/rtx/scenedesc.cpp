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

        ++mStructureRevision;
        ++mMeshRevision;

        const auto vertices = static_cast<Index>(positions.size());
        const auto elements = static_cast<Index>(indices.size());

        // **Best fit and not first.** The free list is what one departing ring left, so it is tens of
        // entries and walking it costs nothing; first fit would spend a cathedral's hole on a crate
        // and leave the next cathedral to append. Fitness is measured on the vertices, which is what
        // dominates the room a mesh takes.
        auto best = mFreeMeshes.end();
        for (auto slot = mFreeMeshes.begin(); slot != mFreeMeshes.end(); ++slot)
        {
            const MeshRange& hole = mMeshes[*slot];
            if (hole.mVertexCapacity < vertices || hole.mIndexCapacity < elements)
                continue;

            if (best == mFreeMeshes.end() || hole.mVertexCapacity < mMeshes[*best].mVertexCapacity)
                best = slot;
        }

        if (best != mFreeMeshes.end())
        {
            const Index index = *best;
            mFreeMeshes.erase(best);

            MeshRange& range = mMeshes[index];
            range.mVertexCount = vertices;
            range.mIndexCount = elements;

            writeMesh(range, positions, normals, texCoords, indices);
            return index;
        }

        const MeshRange range{
            .mVertexOffset = static_cast<Index>(mPositions.size()),
            .mVertexCount = vertices,
            .mIndexOffset = static_cast<Index>(mIndices.size()),
            .mIndexCount = elements,
            .mVertexCapacity = vertices,
            .mIndexCapacity = elements,
        };

        // Grown to the range's own size, so the write below lands in room that exists. The attribute
        // buffers stay parallel to the position buffer whether or not the mesh brought the attribute,
        // so a shader can index all of them with one vertex id.
        mPositions.resize(mPositions.size() + vertices);
        mNormals.resize(mPositions.size());
        mTexCoords.resize(mPositions.size());
        mIndices.resize(mIndices.size() + elements);

        writeMesh(range, positions, normals, texCoords, indices);

        mMeshes.push_back(range);
        return static_cast<Index>(mMeshes.size() - 1);
    }

    void SceneDesc::writeMesh(const MeshRange& range, std::span<const osg::Vec3f> positions,
        std::span<const osg::Vec3f> normals, std::span<const osg::Vec2f> texCoords,
        std::span<const std::uint32_t> indices)
    {
        std::copy(positions.begin(), positions.end(), mPositions.begin() + range.mVertexOffset);
        std::copy(indices.begin(), indices.end(), mIndices.begin() + range.mIndexOffset);

        // **Zeroed where the mesh brought none**, rather than left holding whatever the slot's last
        // tenant had. A reused slot is the only way that could happen and it would light a surface
        // by somebody else's normals.
        if (normals.empty())
            std::fill_n(mNormals.begin() + range.mVertexOffset, range.mVertexCount, osg::Vec3f());
        else
            std::copy(normals.begin(), normals.end(), mNormals.begin() + range.mVertexOffset);

        if (texCoords.empty())
            std::fill_n(mTexCoords.begin() + range.mVertexOffset, range.mVertexCount, osg::Vec2f());
        else
            std::copy(texCoords.begin(), texCoords.end(), mTexCoords.begin() + range.mVertexOffset);
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
        ++mShadingRevision;

        // One size, so any freed slot will do and the back of the list is as good as any of them.
        if (!mFreeMaterials.empty())
        {
            const Index index = mFreeMaterials.back();
            mFreeMaterials.pop_back();
            mMaterials[index] = material;
            return index;
        }

        mMaterials.push_back(material);
        return static_cast<Index>(mMaterials.size() - 1);
    }

    Index SceneDesc::addMask(std::span<const float> weights)
    {
        ++mShadingRevision;
        const auto offset = static_cast<Index>(mMasks.size());
        mMasks.insert(mMasks.end(), weights.begin(), weights.end());
        return offset;
    }

    void SceneDesc::addLayer(const MaterialLayer& layer)
    {
        ++mShadingRevision;
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

        ++mStructureRevision;
        const Index index = static_cast<Index>(mTextures.size());
        mTextures.emplace_back(path);
        mTextureIndex.emplace(path, index);
        return index;
    }

    Index SceneDesc::addInstance(const MeshInstance& instance)
    {
        assert(instance.mMesh < mMeshes.size());
        assert(instance.mMaterial == sNoIndex || instance.mMaterial < mMaterials.size());

        Index slot;
        if (mFreeSlots.empty())
        {
            slot = static_cast<Index>(mInstances.size());
            mInstances.emplace_back();
            mPrevious.emplace_back();
        }
        else
        {
            slot = mFreeSlots.back();
            mFreeSlots.pop_back();
        }

        mInstances[slot] = instance;

        // **Standing where it is, not arriving from wherever the last tenant left.** A reused slot
        // would otherwise inherit a previous transform from something else entirely, and a motion
        // vector built from that points across the frame.
        mPrevious[slot] = instance.mTransform;

        mMoved.push_back(slot);
        ++mPlacedCount;
        return slot;
    }

    bool SceneDesc::moveInstance(Index slot, const osg::Matrixf& transform)
    {
        assert(slot < mInstances.size());
        assert(mInstances[slot].isPlaced() && "a slot nothing stands in");

        if (mInstances[slot].mTransform == transform)
            return false;

        mInstances[slot].mTransform = transform;
        mMoved.push_back(slot);
        return true;
    }

    void SceneDesc::dropInstance(Index slot)
    {
        assert(slot < mInstances.size());
        assert(mInstances[slot].isPlaced() && "a slot dropped twice, or one nothing stood in");

        mInstances[slot] = MeshInstance{};
        mFreeSlots.push_back(slot);
        --mPlacedCount;
    }

    void SceneDesc::advancePlacement()
    {
        for (const Index slot : mMoved)
            mPrevious[slot] = mInstances[slot].mTransform;

        mMoved.clear();
    }

    void SceneDesc::clearPlacement()
    {
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
    }

    bool SceneDesc::release(
        std::span<const Index> meshes, std::span<const Index> materials, std::span<const Index> textures)
    {
        // **The ordinary frame, and it costs two comparisons.** Both keep sets come from an identity
        // map keyed one-to-one on what produced the entry, so a set as large as the live table is
        // the whole of it. The textures are not counted here: several emitters share one sprite, so
        // that set has duplicates in it.
        assert(meshes.size() <= mMeshes.size());
        assert(materials.size() <= mMaterials.size());

        const std::size_t liveMeshes = mMeshes.size() - mFreeMeshes.size();
        const std::size_t liveMaterials = mMaterials.size() - mFreeMaterials.size();
        if (meshes.size() == liveMeshes && materials.size() == liveMaterials)
            return false;

        // Allocated here rather than kept, because this runs on the frame a cell left and on no
        // other. What it is not allowed to do is allocate on the frames in between, which is what
        // the test above is for.
        std::vector<char> keptMesh = keepFlags(mMeshes.size(), meshes);
        std::vector<char> keptMaterial = keepFlags(mMaterials.size(), materials);

        // A slot already free is not one to free again.
        for (const Index slot : mFreeMeshes)
            keptMesh[slot] = 1;

        for (const Index slot : mFreeMaterials)
            keptMaterial[slot] = 1;

        std::size_t freedMeshes = 0;
        for (Index index = 0; index < mMeshes.size(); ++index)
        {
            if (keptMesh[index] != 0)
                continue;

            // **The room stays and only the contents go.** Nothing is moved down over it, so every
            // index above this one still means what it meant — which is the whole point, because
            // each of them names a bottom-level acceleration structure that would otherwise have to
            // be built again.
            MeshRange& range = mMeshes[index];
            range.mVertexCount = 0;
            range.mIndexCount = 0;

            mFreeMeshes.push_back(index);
            ++freedMeshes;
        }

        std::size_t freedMaterials = 0;
        for (Index index = 0; index < mMaterials.size(); ++index)
        {
            if (keptMaterial[index] != 0)
                continue;

            // **Its layers and masks are not freed with it**, and that is a known leak: a run is
            // variable length and reclaiming one means the same suballocator the meshes needed. A
            // material that carries layers is a terrain chunk, so what leaks is a blend map per
            // chunk walked past, and travel is what takes it back (`docs/rtx/plan.md` §10).
            mMaterials[index] = Material{};
            mFreeMaterials.push_back(index);
            ++freedMaterials;
        }

        // **The texture table is append-only, and a slot nothing wears is left standing.**
        //
        // A backend holds these in one bindless array a material indexes by position. Freeing a slot
        // is possible now that nothing renumbers — the array would be written at that element rather
        // than appended to — but it needs the arrivals to stop being a contiguous tail, which is
        // `SceneTextures`, `extendScene` and `TextureArray` together. Until then this is the one
        // table that only grows, and the 4,096 the array holds is the session's bound.
        //
        // The caller's set is therefore unused. It stays in the signature because it says what a
        // sprite's texture is — the one thing no material can speak for — and that is what will keep
        // it alive when the slots do come back.
        (void)textures;

        // Every placement the walk did not meet has already been dropped by whoever swept it, and
        // the per-frame lists are refilled by the walk that comes next.
        clearPlacement();

        if (freedMeshes > 0)
        {
            // **Not a structure change.** Nothing arrived and nothing moved: the structures built
            // from these indices are still correct, they simply describe geometry nothing stands on
            // any more, and the top level a frame rebuilds anyway is what stops them being traced.
            // Saying otherwise here is what made a cell boundary cost a full rebuild.
            ++mShadingRevision;
        }

        if (freedMaterials > 0)
            ++mShadingRevision;

        return freedMeshes > 0 || freedMaterials > 0;
    }

    void SceneDesc::clear()
    {
        ++mStructureRevision;
        ++mShadingRevision;
        ++mMeshRevision;
        ++mResetRevision;
        mPositions.clear();
        mNormals.clear();
        mTexCoords.clear();
        mIndices.clear();
        mMeshes.clear();
        mDeformed.clear();
        mInstances.clear();
        mPrevious.clear();
        mMoved.clear();
        mFreeSlots.clear();
        mPlacedCount = 0;
        mMaterials.clear();
        mLayers.clear();
        mMasks.clear();
        mLights.clear();
        mSprites.clear();
        mEmitters.clear();
        mTextures.clear();
        mTextureIndex.clear();
        mFreeMeshes.clear();
        mFreeMaterials.clear();
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
            if (!instance.isPlaced())
                continue;

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
