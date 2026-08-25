#include "stagedworld.hpp"

#include <span>
#include <string>

#include <components/esm/util.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/fallback/fallback.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/weather/downpour.hpp>

#include "cellscene.hpp"
#include "world.hpp"

namespace RtxTool
{
    StagedWorld::StagedWorld(
        World& world, const ESM::Cell& cell, const StagingRequest& request, const ActorRequest& actors)
        : mExtractor(mScene)
        , mWorld(&world)
        , mActors(actors)
    {
        const RegionLoad arrived = loadRegion(world, cell, *mRoot, mScene, mExtractor, mLoaded, request.mWeather,
            request.mDay, request.mHour, actors.mProps);

        mLighting = arrived.mLighting;

        // **After the region, because an interior's sheet sits over whatever the room holds.** The
        // level it answers is what "how deep is this point" is asked against, so it goes into the
        // lighting the frame is described from.
        mWater.emplace(*mRoot);
        mLighting.mWaterLevel = mWater->follow(cell);

        // **The same particle systems the game builds, from the same component.** Under a node of
        // this harness's own rather than the sky's camera-relative transform, because there is no
        // sky manager here — what matters is that the box travels with the eye, and `driveWeather`
        // is what does that.
        // **Both of the weather's constants, read once and where the other one is.** The first is a
        // game setting and comes from the store; the second is a fallback and comes from the ini,
        // which is the only reason they are fetched differently.
        mStormWindSpeed = world.findGameSetting("fStromWindSpeed", 50.0f);
        mRainGravity = Fallback::Map::getFloat("Weather_Precip_Gravity");

        mWeatherNode = new osg::PositionAttitudeTransform;
        mWeatherNode->setName("Precipitation");
        mRoot->addChild(mWeatherNode);
        mPrecipitation
            = std::make_unique<Weather::Precipitation>(mWeatherNode, *world.getResourceSystem().getSceneManager(), ~0u);

        // **The moons' portraits, into the same table the trace reads.** Held rather than named by a
        // material: a moon is drawn by a ray that reached nothing, so no material can speak for its
        // texture and the sweep would take the slot on the first frame a cell died.
        mLighting.mFaces = Rtx::addMoonFaces(mScene);
        mLighting.mSky = Rtx::addSkyTextures(mScene, *world.getResourceSystem().getSceneManager());
        mReport = std::move(arrived.mReport);

        // **Before the first walk, because the walk runs the animators.** The graph's own
        // controllers — a brazier's flipbook, a lava flow — read the clock off the traversal, and a
        // shot is only repeatable if it is told which second it is showing rather than measuring
        // one of its own.
        setSeconds(actors.mSeconds);

        // **Without this the sea is shaded as ordinary geometry.** `isWater` answers no for every
        // mask until it is told which one names the water, and the game tells its own extractor the
        // same thing at `rtxrenderer.cpp:172`.
        mExtractor.setWaterMask(sWaterMask);

        mRegion = cell.mRegion;

        // Absent for an interior, and that is what `moveTo` reads as "this never streams".
        if (cell.isExterior())
            mStanding = CellSquare{ .mX = cell.getGridX(), .mY = cell.getGridY() };

        // **Before the first walk, because a paged world resolves its chunks during one.** The
        // camera below is placed from the scene's own bounds and the scene does not exist yet, so
        // the detail is anchored on where the run was told to stand — or, where it was told nothing,
        // on the middle of the cell it is centred on. Anchoring it on the origin instead put Seyda
        // Neen's ground seventy thousand units away from the eye that asked for it, and the coarsest
        // chunks in the tree are what came back.
        const float cellSize = static_cast<float>(ESM::getCellSize(ESM::Cell::sDefaultWorldspaceId));
        const osg::Vec3f middle((cell.getGridX() + 0.5f) * cellSize, (cell.getGridY() + 0.5f) * cellSize, 0.0f);
        mWorld->setTerrainViewPoint(request.mOrigin.value_or(middle));

        // The first walk. Everything after this is the same walk again, once a frame.
        mStaged = mirror(0);

        // **Before anyone goes in.** A row of actors stands relative to where the camera ends up, so
        // the camera cannot be derived from bounds that already contain them.
        mPlacement = placeCamera(mScene.getBounds(), request.mFieldOfView, request.mOrigin, request.mTarget);

        // **After the camera is placed and before the first walk.** The box is finite and centred on
        // the eye, so a shot whose weather arrived while the box still sat at the origin is a shot
        // of a rainstorm happening somewhere else.
        setFalling(request.mWeather);
        driveWeather(mPlacement.mOrigin);

        const std::span<const CellPerson> residents
            = actors.mResidents ? std::span<const CellPerson>(mReport.mPeople) : std::span<const CellPerson>();

        const std::span<const CellProp> props
            = actors.mProps ? std::span<const CellProp>(mReport.mProps) : std::span<const CellProp>();

        if (actors.empty() && residents.empty() && props.empty())
            return;

        mPosed = std::make_unique<PosedActors>(world, mScene, mExtractor, *mRoot, actors);
        mPosed->addResidents(residents);
        mPosed->addProps(props);
        mPosed->addRow(actors, mPlacement);
        mSettled = mPosed->settle();
    }

