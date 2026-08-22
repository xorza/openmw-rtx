#include "cellscene.hpp"

#include <osg/MatrixTransform>

#include <components/misc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
        osg::ref_ptr<osg::Group> readObjects(
            World& world, const ESM::Cell& cell, osg::Group& root, CellReport& report, bool liveProps);

        /// Calls `visit` for every cell in the region that `loaded` does not already name, and adds
        /// each one to it.
        ///
        /// An interior is its own region: it has no neighbours to have. An exterior square that runs
        /// off the edge of the world, or over open sea, simply finds fewer cells — a coastline is
        /// not an error.
        /// The square of cells the game keeps active, and the order it fills it in.
        ///
        /// **Copied from `MWWorld::Scene`, deliberately, rather than shared.** The originals —
        /// `iterateOverCellsAround` and `sortCellsToLoad` — sit in an anonymous namespace in
        /// `apps/openmw/mwworld/scene.cpp`, so nothing outside that file can link to them, and
        /// lifting them into `components/` costs three upstream files to share twenty lines of
        /// arithmetic. Twenty lines is the cheaper copy.
        ///
        /// **What it has to keep is the order.** Nearest first, ties broken by distance to the
        /// origin. The scanline fill this replaced put the cells in an order the game never uses,
        /// which for a benchmark that times a camera crossing a boundary is the whole measurement.
        /// If `Scene` ever changes how it sorts, this has to follow it, and nothing here will say
        /// so — that is what the copy costs.
        std::vector<std::pair<int, int>> squareAround(int centreX, int centreY)
        {
            const int range = Constants::CellGridRadius;
            const auto side = static_cast<std::size_t>(2 * range + 1);

            std::vector<std::pair<int, int>> square;
            square.reserve(side * side);

            for (int x = centreX - range; x <= centreX + range; ++x)
                for (int y = centreY - range; y <= centreY + range; ++y)
                    square.emplace_back(x, y);

            const auto priority = [&](const std::pair<int, int>& cell) {
                return std::make_pair(std::abs(cell.first - centreX) + std::abs(cell.second - centreY),
                    std::abs(cell.first) + std::abs(cell.second));
            };

            std::sort(square.begin(), square.end(),
                [&](const std::pair<int, int>& a, const std::pair<int, int>& b) { return priority(a) < priority(b); });

            return square;
        }

        /// How a cell is keyed, and it has to be what the grid walk builds or nothing matches.
        std::string keyOf(const ESM::Cell& cell)
        {
            return cell.isExterior() ? std::to_string(cell.getGridX()) + ',' + std::to_string(cell.getGridY())
                                     : cell.mName;
        }

        void forEachNewCell(World& world, const ESM::Cell& centre, LoadedCells& loaded,
            const std::function<void(const ESM::Cell&)>& visit)
        {
            if (!centre.isExterior())
            {
                if (loaded.emplace(centre.mName, nullptr).second)
                    visit(centre);

                return;
            }

            for (const auto& [x, y] : squareAround(centre.getGridX(), centre.getGridY()))
            {
                std::string spec = std::to_string(x) + ',' + std::to_string(y);
                if (loaded.contains(spec))
                    continue;

                if (const ESM::Cell* cell = world.findCell(spec))
                {
                    loaded.emplace(std::move(spec), nullptr);
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

    std::uint32_t dropCellsOutside(const ESM::Cell& centre, osg::Group& root, LoadedCells& loaded)
    {
        if (!centre.isExterior())
            return 0;

        std::set<std::string> keep;
        for (const auto& [x, y] : squareAround(centre.getGridX(), centre.getGridY()))
            keep.insert(std::to_string(x) + ',' + std::to_string(y));

        std::uint32_t went = 0;
        for (auto entry = loaded.begin(); entry != loaded.end();)
        {
            if (keep.contains(entry->first))
            {
                ++entry;
                continue;
            }

            if (entry->second != nullptr)
                root.removeChild(entry->second);

            entry = loaded.erase(entry);
            ++went;
        }

        return went;
    }

    CellReport readRegion(World& world, const ESM::Cell& centre, osg::Group& root, LoadedCells& loaded, bool liveProps)
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
        osg::ref_ptr<osg::Group> terrain;
        forEachNewCell(world, centre, loaded, [&](const ESM::Cell& cell) {
            ++report.mCells;
            if (osg::ref_ptr<osg::Group> chunks = world.buildTerrain(cell))
                terrain = std::move(chunks);
        });

        // **Hung under the root once, and it accumulates from there.** `World::buildTerrain` keeps
        // every chunk under one node and hands that same node back each call, so this is the same
        // node every time — adding it again would put the whole worldspace under the root twice.
        //
        // Terrain is therefore not per cell the way the objects below are, which is the one thing
        // in this graph that cannot yet be unloaded a cell at a time. See `docs/rtx/harness.md`.
        if (terrain != nullptr && !root.containsNode(terrain))
            root.addChild(terrain);

        // Interiors were authored against a renderer with no bounce, so the cell's own ambient
        // is most of what lights one. An exterior's `AMBI` is the weather system's business and
        // is not read here.
        if (!centre.isExterior() && centre.mHasAmbi)
            report.mAmbient = RtxBridge::decodeColour(centre.mAmbi.mAmbient);

        // A second walk over the same region: the first pass added them to `loaded`, so this one
        // has to be told about the cells it just took. Kept apart because the terrain has to be in
        // the graph and mirrored before the objects standing on it are.
        LoadedCells objects;
        forEachNewCell(world, centre, objects,
            [&](const ESM::Cell& cell) { loaded[keyOf(cell)] = readObjects(world, cell, root, report, liveProps); });

        return report;
    }

    namespace
    {
        osg::ref_ptr<osg::Group> readObjects(
            World& world, const ESM::Cell& cell, osg::Group& root, CellReport& report, bool liveProps)
        {
            // **One group per cell, so a cell can leave the way it arrived.** The game parents every
            // reference under the cell it belongs to; taking that group off the root is what
            // unloading will be, and a flat root would leave nothing to take.
            osg::ref_ptr<osg::Group> group = new osg::Group;
            group->setName(cell.mName.empty() ? "cell" : cell.mName);

            const World::SkippedObjects skipped = world.forEachObject(cell, [&](const World::Object& object) {
                if (object.mPerson != nullptr)
                {
                    report.mPeople.push_back(CellPerson{ .mRecord = object.mPerson, .mTransform = object.mTransform });
                    return;
                }

                if (object.mLight != nullptr)
                    if (const std::optional<Rtx::Light> light
                        = RtxBridge::makeLight(*object.mLight, object.mTransform.getTrans()))
                        report.mLights.push_back(*light);

                osg::ref_ptr<osg::Node> node;
                try
                {
                    // **An instance per reference, which is what the game makes.** A shared template
                    // is one node walked under a hundred paths, so a hundred crates were one
                    // placement between them until an anchor was invented to tell them apart. Give
                    // each its own node and the node path identifies it again, exactly as it does
                    // in the game — and the anchor stops being needed.
                    node = world.getSceneManager().getInstance(object.mModel);
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Cannot load " << object.mModel << ": " << e.what();
                    ++report.mUnreadable;
                    return;
                }

                // **The same test `SceneManager::getInstance` makes**, and for the same reason: a
                // particle emitter is an update callback, so a graph with none of those has nothing
                // that changes between frames. Everything else in a cell is still, and a template
                // shared by every reference of the model is the cheaper thing to walk.
                //
                // Reported *instead of* mirrored, because the instance somebody makes of it shares
                // these very drawables and would place the same candle a second time.
                if (liveProps
                    && (node->getUpdateCallback() != nullptr || node->getNumChildrenRequiringUpdateTraversal() > 0))
                {
                    report.mProps.push_back(CellProp{ .mModel = object.mModel, .mTransform = object.mTransform });
                    return;
                }

                osg::ref_ptr<osg::MatrixTransform> where = new osg::MatrixTransform(osg::Matrixd(object.mTransform));
                where->addChild(node);
                group->addChild(where);
            });

            if (group->getNumChildren() > 0)
                root.addChild(group);

            report.mSkipped.mUnknownType += skipped.mUnknownType;
            report.mSkipped.mNoModel += skipped.mNoModel;
            return group;
        }
    }

    RegionLoad loadRegion(World& world, const ESM::Cell& centre, osg::Group& root, Rtx::SceneDesc& scene,
        RtxBridge::SceneExtractor& extractor, LoadedCells& loaded, std::string_view weather, float hour, bool liveProps)
    {
        CellReport report = readRegion(world, centre, root, loaded, liveProps);
        for (const Rtx::Light& light : report.mLights)
            scene.addLight(light);

        // After the geometry, because an interior's pool is sized by what the room holds and
        // there is nothing to measure until the room is in. Here rather than in `readRegion` for
        // the reason the lights are: reading a region twice must not fill it twice.
        const std::optional<RtxBridge::WaterSurface> water = RtxBridge::addWater(scene, centre);

        // **Held, because no walk will ever meet it.** The quad goes straight into the scene, so a
        // sweep keyed on what the graph holds would take it and leave its placement standing on a
        // mesh that is gone.
        if (water.has_value())
            extractor.hold(water->mMesh, water->mMaterial);

        const float level = water.has_value() ? water->mLevel : -std::numeric_limits<float>::infinity();

        // An interior's sky is never seen and its sun never shines, so the daylight stays dark.
        // Its air is its own, out of the same `AMBI` its ambient came from.
        if (!centre.isExterior())
            return RegionLoad{ .mLighting = CellLighting{ .mAmbient = report.mAmbient,
                                   .mWaterLevel = level,
                                   .mDaylight = {},
                                   .mFog = RtxBridge::interiorFog(centre) },
                .mPeople = std::move(report.mPeople),
                .mProps = std::move(report.mProps) };

        const RtxBridge::Daylight daylight = RtxBridge::makeDaylight(weather, hour);
        return RegionLoad{ .mLighting = CellLighting{ .mAmbient = daylight.mAmbient,
                               .mWaterLevel = level,
                               .mDaylight = daylight,
                               .mFog = daylight.mFog },
            .mPeople = std::move(report.mPeople),
            .mProps = std::move(report.mProps) };
    }
}
