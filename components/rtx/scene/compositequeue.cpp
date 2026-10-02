#include "compositequeue.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/shaders/scene.h>

#include "material.hpp"

namespace Rtx
{
    namespace
    {
        /// The one take `mRefused` records.
        constexpr std::uint16_t sCompositeTake = 1;

        /// The key a chunk's composite is found under — `chunk/` — or its gloss — `gloss/`: the
        /// material's own slot, because one material is one chunk, and one that takes the slot over
        /// is a different chunk that wants the slot overwritten.
        void nameComposite(std::string& key, std::string_view kind, Index material)
        {
            std::array<char, 16> digits{};
            const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), material, 16);

            key.assign(kind);
            key.append(digits.data(), written.ptr);
        }

        /// Whether any layer of `layers` reflects, which is whether a gloss says anything.
        bool reflects(const SceneDesc& scene, const Run& layers)
        {
            return std::ranges::any_of(layers.in(scene.materials().getLayers()),
                [](const MaterialLayer& layer) { return (layer.mFlags & Shaders::LAYER_AUTHORED) != 0u; });
        }

        /// Whether `material` still wants the ground it asked for as `asked`: the same kind, still
        /// asking, not yet given, and its layers where they were. The run and not the layers in
        /// it: the bake reads the row as it stands when the slot is given, so a run handed out
        /// again to another chunk flattens that chunk's ground and costs it only its place in
        /// line.
        bool stillWants(const SceneDesc& scene, const Index material, const Run& layers)
        {
            const std::span<const Material> materials = scene.materials().getRows();
            if (material >= materials.size())
                return false;

            return materials[material].wantsFlattening() && materials[material].mLayers == layers;
        }
    }

    std::size_t CompositeQueue::advance(SceneDesc& scene)
    {
        gather(scene);
        return take(scene, sCompositesPerFrame);
    }

    void CompositeQueue::gather(const SceneDesc& scene)
    {
        const std::span<const Material> materials = scene.materials().getRows();
        if (mPositions.size() < materials.size())
            mPositions.resize(materials.size(), sNoIndex);

        for (const Index at : scene.materials().getWritten())
        {
            const Material& material = materials[at];
            if (!material.wantsFlattening())
                continue;

            const Asked wanted{ .mMaterial = at, .mLayers = material.mLayers };
            if (const Index position = mPositions[at]; position != sNoIndex)
            {
                Asked& waiting = mWaiting[position];
                if (waiting == wanted)
                    continue;

                // A slot taken over by another chunk while its predecessor waited: what was asked is
                // ground that has gone, and the new chunk goes to the back of the schedule. The old
                // ask stays where it stands, naming no material, for `take` to pass over — closing
                // the gap would move every ask behind it.
                waiting.mMaterial = sNoIndex;
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
                if (grown[age].mMaterial != sNoIndex)
                    mPositions[grown[age].mMaterial] = static_cast<Index>(age);
            }

            mWaiting.swap(grown);
            mFront = 0;
        }

        const std::size_t position = (mFront + mCount) % mWaiting.size();
        mWaiting[position] = asked;
        mPositions[asked.mMaterial] = static_cast<Index>(position);
        ++mCount;
    }

    void CompositeQueue::putBack(const Asked& asked)
    {
        mFront = (mFront + mWaiting.size() - 1) % mWaiting.size();
        mWaiting[mFront] = asked;
        mPositions[asked.mMaterial] = static_cast<Index>(mFront);
        ++mCount;
    }

    std::size_t CompositeQueue::take(SceneDesc& scene, const std::size_t limit)
    {
        const std::uint64_t freed = scene.textures().getFreedCount();
        if (mRefused.stands(sCompositeTake, freed))
            return 0;

        std::size_t finished = 0;
        while (finished < limit && mCount > 0)
        {
            const Asked asked = mWaiting[mFront];
            mFront = (mFront + 1) % mWaiting.size();
            --mCount;
            if (asked.mMaterial != sNoIndex)
                mPositions[asked.mMaterial] = sNoIndex;

            // What it asked for has to still be what stands there, or one hillside's ground lands
            // on another's.
            if (!stillWants(scene, asked.mMaterial, asked.mLayers))
                continue;

            nameComposite(mKey, "chunk/", asked.mMaterial);

            // A table with no room left keeps the chunk on its stack, which the shader sums at the
            // hit as it does for every chunk still waiting, and keeps its place in line: dropped, it
            // would never ask again, because its material is not written again.
            const Index slot = scene.textures().addBaked(mKey, TextureEncoding::Colour);
            if (slot == sNoIndex)
            {
                putBack(asked);
                mRefused.refuse(sCompositeTake, freed);
                break;
            }

            Material given = scene.materials().getRows()[asked.mMaterial];
            given.mDiffuse = slot;
            mFinished.push_back(Given{ .mSlot = slot, .mBaked = { .mMaterial = asked.mMaterial, .mGloss = false } });

            // A table with room for the albedo and not the gloss flattens the chunk with no lobe,
            // which is what it was before it could have one.
            if (reflects(scene, given.mLayers))
            {
                nameComposite(mKey, "gloss/", asked.mMaterial);
                given.mSpecular = scene.textures().addBaked(mKey, TextureEncoding::Data);
                if (given.mSpecular != sNoIndex)
                    mFinished.push_back(
                        Given{ .mSlot = given.mSpecular, .mBaked = { .mMaterial = asked.mMaterial, .mGloss = true } });
            }

            scene.setMaterial(asked.mMaterial, given);
            ++finished;
        }

        return finished;
    }

    CompositeQueue::Baked CompositeQueue::find(const Index slot) const
    {
        for (const Given& finished : mFinished)
            if (finished.mSlot == slot)
                return finished.mBaked;

        return Baked{};
    }
}
