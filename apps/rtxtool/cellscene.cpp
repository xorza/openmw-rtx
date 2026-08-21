#include "cellscene.hpp"

#include <cmath>
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

        /// Calls `visit` for every cell in the region that `loaded` does not already name, and adds
        /// each one to it.
        ///
        /// An interior is its own region: it has no neighbours to have. An exterior square that runs
        /// off the edge of the world, or over open sea, simply finds fewer cells — a coastline is
        /// not an error.
        void forEachNewCell(World& world, const ESM::Cell& centre, int radius, std::set<std::string>& loaded,
            const std::function<void(const ESM::Cell&)>& visit)
        {
            if (!centre.isExterior())
            {
                if (loaded.insert(centre.mName).second)
                    visit(centre);

                return;
            }

            const int x = centre.getGridX();
            const int y = centre.getGridY();

            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    std::string spec = std::to_string(x + dx) + ',' + std::to_string(y + dy);
                    if (loaded.contains(spec))
                        continue;

                    if (const ESM::Cell* cell = world.findCell(spec))
                    {
                        loaded.insert(std::move(spec));
                        visit(*cell);
                    }
                }
        }
    }

    std::string cellAt(const osg::Vec3f& position)
    {
        const auto square
            = [](float value) { return static_cast<int>(std::floor(value / static_cast<float>(ESM::Cell::sSize))); };

        return std::to_string(square(position.x())) + ',' + std::to_string(square(position.y()));
    }

    CellReport readRegion(World& world, const ESM::Cell& centre, int radius, RtxBridge::SceneExtractor& extractor,
        std::set<std::string>& loaded)
    {
        CellReport report;

        // **Every new cell's terrain into the graph before any of it is mirrored.**
        // `World::buildTerrain` accumulates chunks under one node and hands back that same node each
        // time, so extracting after each call would place every earlier cell's chunks again — once
        // more per cell.
        //
        // And only the chunks that were not there before, which is what the count remembered here
        // is for: a camera walking into the next cell brings a ring of new terrain and must not
        // place the region it was already standing in a second time.
        // **Counted before anything loads, not after the first cell that did.** A cell with no land
        // record still returns the accumulating node and adds no chunk to it, so taking the count
        // from inside the loop would start one short and mirror a chunk that was already there.
        const osg::Group* root = world.getTerrainRoot();
        const unsigned int before = root != nullptr ? root->getNumChildren() : 0;

        osg::ref_ptr<osg::Group> terrain;
        forEachNewCell(world, centre, radius, loaded, [&](const ESM::Cell& cell) {
            ++report.mCells;
            if (osg::ref_ptr<osg::Group> chunks = world.buildTerrain(cell))
                terrain = std::move(chunks);
        });

        // Terrain before the objects, because it is what everything else stands on and its absence
        // is the loudest thing about a screenshot that lacks it.
        if (terrain != nullptr)
            for (unsigned int at = before; at < terrain->getNumChildren(); ++at)
                report.mTerrain += extractor.extract(*terrain->getChild(at), osg::Matrixf::identity());

        // Interiors were authored against a renderer with no bounce, so the cell's own ambient
        // is most of what lights one. An exterior's `AMBI` is the weather system's business and
        // is not read here.
        if (!centre.isExterior() && centre.mHasAmbi)
            report.mAmbient = RtxBridge::decodeColour(centre.mAmbi.mAmbient);

        // A second walk over the same region: the first pass added them to `loaded`, so this one
        // has to be told about the cells it just took. Kept apart because the terrain has to be in
        // the graph and mirrored before the objects standing on it are.
        std::set<std::string> objects;
        forEachNewCell(world, centre, radius, objects,
            [&](const ESM::Cell& cell) { readObjects(world, cell, extractor, report); });

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
        RtxBridge::SceneExtractor& extractor, std::set<std::string>& loaded, std::string_view weather, float hour)
    {
        const CellReport report = readRegion(world, centre, radius, extractor, loaded);
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