    StagedWorld::~StagedWorld() = default;

    Rtx::ExtractionStats StagedWorld::mirror(std::size_t frame)
    {
        // **Emptied before it is filled, which is what `RtxRenderer::renderFrame` does too.** The
        // lists a walk refills — the lights, the sprites, the emitters, the deforming set — are
        // appended to rather than replaced, so a second walk without this counted every light in the
        // region twice. It did not show while the lights were read out of records and placed once;
        // it showed the moment they became `LightSource` nodes the walk meets.
        mScene.clearPlacement();

        // **The residency is asked inside the same walk**, so the chunks a paged world keeps out of
        // the graph are dated, counted and swept with everything the graph does hold. Null where
        // nothing pages, which is every run that did not ask for it.
        return mExtractor.extract(*mRoot, osg::Matrixf::identity(), 0, frame, mWorld->getTerrainResidency());
    }

    void StagedWorld::driveWeather(const osg::Vec3f& eye)
    {
        mWeatherNode->setPosition(eye);

        mPrecipitation->update(Weather::Conditions{
            .mEye = eye,

            // **Aimed at the camera, because that is the body standing in this weather.** The game
            // aims an ash storm at the player off Red Mountain; the rule is the same one and the
            // observer is whoever is looking.
            .mStormDirection = Weather::stormDirection(mStormEffect, eye),

            // The same question the game asks of the water it owns, asked here of the level this
            // cell reported. A cell with no water reports minus infinity, so nothing is ever under
            // it.
            .mUnderwater = eye.z() < mLighting.mWaterLevel,
        });
    }

    void StagedWorld::setFalling(std::string_view weather)
    {
        const Weather::Downpour falling = Weather::downpourAt(weather, mStormWindSpeed, mRainGravity);
        mStormEffect = falling.mParticleEffect;
        mPrecipitation->setWeather(falling);
    }

