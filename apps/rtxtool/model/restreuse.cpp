#include "restreuse.hpp"

#include <cstddef>
#include <format>
#include <ranges>
#include <string>

#include <components/rtx/common/mebibytes.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/meshtable.hpp>
#include <components/rtx/scene/restqueue.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneroom.hpp>

namespace RtxTool
{
    RestReuse::RestReuse()
    {
        mRested.reserve(Rtx::sWorldRoom.mMeshes);
    }

    void RestReuse::read(const Rtx::SceneDesc& scene)
    {
        // **Woken, and not freed**: a row that rested the read before, is live, rests no more and stands what it stood.
        // One freed and taken over between two reads stands nothing yet, since a take's bytes are its own stand's.
        const auto meshes = scene.meshes().getRows();
        for (const Rested& rested : mRested)
        {
            const bool live = scene.meshes().isLive(rested.mSlot);
            if (!live || meshes[rested.mSlot].mRest.mResting || meshes[rested.mSlot].mStoodBytes != rested.mBytes)
                continue;
            mWoken += rested.mBytes;
            // Its own bytes with what rested after it: the least cache that still holds it.
            const auto step = static_cast<std::size_t>((rested.mAfter + rested.mBytes) / sStep);
            if (step < sSteps)
                mSteps[step] += rested.mBytes;
        }

        mRested.clear();
        for (Rtx::MeshIndex slot = scene.meshes().getResting().first(); slot != Rtx::sNoRow;
            slot = meshes[slot].mRest.mAfter)
            mRested.push_back(
                Rested{ .mSlot = slot, .mFiledAt = meshes[slot].mRest.mFiledAt, .mBytes = meshes[slot].mStoodBytes });

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
