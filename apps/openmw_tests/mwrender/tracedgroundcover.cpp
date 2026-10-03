#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec2i>
#include <osg/Vec3f>

#include <apps/openmw/mwrender/rtx/tracedgroundcover.hpp>
#include <apps/openmw/mwworld/groundcoverstore.hpp>
#include <apps/openmw/mwworld/store.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/files/collections.hpp>
#include <components/files/multidircollection.hpp>
#include <components/terrain/pagedcellref.hpp>
#include <components/testing/util.hpp>

namespace MWRender
{
    namespace
    {
        struct Plant
        {
            std::uint32_t mIndex = 0;
            bool mDeleted = false;
            osg::Vec3f mPosition{};
            osg::Vec3f mRotation{};
            float mScale = 1.0f;
        };

        ESM::CellRef makeRef(const Plant& plant)
        {
            ESM::CellRef ref;
            ref.blank();
            ref.mRefNum.mIndex = plant.mIndex;
            ref.mRefNum.mContentFile = 0;
            ref.mRefID = ESM::RefId::stringRefId("fern");
            for (int axis = 0; axis < 3; ++axis)
            {
                ref.mPos.pos[axis] = plant.mPosition[axis];
                ref.mPos.rot[axis] = plant.mRotation[axis];
            }
            ref.mScale = plant.mScale;
            return ref;
        }

        void saveCell(ESM::ESMWriter& writer, const ESM::Cell& cell, std::span<const Plant> plants)
        {
            writer.startRecord(ESM::REC_CELL);
            cell.save(writer);
            for (const Plant& plant : plants)
                makeRef(plant).save(writer, false, false, plant.mDeleted);
            writer.endRecord(ESM::REC_CELL);
        }

        ESM::Cell exteriorAt(int x, int y)
        {
            ESM::Cell cell;
            cell.blank();
            cell.mData.mX = x;
            cell.mData.mY = y;
            return cell;
        }

        /// A groundcover file of two records and three cells, read through the world's store.
        ///
        /// Cell (2, 3) lists its plants against their numbers, 6 down to 1, with 3 deleted and
        /// 2 placed, turned and scaled; cell (-1, 0) lists 7 to 10; and a room lists one, which the
        /// store leaves out because no grass grows indoors. `fern` names a model under `grass/`,
        /// `rock` one outside it, which the store keeps no model for.
        class TracedGroundcoverTest : public ::testing::Test
        {
        protected:
            TracedGroundcoverTest()
            {
                const std::filesystem::path directory = TestingOpenMW::currentTestDirPath();
                {
                    ESM::Static fern;
                    fern.blank();
                    fern.mId = ESM::RefId::stringRefId("fern");
                    fern.mModel = "grass/fern.nif";
                    ESM::Static rock;
                    rock.blank();
                    rock.mId = ESM::RefId::stringRefId("rock");
                    rock.mModel = "rock.nif";

                    ESM::Cell room;
                    room.blank();
                    room.mName = "room";
                    room.mData.mFlags = ESM::Cell::Interior;

                    std::ofstream stream(directory / "grass.esp", std::ios::binary);
                    ESM::ESMWriter writer;
                    writer.setFormatVersion(ESM::CurrentContentFormatVersion);
                    writer.save(stream);
                    writer.startRecord(ESM::REC_STAT);
                    fern.save(writer);
                    writer.endRecord(ESM::REC_STAT);
                    writer.startRecord(ESM::REC_STAT);
                    rock.save(writer);
                    writer.endRecord(ESM::REC_STAT);
                    saveCell(writer, exteriorAt(2, 3),
                        std::vector<Plant>{
                            { .mIndex = 6 },
                            { .mIndex = 5 },
                            { .mIndex = 4 },
                            { .mIndex = 3, .mDeleted = true },
                            { .mIndex = 2,
                                .mPosition = osg::Vec3f(100.0f, 200.0f, 300.0f),
                                .mRotation = osg::Vec3f(0.25f, 0.5f, 0.75f),
                                .mScale = 2.0f },
                            { .mIndex = 1 },
                        });
                    saveCell(writer, exteriorAt(-1, 0),
                        std::vector<Plant>{ { .mIndex = 7 }, { .mIndex = 8 }, { .mIndex = 9 }, { .mIndex = 10 } });
                    saveCell(writer, room, std::vector<Plant>{ { .mIndex = 11 } });
                    writer.close();
                }

                mStore.init(
                    mStatics, Files::Collections(Files::PathContainer{ directory }), { "grass.esp" }, nullptr, nullptr);
            }

