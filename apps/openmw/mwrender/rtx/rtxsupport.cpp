#include "rtxsupport.hpp"

#include <array>
#include <string_view>

namespace MWRender
{
    namespace
    {
        constexpr std::string_view sNearClip
            = "The trace starts each ray at the eye, and no near plane cuts the picture.";
        constexpr std::string_view sNoCulling = "The trace culls nothing by its size on the screen.";
        constexpr std::string_view sChunkCache
            = "This sets how long the rasterizer keeps its chunks. The ray tracer keeps what its ring of cells holds.";
        constexpr std::string_view sLoadingBudget
            = "This sets the rasterizer's loading budget for each frame. The ray tracer loads cells off the frame.";
        constexpr std::string_view sFog
            = "The ray tracer integrates the air along each ray, as dense as the weather's own fog depth makes it.";
        constexpr std::string_view sFiltering = "The ray tracer filters every texture trilinearly.";
        constexpr std::string_view sGroundcover = "The ray tracer does not draw groundcover yet.";
        constexpr std::string_view sDrawThreads = "This sets the rasterizer's draw threads.";
        constexpr std::string_view sPostProcessing
            = "Shader post-processing runs on the rasterizer. The ray tracer has its own exposure, bloom and tone "
              "curve.";
        constexpr std::string_view sAlphaTest
            = "The trace cuts an alpha-tested surface for each ray, with no coverage to adjust.";
        constexpr std::string_view sEnvironmentMaps = "The ray tracer adds an environment map after the lighting.";
        constexpr std::string_view sLamps
            = "The ray tracer lights each surface from every lamp in reach, by the lamp's own falloff.";
        constexpr std::string_view sSunAtDisc = "The ray tracer's sun stands where the sun's disc is.";
        constexpr std::string_view sRoomAmbient = "The ray tracer lights a room by the ambient its record states.";
        constexpr std::string_view sSoftParticles = "The trace meets a particle as a volume, which a wall cuts softly.";
        constexpr std::string_view sShadows = "Each surface casts a traced shadow.";
        constexpr std::string_view sOneEye = "The ray tracer draws one eye.";
        constexpr std::string_view sTerrainChunks
            = "This sets the rasterizer's terrain chunks. The ray tracer stands each cell of its reach whole.";
        constexpr std::string_view sMergedObjects
            = "This sets how the rasterizer merges distant objects. The ray tracer places each distant object whole.";
        constexpr std::string_view sAntialiasing = "The ray tracer's reconstruction resolves the edges.";
        constexpr std::string_view sWater = "The ray tracer reflects and refracts each water surface by its own rays.";

