#include "cellscene.hpp"

#include <limits>
#include <optional>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtxbridge/fogbuilder.hpp>
#include <components/rtxbridge/lightbuilder.hpp>
#include <components/rtxbridge/texturebuilder.hpp>
#include <components/rtxbridge/waterbuilder.hpp>

namespace RtxTool
{
    CellReport readCell(World& world, const ESM::Cell& cell, RtxBridge::SceneExtractor& extractor)
    {
        CellReport report;

        // Terrain first, because it is what everything else stands on and its absence is the
        // loudest thing about a screenshot that lacks it.
        if (const osg::ref_ptr<osg::Node> terrain = world.buildTerrain(cell))
            report.mTerrain = extractor.extract(*terrain, osg::Matrixf::identity());

        // Interiors were authored against a renderer with no bounce, so the cell's own ambient
        // is most of what lights one. An exterior's `AMBI` is the weather system's business and
        // is not read here.
        if (!cell.isExterior() && cell.mHasAmbi)
            report.mAmbient = RtxBridge::decodeColour(cell.mAmbi.mAmbient);

        report.mSkipped = world.forEachObject(cell, [&](const World::Object& object) {
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

        return report;
    }

    CellLighting loadCell(World& world, const ESM::Cell& cell, Rtx::SceneDesc& scene,
        RtxBridge::SceneExtractor& extractor, std::string_view weather, float hour)
    {
        const CellReport report = readCell(world, cell, extractor);
        for (const Rtx::Light& light : report.mLights)
            scene.addLight(light);

        // After the geometry, because an interior's pool is sized by what the room holds and
        // there is nothing to measure until the room is in. Here rather than in `readCell` for
        // the reason the lights are: reading a cell twice must not fill it twice.
        const std::optional<float> water = RtxBridge::addWater(scene, cell);

        const float level = water.value_or(-std::numeric_limits<float>::infinity());

        // An interior's sky is never seen and its sun never shines, so the daylight stays dark.
        // Its air is its own, out of the same `AMBI` its ambient came from.
        if (!cell.isExterior())
            return CellLighting{
                .mAmbient = report.mAmbient, .mWaterLevel = level, .mDaylight = {}, .mFog = RtxBridge::interiorFog(cell)
            };

        const RtxBridge::Daylight daylight = RtxBridge::makeDaylight(weather, hour);
        return CellLighting{
            .mAmbient = daylight.mAmbient, .mWaterLevel = level, .mDaylight = daylight, .mFog = daylight.mFog
        };
    }
}
