#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/common/index.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/rtx/world/skycontent.hpp>
#include <components/rtx/world/weather.hpp>
#include <components/vfs/pathutil.hpp>

namespace Resource
{
    class SceneManager;
}

namespace Rtx
{
    struct ThreadContent;
    class SceneDesc;

    /// What the sky is read off — `Models/skyclouds`, the atmosphere and the two star domes, and
    /// each weather's cloud sheet — named by the host, because this library holds no settings or
    /// fallback registry.
    struct SkySources
    {
        /// The cap the cloud deck is painted on.
        VFS::Path::Normalized mClouds;

        /// The cylinder the fog colour fades to the sky colour on.
        VFS::Path::Normalized mAtmosphere;

        /// The star dome, and the one to fall back to where the archives hold no `mStars`.
        VFS::Path::Normalized mStars;
        VFS::Path::Normalized mStarsFallback;

        /// Each weather's sheet by `Weather`, as `Weather_<name>_Cloud_Texture` names it: a bare
        /// file name, and empty where a weather names none.
        std::array<std::string, sWeatherCount> mCloudSheets;
    };

    /// Opens the sheet a weather names `name`, once: loads its texture into `scene`, reads its mean
    /// and cover, and appends it to `content`, with no texture where it does not open, refused to
    /// `scene` as the sky layer it is. A deck's sheet is left out rather than stood in for, because
    /// the stand-in is an opaque grey, which over a cloud deck is the entire sky.
    ///
    /// @return its index in `content.mSheets`.
    std::uint32_t addCloudSheet(SceneDesc& scene, Resource::SceneManager& scenes, ThreadContent& thread,
        std::vector<TextureHold>& holds, std::string_view name, SkyContent& content);

    /// Reads all of it, loading the textures into `scene` and appending a hold on each to `holds`,
    /// which the caller gives back when the world goes, so a scene the world has left holds nothing
    /// of its sky. A file this cannot take is refused to `scene` and its layer left out: content
    /// short of a file is content the game still runs, and the shipped fallbacks name Solstheim's
    /// two skies without Bloodmoon.
    ///
    /// @param thread what each sheet's mean is read through: the frame thread's.
    SkyContent addSkyContent(SceneDesc& scene, Resource::SceneManager& scenes, const SkySources& sources,
        ThreadContent& thread, std::vector<TextureHold>& holds);
}
