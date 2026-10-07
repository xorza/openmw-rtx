#pragma once

#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/world/atmosphere.hpp>
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
    /// Reads the `Atmosphere` off the atmosphere mesh the configuration names. An error where the
    /// file does not exist, as `readCloudShell` answers, saying why and leaving the name to whoever
    /// reports it.
    Misc::Result<Atmosphere, std::string> readAtmosphere(
        Resource::SceneManager& scenes, VFS::Path::NormalizedView mesh);

    /// The same reading, of a mesh already in hand: the mean radius and height of each of the two
    /// rings `Sky::atmosphereAlphaOf` names, as the graph places them. No atmosphere where the mesh
    /// holds no two rings with the upper one higher.
    Atmosphere readAtmosphere(const osg::Node& mesh);
}
