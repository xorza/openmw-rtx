#include "restreuse.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <ranges>
#include <span>
#include <string>

#include <components/rtx/common/mebibytes.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/meshtable.hpp>
#include <components/rtx/scene/restqueue.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneroom.hpp>
#include <components/rtx/scene/texturetable.hpp>

namespace RtxTool
{
    RestReuse::RestReuse()
    {
        mRested.reserve(Rtx::TextureTable::sCapacity + Rtx::sWorldRoom.mMeshes);
    }

    void RestReuse::read(const Rtx::SceneDesc& scene)
    {
        // **Woken, and not freed**: a row that rested the read before, is live, rests no more and stands what it stood.
        // One freed and taken over between two reads stands nothing yet, since a take's bytes are its own stand's.
        const std::span<const Rtx::TextureRow> textures = scene.textures().getRows();
        const std::span<const Rtx::MeshRange> meshes = scene.meshes().getRows();
        for (const Rested& rested : mRested)
        {
            const bool live
                = rested.mMesh ? scene.meshes().isLive(rested.mSlot) : scene.textures().isLive(rested.mSlot);
            const Rtx::RestPlace& place = rested.mMesh ? meshes[rested.mSlot].mRest : textures[rested.mSlot].mRest;
            const std::uint64_t stood
                = rested.mMesh ? meshes[rested.mSlot].mStoodBytes : textures[rested.mSlot].mStoodBytes;
            if (!live || place.mResting || stood != rested.mBytes)
                continue;
            mWoken += rested.mBytes;
            // Its own bytes with what rested after it: the least cache that still holds it.
            const auto step = static_cast<std::size_t>((rested.mAfter + rested.mBytes) / sStep);
            if (step < sSteps)
                mSteps[step] += rested.mBytes;
        }

        // Both orders, merged as the scene frees them: by when each was filed, a texture first on one tick, each
        // table's own order within it.
        mRested.clear();
        for (Rtx::Index slot = scene.textures().getResting().first(); slot != Rtx::sNoIndex;
            slot = textures[slot].mRest.mAfter)
            mRested.push_back(Rested{ .mMesh = false,
                .mSlot = slot,
                .mFiledAt = textures[slot].mRest.mFiledAt,
                .mBytes = textures[slot].mStoodBytes });
        const auto textureEnd = static_cast<std::ptrdiff_t>(mRested.size());
        for (Rtx::Index slot = scene.meshes().getResting().first(); slot != Rtx::sNoIndex;
            slot = meshes[slot].mRest.mAfter)
            mRested.push_back(Rested{ .mMesh = true,
                .mSlot = slot,
                .mFiledAt = meshes[slot].mRest.mFiledAt,
                .mBytes = meshes[slot].mStoodBytes });
        std::ranges::inplace_merge(mRested, mRested.begin() + textureEnd, {}, &Rested::mFiledAt);

        std::uint64_t after = 0;
        for (Rested& rested : std::views::reverse(mRested))
        {
            rested.mAfter = after;
            after += rested.mBytes;
        }
    }

    std::uint64_t RestReuse::keptUnder(const std::uint64_t bytes) const
    {
        std::uint64_t kept = 0;
        for (std::size_t step = 0; step < sSteps && (step + 1) * sStep <= bytes; ++step)
            kept += mSteps[step];
        return kept;
    }

    std::string RestReuse::describe() const
    {
        if (mWoken == 0)
            return "\nrest reuse: nothing woke from rest\n";

        std::string text = std::format(
            "\nrest reuse: {:.1f} MiB woke from rest; what a cache of each size kept of it:", Rtx::mebibytes(mWoken));
        for (std::uint64_t size = std::uint64_t{ 64 } << 20; size <= sStep * sSteps; size *= 2)
            text += std::format(" {:.0f} MiB {:.1f}%,", Rtx::mebibytes(size),
                100.0 * static_cast<double>(keptUnder(size)) / static_cast<double>(mWoken));
        text.back() = '\n';
        return text;
    }
}
