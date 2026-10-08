#pragma once

#include <string>
#include <vector>

#include <components/misc/result.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/rtx/world/nightsky.hpp>
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
    struct ThreadContent;
    class SceneDesc;

    /// Reads the night sky (`NightSky`), adding every texture it names to `scene` and appending a
    /// hold on each to `holds`, which the caller gives back with the rest of the sky. An error
    /// where neither file exists, having held nothing, saying why and leaving the name of `mesh` to
    /// whoever reports it: a gap in the content is refused and not read as a sky with no stars in
    /// it, and what the sky does without them is `addSkyContent`'s answer. A sheet the upload
    /// cannot take is refused to `scene` by its file's name, and the dome goes on without it.
    ///
    /// @param mesh the star dome the configuration names.
    /// @param fallback the dome to read where the archives hold no `mesh`: Tribunal ships the
    ///        second one and Morrowind alone does not, and the rasterizer picks by the same test.
    /// @param thread what each sheet's mean is read through.
    Misc::Result<NightSky, std::string> readNightSky(SceneDesc& scene, Resource::SceneManager& scenes,
        VFS::Path::NormalizedView mesh, VFS::Path::NormalizedView fallback, ThreadContent& thread,
        std::vector<TextureHold>& holds);

    /// The same, off a mesh already loaded: each drawable's vertices placed through the transforms
    /// above it (`placedVertices`), and its sheet the one the state sets down its path bind.
    NightSky readNightSky(
        SceneDesc& scene, const osg::Node& mesh, ThreadContent& thread, std::vector<TextureHold>& holds);
}
