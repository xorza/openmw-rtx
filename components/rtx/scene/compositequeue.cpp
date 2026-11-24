#include "compositequeue.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <components/rtx/shaders/scene.h>

#include "material.hpp"

namespace Rtx
{
    namespace
    {
        /// The one take `mRefused` records.
        constexpr std::uint16_t sCompositeTake = 1;

        /// Whether any layer of `layers` reflects, which is whether a gloss says anything.
        bool reflects(const SceneDesc& scene, const Run& layers)
        {
            return std::ranges::any_of(layers.in(scene.materials().getLayers()),
                [](const MaterialLayer& layer) { return (layer.mFlags & Shaders::LAYER_AUTHORED) != 0u; });
        }

        /// Whether every layer of `layers` reads a texture that stands on the device (`LayerStanding`), or the neutral
        /// texel a full table gave it: what the bake reads of each is its diffuse, and one that still stands in, its
        /// file being read, would be flattened into the chunk as the stand-in's grey for good.
        bool layersStand(const SceneDesc& scene, const Run& layers, const LayerStanding& standing)
        {
            return std::ranges::all_of(layers.in(scene.materials().getLayers()), [&](const MaterialLayer& layer) {
                return layer.mDiffuse == Shaders::TEXTURE_NEUTRAL || standing.stands(TextureIndex(layer.mDiffuse));
            });
        }

        /// Whether `material` still wants the ground it asked for as `asked`: the same kind, still
        /// asking, not yet given, and its layers where they were. The run and not the layers in it:
        /// the bake reads the row as it stands when the slot is given, so a run handed out again to
        /// another chunk flattens that chunk's ground.
        bool stillWants(const SceneDesc& scene, const MaterialIndex material, const Run& layers)
        {
            const RowSpan<const Material, MaterialIndex> materials = scene.materials().getRows();
            if (!materials.holds(material))
                return false;

            return materials[material].wantsFlattening() && materials[material].mLayers == layers;
        }
    }

    std::size_t CompositeQueue::advance(SceneDesc& scene, const LayerStanding& standing)
    {
        gather(scene);
        return take(scene, standing, sCompositesPerFrame);
    }

    void CompositeQueue::gather(const SceneDesc& scene)
    {
        const RowSpan<const Material, MaterialIndex> materials = scene.materials().getRows();
        if (mPositions.size() < materials.size())
            mPositions.resize(materials.size(), sNoPosition);

        for (const MaterialIndex at : scene.materials().getWritten())
        {
            const Material& material = materials[at];
            if (!material.wantsFlattening())
                continue;

            const Asked wanted{ .mMaterial = at, .mLayers = material.mLayers };
            if (const std::uint32_t position = mPositions[at]; position != sNoPosition)
            {
                Asked& waiting = mWaiting[position];
                if (waiting == wanted)
                    continue;

                // A slot taken over by another chunk while its predecessor waited: what was asked is
                // ground that has gone, and the new chunk goes to the back of the schedule. The old
                // ask stays where it stands, naming no material, for `take` to pass over — closing
                // the gap would move every ask behind it.
                waiting.mMaterial = sNoRow;
            }

            wait(wanted);
        }
    }

    void CompositeQueue::wait(const Asked& asked)
    {
        // By doubling, and unrolled so the oldest is at the front again: a region's worth of chunks
        // asks in one walk, and the ring settles at the most that ever waited at once.
        if (mCount == mWaiting.size())
        {
            std::vector<Asked> grown(std::max<std::size_t>(mWaiting.size() * 2, 8));
            for (std::size_t age = 0; age < mCount; ++age)
            {
                grown[age] = waitingAt(age);
                if (grown[age].mMaterial != sNoRow)
                    mPositions[grown[age].mMaterial] = static_cast<std::uint32_t>(age);
            }

            mWaiting.swap(grown);
            mFront = 0;
        }

        const std::size_t position = (mFront + mCount) % mWaiting.size();
        mWaiting[position] = asked;
        mPositions[asked.mMaterial] = static_cast<std::uint32_t>(position);
        ++mCount;
    }

    void CompositeQueue::putBack(const Asked& asked)
    {
        mFront = (mFront + mWaiting.size() - 1) % mWaiting.size();
        mWaiting[mFront] = asked;
        mPositions[asked.mMaterial] = static_cast<std::uint32_t>(mFront);
        ++mCount;
    }

    std::size_t CompositeQueue::take(SceneDesc& scene, const LayerStanding& standing, const std::size_t limit)
    {
        const std::uint64_t room = scene.textures().getRoomMade();
        if (mRefused.stands(sCompositeTake, room))
            return 0;

        // Each ask looked at once a frame at most: one whose layers still stand in goes to the back.
        const std::size_t waiting = mCount;
        std::size_t finished = 0;
        for (std::size_t looked = 0; looked < waiting && finished < limit; ++looked)
        {
            const Asked asked = mWaiting[mFront];
            mFront = (mFront + 1) % mWaiting.size();
            --mCount;
            if (asked.mMaterial != sNoRow)
                mPositions[asked.mMaterial] = sNoPosition;

            // What it asked for has to still be what stands there, or one hillside's ground lands
            // on another's.
            if (!stillWants(scene, asked.mMaterial, asked.mLayers))
                continue;

            // **Flattened once every layer's texture stands**, and shading from its stack until then: a bake over a
            // layer still read from its file held the stand-in's grey in the distance until the chunk was placed
            // again.
            if (!layersStand(scene, asked.mLayers, standing))
            {
                wait(asked);
                continue;
            }

            // A table with no room left keeps the chunk on its stack, which the shader sums at the
            // hit as it does for every chunk still waiting, and keeps its place in line: dropped, it
            // would never ask again, because its material is not written again.
            const TextureIndex slot = scene.textures().addGround(asked.mMaterial, TextureKind::GroundAlbedo);
            if (slot == sNoRow)
            {
                putBack(asked);
                mRefused.refuse(sCompositeTake, room);
                break;
            }

            Material given = scene.materials().getRows()[asked.mMaterial];
            given.mDiffuse = slot;

            // A table with room for the albedo and not the gloss flattens the chunk with no lobe,
            // which is what it was before it could have one.
            if (reflects(scene, given.mLayers))
                given.mSpecular = scene.textures().addGround(asked.mMaterial, TextureKind::GroundGloss);

            scene.setMaterial(asked.mMaterial, given);
            ++finished;
        }

        return finished;
    }
}
