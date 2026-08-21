#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>

#include "lighting.hpp"
#include "world.hpp"

namespace ESM
{
    struct Cell;
}

namespace RtxTool
{
    /// What reading a cell produced besides the scene itself.
    struct CellReport
    {
        RtxBridge::ExtractionStats mStats;
        RtxBridge::ExtractionStats mTerrain;
        World::SkippedObjects mSkipped;

        /// References whose model is named but will not load. Logged individually as they fail.
        std::uint32_t mUnreadable = 0;

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
    CellReport readCell(World& world, const ESM::Cell& cell, RtxBridge::SceneExtractor& extractor);

    /// Everything one cell puts into `scene`, and how it is lit.
    ///
    /// Geometry and lights through `extractor` and `scene`, and the sky, water and air as the
    /// return. **In the library rather than beside `main` because it has three callers now** — the
    /// screenshot, the window, and the test that needs a frame of real content to measure.
    CellLighting loadCell(World& world, const ESM::Cell& cell, Rtx::SceneDesc& scene,
        RtxBridge::SceneExtractor& extractor, std::string_view weather, float hour);
}