        /// Every key the game's renderers read, the rasterizer's own and the shared files', and
        /// every key of the settings window's rendering pages, by category: `RtxSupportTest` holds
        /// it to the tree. A category with no name answers for every key of it.
        constexpr auto sSettings = std::to_array<SettingSupport>({
            { "Camera", "field of view", {} },
            { "Camera", "first person field of view", {} },
            { "Camera", "near clip", sNearClip },
            { "Camera", "small feature culling", sNoCulling },
            { "Camera", "small feature culling pixel size", sNoCulling },
            { "Camera", "viewing distance", {} },
            { "Cells", "cache expiry delay", sChunkCache },
            { "Cells", "target framerate", sLoadingBudget },
            { "Fog", "", sFog },
            { "Game", "day night switches", {} },
            { "Game", "graphic herbalism", {} },
            { "Game", "shield sheathing", {} },
            { "Game", "smooth animation transitions", {} },
            { "Game", "use additional anim sources", {} },
            { "Game", "weapon sheathing", {} },
            { "General", "anisotropy", {} },
            { "General", "texture mag filter", sFiltering },
            { "General", "texture min filter", sFiltering },
            { "General", "texture mipmap", sFiltering },
            { "Groundcover", "", sGroundcover },
            { "Map", "global map cell size", {} },
            { "Map", "local map resolution", {} },
            { "Models", "baseanim", {} },
            { "Models", "baseanimfemale", {} },
            { "Models", "baseanimfemale1st", {} },
            { "Models", "baseanimkna", {} },
            { "Models", "baseanimkna1st", {} },
            { "Models", "skyatmosphere", {} },
            { "Models", "skyclouds", {} },
            { "Models", "skynight01", {} },
            { "Models", "skynight02", {} },
            { "Models", "weatherashcloud", {} },
            { "Models", "weatherblightcloud", {} },
            { "Models", "weatherblizzard", {} },
            { "Models", "weathersnow", {} },
            { "Models", "wolfskin", {} },
            { "Models", "wolfskin1st", {} },
            { "Models", "xargonianswimkna", {} },
            { "Models", "xargonianswimknakf", {} },
            { "Models", "xbaseanim", {} },
            { "Models", "xbaseanim1st", {} },
            { "Models", "xbaseanim1stkf", {} },
            { "Models", "xbaseanimfemale", {} },
            { "Models", "xbaseanimfemalekf", {} },
            { "Models", "xbaseanimkf", {} },
            { "Navigator", "enable agents paths render", {} },
            { "Navigator", "enable nav mesh render", {} },
            { "Navigator", "enable recast mesh render", {} },
            { "Navigator", "nav mesh render mode", {} },
            { "Physics", "async num threads", sDrawThreads },
            { "Post Processing", "", sPostProcessing },
            { "RTX", "distant land cells", {} },
            { "RTX", "enabled", {} },
            { "RTX", "specular map layout", {} },
            { "RTX", "upscale", {} },
            { "Shaders", "adjust coverage for alpha test", sAlphaTest },
            { "Shaders", "antialias alpha test", sAlphaTest },
            { "Shaders", "apply lighting to environment maps", sEnvironmentMaps },
            { "Shaders", "auto use terrain normal maps", {} },
            { "Shaders", "auto use terrain specular maps", {} },
            { "Shaders", "clamp lighting", sLamps },
            { "Shaders", "classic falloff", sLamps },
            { "Shaders", "clustered lighting", sLamps },
            { "Shaders", "force per pixel lighting", sLamps },
            { "Shaders", "light fade start", sLamps },
            { "Shaders", "light radius multiplier", sLamps },
            { "Shaders", "match sunlight to sun", sSunAtDisc },
            { "Shaders", "max lights", sLamps },
            { "Shaders", "maximum light distance", sLamps },
            { "Shaders", "minimum interior brightness", sRoomAmbient },
            { "Shaders", "normal height map pattern", {} },
            { "Shaders", "normal map pattern", {} },
            { "Shaders", "particle point lighting", sLamps },
            { "Shaders", "soft particles", sSoftParticles },
            { "Shaders", "terrain specular map pattern", {} },
            { "Shaders", "weather particle occlusion", {} },
            { "Shaders", "weather particle occlusion small feature culling pixel size", sNoCulling },
            { "Shadows", "", sShadows },
            { "Stereo", "", sOneEye },
            { "Stereo View", "", sOneEye },
            { "Terrain", "composite map level", sTerrainChunks },
            { "Terrain", "composite map resolution", sTerrainChunks },
            { "Terrain", "debug chunks", sTerrainChunks },
            { "Terrain", "distant terrain", sTerrainChunks },
            { "Terrain", "lod factor", sTerrainChunks },
            { "Terrain", "max composite geometry size", sTerrainChunks },
            { "Terrain", "object paging", {} },
            { "Terrain", "object paging active grid", sMergedObjects },
            { "Terrain", "object paging merge factor", sMergedObjects },
            { "Terrain", "object paging min size", {} },
            { "Terrain", "object paging min size cost multiplier", sMergedObjects },
            { "Terrain", "object paging min size merge factor", sMergedObjects },
            { "Terrain", "vertex lod mod", sTerrainChunks },
            { "Terrain", "water culling", sTerrainChunks },
            { "Video", "antialiasing", sAntialiasing },
            { "Video", "gamma", {} },
            { "Video", "minimize on focus loss", {} },
            { "Video", "resolution x", {} },
            { "Video", "resolution y", {} },
            { "Video", "screen", {} },
            { "Video", "vsync mode", {} },
            { "Video", "window border", {} },
            { "Video", "window height", {} },
            { "Video", "window mode", {} },
            { "Video", "window width", {} },
            { "Water", "", sWater },
        });

        constexpr std::array sModes{
            ModeSupport{ Render_Wireframe, "The ray tracer draws no wireframe." },
        };

        constexpr std::array sRequests{
            RequestSupport{ ScriptRequest::ShaderReload,
                "The ray tracer's kernels are compiled into the build, and a rebuild changes them." },
            RequestSupport{ ScriptRequest::LiveShaderReload,
                "The ray tracer's kernels are compiled into the build, and a rebuild changes them." },
        };

        constexpr RenderSupport sSupport(sSettings, sModes, sRequests);
    }

    const RenderSupport& rtxSupport()
    {
        return sSupport;
    }
}
