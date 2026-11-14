#include "worldmap.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <osg/Vec2i>
#include <osg/Vec3f>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/store.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/position.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/landrecorddata.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm3/refnum.hpp>
#include <components/misc/strings/lower.hpp>

namespace RtxTool
{
    namespace
    {
        /// How far over the feet `coc` puts a player at the first-person eye stands: the lift the camera driver's
        /// first correction measured at the pier (`CameraDriver::settleBody`).
        constexpr float sEyeOverFeet = 162.0f;

        /// The highest the ground of `land` stands, its heights read through `readers` into `data`, or nothing
        /// where the record holds no heights.
        std::optional<float> highestOf(const ESM::Land& land, ESM::ReadersCache& readers, ESM::Land::LandData& data)
        {
            if ((land.mDataTypes & ESM::Land::DATA_VHGT) == 0)
                return std::nullopt;

            // A record the game made rather than read keeps its data, which nothing reads a file for.
            if (land.mContext.filename.empty())
            {
                const ESM::Land::LandData* held = land.getLandData();
                if (held == nullptr || (held->mDataLoaded & ESM::Land::DATA_VHGT) == 0)
                    return std::nullopt;
                return held->mMaxHeight;
            }

            data.mDataLoaded = 0;
            const ESM::ReadersCache::BusyItem reader = readers.get(static_cast<std::size_t>(land.mContext.index));
            reader->restoreContext(land.mContext);
            ESM::loadLandRecordData(ESM::Land::DATA_VHGT, *reader, data);
            return data.mMaxHeight;
        }

        /// How many references `cell` holds once every file that names it is read, a later file's deletion taking
        /// one away; and the interior each door among them leads into, into `entered`, lower case.
        std::uint32_t referencesIn(const ESM::Cell& cell, ESM::ReadersCache& readers, std::set<ESM::RefNum>& held,
            std::unordered_set<std::string>& entered)
        {
            held.clear();
            for (std::size_t at = 0; at < cell.mContextList.size(); ++at)
            {
                const ESM::ReadersCache::BusyItem reader
                    = readers.get(static_cast<std::size_t>(cell.mContextList[at].index));
                cell.restore(*reader, at);
                ESM::CellRef ref;
                bool deleted = false;
                while (ESM::Cell::getNextRef(*reader, ref, deleted))
                {
                    if (deleted)
                    {
                        held.erase(ref.mRefNum);
                        continue;
                    }
                    held.insert(ref.mRefNum);
                    if (ref.mTeleport && !ref.mDestCell.empty())
                        entered.insert(Misc::StringUtils::lowerCase(ref.mDestCell));
                }
            }
            return static_cast<std::uint32_t>(held.size());
        }
    }

    WorldMap readWorldMap()
    {
        MWBase::World& world = *MWBase::Environment::get().getWorld();
        const MWWorld::ESMStore& store = world.getStore();

        WorldMap map;
        map.mFiles = world.getContentFiles();

        ESM::ReadersCache readers;
        ESM::Land::LandData data;
        for (const ESM::Land& land : store.get<ESM::Land>())
        {
            if (const std::optional<float> highest = highestOf(land, readers, data))
                map.mLand.push_back(LandCell{ .mCell = osg::Vec2i(land.mX, land.mY), .mHighest = *highest });
        }

        // **Only a room a door leads into**, from outside or from another room: a player walks into no other, and a
        // content's test cells, which nothing leads to, have no lamps to light them by design.
        std::set<ESM::RefNum> held;
        std::unordered_set<std::string> entered;
        const MWWorld::Store<ESM::Cell>& cells = store.get<ESM::Cell>();
        for (auto cell = cells.extBegin(); cell != cells.extEnd(); ++cell)
            referencesIn(*cell, readers, held, entered);
        // Every room walked before any is kept, since a door into one may stand in a room read after it; and a place
        // to stand asked of the kept alone.
        std::vector<Room> rooms;
        for (auto cell = cells.intBegin(); cell != cells.intEnd(); ++cell)
        {
            if (cell->mContextList.empty())
                continue;
            rooms.push_back(Room{ .mName = cell->mName,
                .mFile = static_cast<std::uint32_t>(cell->mContextList.front().index),
                .mReferences = referencesIn(*cell, readers, held, entered),
                .mEye = {} });
        }
        for (Room& room : rooms)
        {
            if (!entered.contains(Misc::StringUtils::lowerCase(room.mName)))
            {
                ++map.mRoomsNoDoorEnters;
                continue;
            }

            // A room `coc` cannot stand a player in is no room a tour can show.
            ESM::Position where{};
            if (world.findInteriorPosition(room.mName, where).empty())
            {
                Log(Debug::Warning) << "Tour: no place to stand in \"" << room.mName << "\", left out";
                continue;
            }
            room.mEye = osg::Vec3f(where.pos[0], where.pos[1], where.pos[2] + sEyeOverFeet);
            map.mRooms.push_back(std::move(room));
        }

        return map;
    }
}
