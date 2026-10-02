#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/rtx/shaders/sky.h>
#include <components/vfs/pathutil.hpp>

#include "atmosphere.hpp"
#include "cloudshell.hpp"
#include "moonbuilder.hpp"
#include "nightsky.hpp"
#include "skylight.hpp"

namespace Rtx
{
    class ImageFactCache;
    class SceneDesc;

    /// Which meshes the sky's two surfaces are read off — `Models/skyclouds` and the two star
    /// domes — named by the host, because this library holds no settings registry.
    struct SkyMeshes
    {
        /// The cap the cloud deck is painted on.
        VFS::Path::Normalized mClouds;

        /// The cylinder the fog colour fades to the sky colour on.
        VFS::Path::Normalized mAtmosphere;

        /// The star dome, and the one to fall back to where the archives hold no `mStars`.
        VFS::Path::Normalized mStars;
        VFS::Path::Normalized mStarsFallback;
    };

    /// One cloud sheet the sky holds, found by the name a weather gives it.
    struct CloudSheet
    {
        /// What `WeatherResult` names it by: the fallback's or a script's bare file name, as the
        /// rasterizer is handed it, so a frame finds the sheet by comparing names and resolves no
        /// path. A name that did not open is kept too, with no texture, so it is asked once.
        std::string mName;

        /// What the shader takes, or `sNoIndex` where the name did not open.
        Index mTexture = sNoIndex;

        /// The mean luminance of what the sheet paints, linear: what a texel is read as a ratio to,
        /// so the painting gives shape and not a level. Each sheet is a photograph of a 2002 sky with
        /// that day's light in it, and for half the decks it is the only shape there is —
        /// `tx_sky_overcast`, `_rainy` and `_thunder` carry an alpha of 255 in every texel; their
        /// means are 0.268, 0.283 and 0.357, against clear 0.435, cloudy 0.552, foggy 0.639.
        /// Measured over the alpha, because clear weather's cirrus covers a quarter of its sheet.
        float mMean = 0.0f;

        /// The mean alpha: how much sky the deck hides on average, which a cloud's shadow is measured
        /// against because the content's own `Sun_*_Color` has already dimmed the sun for that
        /// weather (`Shaders::CLOUD_SHADOW_DEPTH`). A quarter for clear weather's cirrus, three
        /// quarters for cloudy, all of it for the three opaque sheets.
        float mCover = 0.0f;
    };

    /// No sheet: a weather that names none, which the shipped fallbacks do for ash and blight.
    inline constexpr std::uint32_t sNoSheet = ~std::uint32_t{ 0 };

    /// Everything the sky was read from the content files: its sheets, the night sky and the
    /// surfaces they are laid on. The textures are held, through the list `addSkyContent` fills,
    /// rather than named by a material: they are found by rays that reached nothing, and a slot
    /// nothing holds is freed.
    struct SkyContent
    {
        /// Every sheet held, by name: the ten weathers' from the fallbacks at attach, under a
        /// megabyte, so a storm arriving costs no upload, and whatever a script named since —
        /// `SkyReader::follow`.
        std::vector<CloudSheet> mSheets;

        /// The night sky, read off the mesh the rasterizer draws it with: the star field, the scale
        /// its sheet is laid at, where it fades, and the six patches painted across it.
        NightSky mNight;

        /// The surface every weather's deck hangs on, read off the cloud mesh. One shell for all
        /// ten, because the engine draws all ten on the one mesh.
        CloudShell mShell;

        /// Where the fog colour fades to the sky colour, read off the atmosphere mesh. One for
        /// every weather, as the engine draws every weather's on the one mesh.
        Atmosphere mAtmosphere;

        /// The sheet named `name`, or `sNoSheet` where none is held by it. An empty name is none.
        std::uint32_t sheetNamed(std::string_view name) const;

        /// The sheet at `sheet` where it is one a deck can draw, or null.
        const CloudSheet* drawable(std::uint32_t sheet) const;
    };

