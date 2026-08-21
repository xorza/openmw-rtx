#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Matrixf>
#include <osg/Vec3f>

#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>

#include "lighting.hpp"
#include "world.hpp"

namespace ESM
{
    struct Cell;
    struct NPC;
}

namespace RtxTool
{
    /// One person a cell places, and where they stand.
    ///
    /// **Reported rather than built, for the reason the lights are.** A body is a graph the extractor
    /// keys its meshes on, so whoever owns it has to own it for as long as the scene names it — and
    /// reading a region twice must not stand two of everyone in it.
    struct CellPerson
    {
        /// Points into the loaded content, which outlives the run.
        const ESM::NPC* mRecord = nullptr;
        osg::Matrixf mTransform;
    };

    /// What reading a cell produced besides the scene itself.
    struct CellReport
    {
        RtxBridge::ExtractionStats mStats;
        RtxBridge::ExtractionStats mTerrain;
        World::SkippedObjects mSkipped;

        /// References whose model is named but will not load. Logged individually as they fail.
        std::uint32_t mUnreadable = 0;

        /// Everyone the region places. Empty is a wilderness cell, not a failure.
        std::vector<CellPerson> mPeople;

        /// How many cells the region actually found. Fewer than asked for at a coastline.
        std::uint32_t mCells = 0;

        /// What the cell's `LIGH` references cast. Carried, negative and off-by-default records
        /// place a mesh and no light, so this is shorter than the cell's list of them.
        ///
        /// Reported rather than placed, because reading a cell twice must not light it twice.
        /// Geometry is safe from that — the extractor recognises what it has already seen — and
        /// a light has no such identity, so the decision to place one belongs to whoever knows
        /// whether this cell is already in the scene.
        std::vector<Rtx::Light> mLights;

        /// The cell's ambient, linear. Black for an exterior, whose sky is M5's.
        osg::Vec3f mAmbient;
    };

    /// Mirrors one cell's geometry through `extractor`, and reports what else it holds.
    ///
    /// The content arrives by two routes and only one of them is the scene graph: lights are not
    /// in it at all, because `NifOsg` never reads `NiLight`. They come off the `LIGH` records the
    /// same references point at, and leave here in the report.
    /// The same, over a square of exterior cells centred on `centre`.
    ///
    /// @param radius cells out from the centre in each direction, so three is seven by seven.
    ///        Ignored for an interior, which has no neighbours. Cells the content files do not
    ///        define are open sea and are skipped rather than missing.
    /// @param loaded which cells are already in the scene. Cells named here are left alone and
    ///        every cell this places is added to it, so a caller that keeps one across calls walks
    ///        into a region rather than reloading it.
    CellReport readRegion(World& world, const ESM::Cell& centre, int radius, RtxBridge::SceneExtractor& extractor,
        std::set<std::string>& loaded);

    /// The exterior cell a point stands in, as `--cell` spells it.
    ///
    /// A point outside every cell the content files define still has a square: what it does not have
    /// is a cell record there, which is what `World::findCell` says by answering nothing.
    std::string cellAt(const osg::Vec3f& position);

    /// Everything a region puts into `scene`, and how its centre is lit.
    ///
    /// Geometry and lights through `extractor` and `scene`, and the sky, water and air as the
    /// return. **In the library rather than beside `main` because it has three callers now** — the
    /// screenshot, the window, and the test that needs a frame of real content to measure.
    /// What loading a region left for its caller to place.
    ///
    /// The lights and the water are already in the scene; these two are not, because neither is
    /// something a second read of the same region may do twice — a light has no identity to
    /// recognise, and a person is a graph whose owner has to outlive the scene that names it.
    struct RegionLoad
    {
        CellLighting mLighting;
        std::vector<CellPerson> mPeople;
    };

    RegionLoad loadRegion(World& world, const ESM::Cell& centre, int radius, Rtx::SceneDesc& scene,
        RtxBridge::SceneExtractor& extractor, std::set<std::string>& loaded, std::string_view weather, float hour);
}
