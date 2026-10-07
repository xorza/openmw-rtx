#pragma once

#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/world/cloudshell.hpp>
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
    /// Reads the deck's `CloudShell` off the cloud mesh the configuration names, which the host
    /// passes in. An error where the file does not exist, as `readNightSky` answers, saying why and
    /// leaving the name to whoever reports it. A mesh that is there and says nothing hangs no
    /// layer, which the overload below says.
    Misc::Result<CloudShell, std::string> readCloudShell(
        Resource::SceneManager& scenes, VFS::Path::NormalizedView mesh);

    /// The same reading, of a mesh already in hand.
    CloudShell readCloudShell(const osg::Node& mesh);
}