    /// Opens the sheet a weather names `name`, once: loads its texture into `scene`, reads its mean
    /// and cover, and appends it to `content`, with no texture where it does not open, refused to
    /// `scene` as the sky layer it is. A deck's sheet is left out rather than stood in for, because
    /// the stand-in is an opaque grey, which over a cloud deck is the entire sky.
    ///
    /// @return its index in `content.mSheets`.
    std::uint32_t addCloudSheet(SceneDesc& scene, Resource::SceneManager& scenes, ImageFactCache& facts,
        std::vector<TextureHold>& holds, std::string_view name, SkyContent& content);

    /// Reads all of it, loading the textures into `scene` and appending a hold on each to `holds`,
    /// which the caller gives back when the world goes, so a scene the world has left holds nothing
    /// of its sky. A file this cannot take is refused to `scene` and its layer left out: content
    /// short of a file is content the game still runs, and the shipped fallbacks name Solstheim's
    /// two skies without Bloodmoon.
    ///
    /// @param facts what each sheet's mean is read through: the frame thread's.
    SkyContent addSkyContent(SceneDesc& scene, Resource::SceneManager& scenes, const SkyMeshes& meshes,
        ImageFactCache& facts, std::vector<TextureHold>& holds);

    /// What a cloud deck radiates from below, where its own body shadows it and where it does not.
    /// The deck takes only the *shape* out of a sheet (`CloudSheet::mMean`) and the colour
    /// comes from here.
    struct DeckLight
    {
        /// What a cloud in full sunlight shows: everything reaching the top of the layer.
        osg::Vec3f mLit;

        /// What a cloud in its own shadow shows: the sky alone, which is the light a deck cannot
        /// keep off its own base.
        osg::Vec3f mShadowed;
    };

    /// Lights a deck by what stands over it, however a sun, a sky and two moons were reached.
    ///
    /// @param skyMean what the sky over the deck delivers, as a radiance — `SkyBudget::mMean`.
    /// @param moons both of them, whether or not either is up: a moon that is down delivers nothing
    ///        and needs no test of its own.
    DeckLight deckLight(const Sun& sun, const osg::Vec3f& skyMean, std::span<const MoonPlacement, 2> moons);

    /// Which sheet the deck over the eye wears, which one is arriving, and how the two stand: the
    /// sheets the weather names, `SkyContent::sheetNamed`.
    struct CloudCrossing
    {
        std::uint32_t mSheet = sNoSheet;
        std::uint32_t mNext = sNoSheet;

        /// How far the deck has crossed from this weather's sheet to the next one's.
        float mBlend = 0.0f;

        /// Where each weather drives what it carries, which is what its sheet is turned by. The
        /// engine turns each of its two cloud meshes by its own weather's storm; a direction nobody
        /// stated reads as due north rather than as a sheet with no size.
        osg::Vec3f mDirection = osg::Vec3f(0.0f, 1.0f, 0.0f);
        osg::Vec3f mNextDirection = osg::Vec3f(0.0f, 1.0f, 0.0f);

        /// How far the deck has scrolled, `Sky::SkyClock::mCloudScroll`, which both sheets share:
        /// the engine sets one texture matrix on both of its cloud updaters.
        float mScroll = 0.0f;
    };

    /// The cloud deck, in the units the shader takes — one conversion, so a screenshot and a
    /// played frame stand under one sky.
    Shaders::CloudDeck describeClouds(const CloudCrossing& clouds, const DeckLight& light, const SkyContent& textures);

    /// The star field, in the units the shader takes.
    ///
    /// @param fade the engine's `Stars` ramp at this hour, which is what brings them out at dusk.
    /// @param glare the weather's `Glare_View`, which is what keeps them in under an overcast.
    /// @param turn `Rtx::WorldReading::mStarRoll`.
    Shaders::StarField describeStars(float fade, float glare, float turn, const SkyContent& textures);

    /// The nebulae and the constellations, placed — the same shape a moon is, a direction, a size
    /// and a texture, drawn as the disc the moons are. Where they go was read off the mesh.
    ///
    /// @param turn `Rtx::WorldReading::mStarRoll`, because they are on the star sphere.
    /// @param patches written here rather than returned, so a frame's description costs no
    ///        allocation.
    void describePatches(
        float turn, const SkyContent& textures, std::span<Shaders::SkyPatch, Shaders::SKY_PATCH_COUNT> patches);
}