    Crossing StagedWorld::moveTo(const osg::Vec3f& where)
    {
        driveWeather(where);

        // **Every frame and not only on a crossing**, because the detail a paged world builds at is
        // a distance from the eye rather than a property of the cell: a camera flying across one
        // cell changes what the chunks under it should be without changing which cell it is in.
        mWorld->setTerrainViewPoint(where);

        if (!mStanding.has_value())
            return {};

        // **Two integers compared, and nothing spelled out.** This runs every frame of a streaming
        // run and answers no on all but a handful of them; naming the square to find that out would
        // be two allocations a frame for the privilege.
        const CellSquare square = squareAt(where);
        if (square == *mStanding)
            return {};

        mStanding = square;

        // **Open sea, and the answer is to keep what is already loaded.** Every point has a square;
        // not every square has a cell record, and a camera over the water is standing in one of the
        // ones that does not. The game holds its last grid there too.
        const ESM::Cell* cell = mWorld->findCell(cellAt(square));
        if (cell == nullptr)
            return {};

        mRegion = cell->mRegion;

        // **The sheet moves with the camera's cell, exactly as `Water::changeCell` moves the
        // game's.** It is finite — a hundred and fifty cells across — so a camera that flew far
        // enough from where it started would otherwise reach the edge of the sea.
        mLighting.mWaterLevel = mWater->follow(*cell);

        // **The actors come out first.** The new cells are walked into whatever the scene holds, so
        // a snapshot retaken with everyone still in it would place a second copy of them on the very
        // next frame.
        if (mPosed != nullptr)
            mPosed->unplace();

        const CellReport arrived = readRegion(*mWorld, *cell, *mRoot, mScene, mExtractor, mLoaded, mActors.mProps);

        // **The ring that arrived and the ones that left.** The working set is a square that follows
        // the camera, not everything ever visited; without the second half this grows for as long as
        // the run lasts and stops resembling the game after the first crossing.
        const Crossing crossed{ .mArrived = arrived.mCells,
            .mDeparted = dropCellsOutside(*mWorld, *cell, *mRoot, mScene, mExtractor, mLoaded) };

        // Built, then walked, which is the split the game has too. The walk is also what tells the
        // sweep below that the departed cells are no longer met.
        mirror(0);
        mExtractor.advance();

        if (crossed.mDeparted > 0)
            mExtractor.retire();

        // **A second walk, because the sweep emptied what the first one filled.**
        // `SceneDesc::release` clears the sprites, the emitters and the light table on the frames a
        // cell dies, on the understanding that the walk which comes next refills them — true of the
        // game, which walks every frame, and false here, where a walk happens only when the ring
        // moves. Bringing that next walk forward is what the understanding actually asks for.
        if (crossed.mDeparted > 0)
        {
            mirror(0);
            mExtractor.advance();
        }

        if (mPosed == nullptr)
            return crossed;

        // The people who arrived with the ring. A resident belongs to the half of the scene that is
        // walked in again per frame, which is why they go in after the cells rather than with them.
        if (mActors.mResidents)
            mPosed->addResidents(arrived.mPeople);
        if (mActors.mProps)
            mPosed->addProps(arrived.mProps);

        mSettled = mPosed->settle();
        return crossed;
    }

    bool StagedWorld::advanceTo(float seconds)
    {
        const float elapsed = seconds - mSeconds;
        setSeconds(seconds);
        if (mPosed != nullptr && mPosed->advanceTo(seconds))
            return true;

        // **Nothing moved, and the walk happens anyway.** Actors already walk when they step, so
        // this is only ever the still world — which is exactly the case a snapshot was hiding.
        //
        // The emitters are carried by that step where there is one, so this is the only path that
        // owes them the gap itself.
        mExtractor.advanceEmitters(static_cast<double>(elapsed));
        mirror(0);
        mExtractor.advance();
        return true;
    }

    Motion* StagedWorld::getMotion()
    {
        // **The actors' own stepping first**, because it already walks the whole graph when it runs
        // and asking for both would walk it twice. Where there are none, the walk is still the
        // game's — a still world is walked every frame here exactly as it is there.
        return mPosed != nullptr ? static_cast<Motion*>(mPosed.get()) : &mEveryFrame;
    }

    bool StagedWorld::EveryFrame::step(std::uint32_t frame)
    {
        mStaged.mirror(frame);
        mStaged.mExtractor.advance();

        // **Always true, because the frame after a walk has to be handed over.** A walk that found
        // everything where it was still emptied and refilled the per-frame lists, and the backend's
        // copy of those is what a hand-over rewrites.
        return true;
    }

    void StagedWorld::setSeconds(float seconds)
    {
        mSeconds = seconds;
        mExtractor.setSimulationTime(mSeconds);
    }

    std::size_t StagedWorld::getActorCount() const
    {
        return mPosed == nullptr ? 0 : mPosed->getCount() - mPosed->getPropCount();
    }

    std::size_t StagedWorld::getPropCount() const
    {
        return mPosed == nullptr ? 0 : mPosed->getPropCount();
    }
}
