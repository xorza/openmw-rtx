#pragma once

#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/shaders/sky.h>
#include <components/vfs/pathutil.hpp>

namespace osg
{
    class Node;
}

namespace Resource
{
    class SceneManager;
}

namespace Rtx
{
    /// Where Morrowind's atmosphere fades the fog colour to the sky colour, `Shaders::SkyRamp`, and
    /// what that fade is worth to a surface facing the sky. Read off the mesh the rasterizer draws
    /// it with, because the mesh is content: a sky mod's atmosphere is read the same way.
    ///
    /// **A fresh one is no atmosphere**: the fog colour everywhere, as the rasterizer draws with no
    /// mesh to draw.
    struct Atmosphere
    {
        Shaders::SkyRamp mRamp{ .mBottom = 2.0f, .mTop = 2.0f };

        /// The cosine-weighted mean of `Shaders::skyShare` over the hemisphere: how much of the sky
        /// colour a surface facing the sky receives, against the fog colour's `1 - mZenithShare`.
        /// 0.9076 on Morrowind's own mesh.
        float mZenithShare = 0.0f;
    };

    /// Reads it off the atmosphere mesh the configuration names. An error where the file does not
    /// exist, as `readCloudShell` answers, saying why and leaving the name to whoever reports it.
    Misc::Result<Atmosphere, std::string> readAtmosphere(
        Resource::SceneManager& scenes, VFS::Path::NormalizedView mesh);

    /// The same reading, of a mesh already in hand: the mean radius and height of each of the two
    /// rings `Sky::atmosphereAlphaOf` names, as the graph places them. No atmosphere where the mesh
    /// holds no two rings with the upper one higher.
    Atmosphere readAtmosphere(osg::Node& mesh);

    /// `Atmosphere::mZenithShare` of `ramp`, in closed form.
    float zenithShareOf(const Shaders::SkyRamp& ramp);
}