            /// The numbers of the references `density` keeps in `cell`, in the order handed on.
            std::vector<std::uint32_t> kept(float density, const osg::Vec2i& cell)
            {
                TracedGroundcover groundcover(mStore, density);
                std::vector<Terrain::PagedCellRef> refs;
                groundcover.collect(cell, refs);

                std::vector<std::uint32_t> numbers;
                for (const Terrain::PagedCellRef& ref : refs)
                    numbers.push_back(ref.mRefNum.mIndex);
                return numbers;
            }

            MWWorld::Store<ESM::Static> mStatics;
            MWWorld::GroundcoverStore mStore;
        };

        /// **The density keeps the rasterizer's plants**: a turn for each reference the files
        /// still keep, in the order the file lists them, a plant kept on each turn that takes the
        /// running sum to a whole, and none for a deleted one. At a half, (2, 3)'s turns fall on
        /// 6, 5, 4, 2 and 1, and the second and the fourth stand: 5 and 2, handed on by their
        /// numbers. A turn for the deleted 3 would have kept 5 and 1.
        ///
        /// **Every cell counts from nought**: (-1, 0) after (2, 3) keeps 8 and 10 at a half, where
        /// the half (2, 3) left over would have kept 7 and 9.
        TEST_F(TracedGroundcoverTest, theDensityKeepsTheRasterizersPlantsCellByCell)
        {
            EXPECT_EQ(kept(1.0f, osg::Vec2i(2, 3)), (std::vector<std::uint32_t>{ 1, 2, 4, 5, 6 }))
                << "every plant but the deleted one, by number";
            EXPECT_EQ(kept(0.5f, osg::Vec2i(2, 3)), (std::vector<std::uint32_t>{ 2, 5 }));
            EXPECT_EQ(kept(0.0f, osg::Vec2i(2, 3)), std::vector<std::uint32_t>{});

            TracedGroundcover groundcover(mStore, 0.5f);
            std::vector<Terrain::PagedCellRef> refs;
            groundcover.collect(osg::Vec2i(2, 3), refs);
            refs.clear();
            groundcover.collect(osg::Vec2i(-1, 0), refs);
            ASSERT_EQ(refs.size(), 2u);
            EXPECT_EQ(refs[0].mRefNum.mIndex, 8u);
            EXPECT_EQ(refs[1].mRefNum.mIndex, 10u);

            EXPECT_EQ(kept(1.0f, osg::Vec2i(0, 0)), std::vector<std::uint32_t>{}) << "a cell no file lists";
        }

        /// A plant is handed on where the file placed it, and its record names the model the store
        /// corrected under `meshes/`; a record outside `grass/` names none.
        TEST_F(TracedGroundcoverTest, aPlantKeepsItsPlacementAndItsRecordItsModel)
        {
            TracedGroundcover groundcover(mStore, 1.0f);
            std::vector<Terrain::PagedCellRef> refs;
            groundcover.collect(osg::Vec2i(2, 3), refs);
            ASSERT_EQ(refs.size(), 5u);

            const Terrain::PagedCellRef& placed = refs[1];
            EXPECT_EQ(placed.mRefNum.mIndex, 2u);
            EXPECT_EQ(placed.mRefId, ESM::RefId::stringRefId("fern"));
            EXPECT_EQ(placed.mPosition, osg::Vec3f(100.0f, 200.0f, 300.0f));
            EXPECT_EQ(placed.mRotation, osg::Vec3f(0.25f, 0.5f, 0.75f));
            EXPECT_EQ(placed.mScale, 2.0f);
            EXPECT_EQ(placed.mGate, Terrain::sNoGate);

            EXPECT_EQ(groundcover.modelOf(ESM::RefId::stringRefId("fern")).value(), "meshes/grass/fern.nif");
            EXPECT_TRUE(groundcover.modelOf(ESM::RefId::stringRefId("rock")).empty());
            EXPECT_TRUE(groundcover.modelOf(ESM::RefId::stringRefId("missing")).empty());
        }
    }
}
