#pragma once

#include <array>
#include <vector>

#include <components/rtx/scene/rowhold.hpp>
#include <components/rtx/world/moon.hpp>
#include <components/vfs/pathutil.hpp>

namespace Resource
{
    class ImageManager;
}

namespace Rtx
{
    struct ThreadContent;
    class SceneDesc;

    /// The painted face of `moon`, as the content files name it: what `addMoonFaces` reads and what
    /// the game preloads, from this one answer.
    constexpr VFS::Path::NormalizedView moonFaceOf(const Moon moon)
    {
        constexpr std::array<VFS::Path::NormalizedView, sMoonCount> faces{
            VFS::Path::NormalizedView("textures/tx_masser_full.dds"),
            VFS::Path::NormalizedView("textures/tx_secunda_full.dds"),
        };
        return faces[indexOf(moon)];
    }

    /// Adds each moon's face, `moonFaceOf`, opened from `images`, to `scene`, with how wide `sizes`
    /// draws each moon and what each face averages, read through `thread`, appending a hold on each
    /// to `holds`, which the caller gives back when the world goes. A moon drawn from the mean of
    /// its portrait is a coloured circle. A moon of size nought is not drawn, as the game draws
    /// none; one whose size is below nought or not finite is refused to `scene`.
    MoonFaces addMoonFaces(SceneDesc& scene, Resource::ImageManager& images, const MoonSizes& sizes,
        ThreadContent& thread, std::vector<TextureHold>& holds);
}
