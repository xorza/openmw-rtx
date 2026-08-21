#include "cellscene.hpp"

#include <functional>
#include <limits>
#include <optional>
#include <string>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtxbridge/fogbuilder.hpp>
#include <components/rtxbridge/lightbuilder.hpp>
#include <components/rtxbridge/texturebuilder.hpp>
#include <components/rtxbridge/waterbuilder.hpp>

namespace RtxTool
{
    namespace
    {
        void readObjects(World& world, const ESM::Cell& cell, RtxBridge::SceneExtractor& extractor, CellReport& report);

        /// Calls `visit` for every cell in the region, centre first.
        ///
        /// An interior is its own region: it has no neighbours to have. An exterior square that runs
        /// off the edge of the world, or over open sea, simply finds fewer cells — a coastline is
        /// not an error.
        void forEachCell(
            World& world, const ESM::Cell& centre, int radius, const std::function<void(const ESM::Cell&)>& visit)
        {
            if (!centre.isExterior())
            {
                visit(centre);
                return;
            }

            const int x = centre.getGridX();
            const int y = centre.getGridY();

            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx)
                    if (const ESM::Cell* cell = world.findCell(std::to_string(x + dx) + ',' + std::to_string(y + dy)))
                        visit(*cell);
        }
    }

    CellReport readRegion(World& world, const ESM::Cell& centre, int radius, RtxBridge::SceneExtractor& extractor)
    {
        CellReport report;

        // **Every cell's terrain into the graph before any of it is mirrored.** `World::buildTerrain`
        // accumulates chunks under one node and hands back that same node each time, so extracting
        // after each call would place every earlier cell's chunks again — once more per cell.
        osg::ref_ptr<osg::Node> terrain;
        forEachCell(world, centre, radius, [&](const ESM::Cell& cell) {
            ++report.mCells;
            if (osg::ref_ptr<osg::Node> loaded = world.buildTerrain(cell))
                terrain = std::move(loaded);
        });

        // Terrain before the objects, because it is what everything else stands on and its absence
        // is the loudest thing about a screenshot that lacks it.
        if (terrain != nullptr)
            report.mTerrain = extractor.extract(*terrain, osg::Matrixf::identity());

        // Interiors were authored against a renderer with no bounce, so the cell's own ambient
        // is most of what lights one. An exterior's `AMBI` is the weather system's business and
        // is not read here.
        if (!centre.isExterior() && centre.mHasAmbi)
            report.mAmbient = RtxBridge::decodeColour(centre.mAmbi.mAmbient);

        forEachCell(world, centre, radius, [&](const ESM::Cell& cell) { readObjects(world, cell, extractor, report); });

        return report;
    }

    namespace
    {
        void readObjects(World& world, const ESM::Cell& cell, RtxBridge::SceneExtractor& extractor, CellReport& report)
        {
            const World::SkippedObjects skipped = world.forEachObject(cell, [&](const World::Object& object) {
                if (object.mLight != nullptr)
                    if (const std::optional<Rtx::Light> light
                        = RtxBridge::makeLight(*object.mLight, object.mTransform.getTrans()))
                        report.mLights.push_back(*light);

                osg::ref_ptr<const osg::Node> node;
                try
                {
                    node = world.getSceneManager().getTemplate(object.mModel, false);
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Cannot load " << object.mModel << ": " << e.what();
                    ++report.mUnreadable;
                    return;
                }

                report.mStats += extractor.extract(*node, object.mTransform);
            });

            report.mSkipped.mUnknownType += skipped.mUnknownType;
            report.mSkipped.mNoModel += skipped.mNoModel;
        }
    }

    CellLighting loadRegion(World& world, const ESM::Cell& centre, int radius, Rtx::SceneDesc& scene,
        RtxBridge::SceneExtractor& extractor, std::string_view weather, float hour)
    {
        const CellReport report = readRegion(world, centre, radius, extractor);
        for (const Rtx::Light& light : report.mLights)
            scene.addLight(light);

        // After the geometry, because an interior's pool is sized by what the room holds and
        // there is nothing to measure until the room is in. Here rather than in `readRegion` for
        // the reason the lights are: reading a region twice must not fill it twice.
        const std::optional<float> water = RtxBridge::addWater(scene, centre);

        const float level = water.value_or(-std::numeric_limits<float>::infinity());

        // An interior's sky is never seen and its sun never shines, so the daylight stays dark.
        // Its air is its own, out of the same `AMBI` its ambient came from.
        if (!centre.isExterior())
            return CellLighting{ .mAmbient = report.mAmbient,
                .mWaterLevel = level,
                .mDaylight = {},
                .mFog = RtxBridge::interiorFog(centre) };

        const RtxBridge::Daylight daylight = RtxBridge::makeDaylight(weather, hour);
        return CellLighting{
            .mAmbient = daylight.mAmbient, .mWaterLevel = level, .mDaylight = daylight, .mFog = daylight.mFog
        };
    }
}
