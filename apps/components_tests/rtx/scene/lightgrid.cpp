#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>
#include <osg/Vec3ui>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/lightgrid.hpp>

namespace Rtx
{
    namespace
    {
        /// What one cell's run under `key` holds, read the way the shader reads it — the flat index
        /// is written out here rather than borrowed, so that a change to it has to be made twice and
        /// noticed once.
        std::vector<std::uint32_t> runIn(const LightGrid& grid, std::uint32_t cell, std::uint32_t key)
        {
            const Shaders::GpuLightCell& held = grid.getCells()[cell];
            const auto first = grid.getList().begin() + held.mFirst[key];

            return std::vector<std::uint32_t>(first, first + held.mCount[key]);
        }

        std::vector<std::uint32_t> lampsIn(const LightGrid& grid, std::uint32_t x, std::uint32_t y, std::uint32_t z)
        {
            return runIn(grid, (z * grid.getSize().y() + y) * grid.getSize().x() + x, 0);
        }

        /// How many lamps the runs hold between them, both keys.
        std::size_t entriesIn(const LightGrid& grid)
        {
            std::size_t entries = 0;
            for (const Shaders::GpuLightCell& held : grid.getCells())
                entries += held.mCount[0] + held.mCount[1];
            return entries;
        }

        Light lampAt(float x, float reach)
        {
            return Light{ .mPosition = osg::Vec3f(x, 0.0f, 0.0f), .mReach = reach };
        }

        /// A lamp is binned into every cell its reach touches, and into no others.
        ///
        /// **That is what makes the lookup complete.** A cell's list has to be every lamp that could
        /// light it, so the shader's own distance test refines the answer and never corrects it — a
        /// lamp binned only where it stands would go dark one cell away and leave a seam.
        ///
        /// Two lamps of reach 100 four thousand units apart put the grid's corner at -100 and its
        /// far edge at 4196, which is 16.8 cells of 256 and so seventeen of them. Each lamp spans
        /// 200 units at one end, and the fifteen cells between them hold nothing.
        TEST(RtxLightGridTest, aLampIsBinnedIntoEveryCellItsReachTouchesAndNoOthers)
        {
            const std::array lights{ lampAt(0.0f, 100.0f), lampAt(4096.0f, 100.0f) };
            const LightGrid grid(lights);

            EXPECT_EQ(grid.getOrigin(), osg::Vec3f(-100.0f, -100.0f, -100.0f));
            EXPECT_EQ(grid.getSize(), osg::Vec3ui(17u, 1u, 1u));
            EXPECT_FLOAT_EQ(grid.getInverseCell(), 1.0f / 256.0f);

            EXPECT_EQ(lampsIn(grid, 0, 0, 0), std::vector<std::uint32_t>{ 0u });
            for (std::uint32_t x = 1; x < 16; ++x)
                EXPECT_TRUE(lampsIn(grid, x, 0, 0).empty()) << "the air between them, at cell " << x;
            EXPECT_EQ(lampsIn(grid, 16, 0, 0), std::vector<std::uint32_t>{ 1u });

            // A row a cell, and each run in a block of room for one lamp more than it holds: two
            // runs of one, at the list's start, and nothing for the empty ones. Past the blocks, room
            // for each to move to one twice its size and for every cell to take a lamp it had none
            // of: 2 * 4 + 2 * 17 words.
            const std::span<const Shaders::GpuLightCell> cells = grid.getCells();
            ASSERT_EQ(cells.size(), 17u);
            for (std::uint32_t cell = 0; cell < 17; ++cell)
            {
                EXPECT_EQ(cells[cell].mCount[0], cell == 0 || cell == 16 ? 1u : 0u)
                    << "the lamps lighting cell " << cell;
                EXPECT_EQ(cells[cell].mCount[1], 0u) << "the lamps taking light from cell " << cell;
            }
            EXPECT_EQ(cells[0].mFirst[0], 0u);
            EXPECT_EQ(cells[16].mFirst[0], 2u);
            EXPECT_EQ(grid.getList().size(), 2u * 4u + 2u * 17u);
        }

        /// Every lamp that reaches a cell is in it, in the order they were given.
        TEST(RtxLightGridTest, aCellHoldsEveryLampThatReachesIt)
        {
            // Reaches of 60 about 0 and 100 span -60 to 160, which is inside one cell of 256.
            std::array lights{ lampAt(0.0f, 60.0f), lampAt(50.0f, 60.0f), lampAt(100.0f, 60.0f) };
            LightGrid grid(lights);

            ASSERT_EQ(grid.getSize(), osg::Vec3ui(1u, 1u, 1u)) << "one cell holds every reach";
            EXPECT_EQ(lampsIn(grid, 0, 0, 0), (std::vector<std::uint32_t>{ 0u, 1u, 2u }));

            // **A lamp that takes light away is a run of its own**, beside its cell's, so no walk of
            // the lamps meets it — and one that turns into one leaves the one run and enters the
            // other where it stands.
            lights[1].mIntensity = osg::Vec3f(-1.0f, -1.0f, -1.0f);
            grid.rebuild(lights);
            EXPECT_FALSE(grid.wasMadeAgain());
            EXPECT_EQ(
                std::vector<Index>(grid.getRewritten().begin(), grid.getRewritten().end()), std::vector<Index>{ 0u });
            EXPECT_EQ(lampsIn(grid, 0, 0, 0), (std::vector<std::uint32_t>{ 0u, 2u }));
            EXPECT_EQ(runIn(grid, 0, 1), std::vector<std::uint32_t>{ 1u });
        }

        /// An empty scene is a grid nothing can be found in, and asking is still legal.
        TEST(RtxLightGridTest, noLampsIsOneEmptyCell)
        {
            // Spelled out because `{}` would also name the unfilled grid, which is a different
            // thing: this is the one lamps were binned into and there were none.
            const LightGrid grid{ std::span<const Light>{} };

            EXPECT_EQ(grid.getSize(), osg::Vec3ui(1u, 1u, 1u));

            // The one cell's runs, both empty and with no block; room in the list for the one cell
            // to take a lamp.
            ASSERT_EQ(grid.getCells().size(), 1u);
            EXPECT_EQ(grid.getCells()[0].mCount[0], 0u);
            EXPECT_EQ(grid.getCells()[0].mCount[1], 0u);
            EXPECT_EQ(grid.getList().size(), 2u);
        }

        /// The cell doubles until the grid fits, and there are two budgets to fit — or until the grid
        /// is one cell, past which doubling drops no entry.
        ///
        /// **The second is not implied by the first.** Lamps spread across a world overrun the cell
        /// count while each of them is ordinary; a handful with enormous reaches overrun the entry
        /// count while the grid is still small, because each lands in every cell it touches.
        ///
        /// **And every lamp a builder hands over ends it.** Its numbers are finite and nothing
        /// else: a record can place one near the end of the float range, where the extent in float
        /// is past the largest float, and a cell can hold more lamps than the entry budget. The
        /// first cast an infinite count and the second doubled the cell for ever.
        TEST(RtxLightGridTest, theCellDoublesUntilBothBudgetsFitOrTheGridIsOneCell)
        {
            // Seventy million units apart is 273,438 cells of 256 along x, 68,360 of 1024 and 34,180
            // of 2048, so the cell count alone forces three doublings.
            const std::array wideLights{ lampAt(0.0f, 1.0f), lampAt(70.0e6f, 1.0f) };
            const LightGrid wide(wideLights);

            EXPECT_FLOAT_EQ(wide.getInverseCell(), 1.0f / 2048.0f) << "the cell count alone";
            EXPECT_EQ(wide.getSize().x(), 34180u);

            // And five lamps sharing one reach of 20,480 units, which is the case only the entry
            // budget catches. **The grid is a volume, so the cell budget is reached far sooner than
            // a plane would suggest** — the cell doubles to 1024 before 40 cells an axis is 64,000
            // of them, just inside the 65,536 allowed. Five lamps each covering all of that is
            // 320,000 entries against 262,144, so it doubles once more to 20 an axis: 8,000 cells
            // and 40,000 entries.
            std::array<Light, 5> greedy{};
            for (Light& light : greedy)
                light.mReach = 20480.0f;

            const LightGrid crowded(greedy);
            EXPECT_FLOAT_EQ(crowded.getInverseCell(), 1.0f / 2048.0f) << "the entry count, at a legal cell count";
            EXPECT_EQ(crowded.getSize(), osg::Vec3ui(20u, 20u, 20u));
            EXPECT_EQ(entriesIn(crowded), 5u * 20u * 20u * 20u);

            // Two lamps at x = -2^127 and 2^127, each reaching 2^123: every number a float, and the
            // extent `2^128 + 2^124 = 17 * 2^124` along x is not, while y and z are 2^124. A cell of
            // `2^(116 - m) * 256` is `17 * 2^m` by `2^m` by `2^m` cells, so the first inside 65,536
            // is m = 3: 136 by 8 by 8, of side 2^121. The corner is (-17, -1, -1) * 2^123, so the
            // near lamp spans x cells [0, 9) — its reach ends on the edge of the ninth, which a box
            // counts — and the far one [128, 136), clamped at the grid's end, the eight rows of y
            // and z each: 576 and 512 entries.
            const float edge = std::ldexp(1.0f, 127);
            const float far = std::ldexp(1.0f, 123);
            const std::array farLights{ lampAt(-edge, far), lampAt(edge, far) };
            const LightGrid ranged(farLights);

            EXPECT_EQ(ranged.getOrigin(), osg::Vec3f(-17.0f * far, -far, -far));
            EXPECT_EQ(ranged.getSize(), osg::Vec3ui(136u, 8u, 8u));
            EXPECT_EQ(ranged.getInverseCell(), std::ldexp(1.0f, -121));
            EXPECT_EQ(entriesIn(ranged), 576u + 512u);
            EXPECT_EQ(lampsIn(ranged, 0, 0, 0), std::vector<std::uint32_t>{ 0u });
            EXPECT_EQ(lampsIn(ranged, 135, 7, 7), std::vector<std::uint32_t>{ 1u });
            EXPECT_TRUE(lampsIn(ranged, 64, 4, 4).empty()) << "the air between them";

            // One lamp past the entry budget, every one of them in the first cell of 256: one cell
            // holding them all, which the shader walks as it walks any other.
            const std::vector<Light> packed(262144 + 1, lampAt(0.0f, 1.0f));
            const LightGrid full(packed);

            EXPECT_EQ(full.getSize(), osg::Vec3ui(1u, 1u, 1u));
            EXPECT_FLOAT_EQ(full.getInverseCell(), 1.0f / 256.0f) << "it stopped at the first cell and not after";
            EXPECT_EQ(entriesIn(full), packed.size());
        }

        /// Three reaches at once: one lamp inside a corner of the grid, one against its far edge and
        /// one that covers the whole of it.
        ///
        /// **The three the binning has to get right at the same time.** `boxAround` runs once for
        /// the sizing, once for the count and once for the put, so a box the three do not agree on
        /// is a lamp in the wrong cells — and the case that would show it is a lamp whose box is
        /// clamped at one end and not the other.
        ///
        /// The grid, by hand. A reaches x[-512, 512] and B x[3584, 4608], both y and z [-512, 512];
        /// C reaches x[-512, 4608] and y and z [-2560, 2560]. So the corner is (-512, -2560, -2560)
        /// and the far edge is (4608, 2560, 2560): an extent of 5120 on every axis, which is 20 cells
        /// of 256 each way. Eight thousand cells and 8,225 entries, both inside the first cell
        /// size's budgets, so nothing doubles.
        TEST(RtxLightGridTest, lampsSpanningOneCellSeveralAndTheWholeGridAreEachBinnedRight)
        {
            const std::array lights{ lampAt(0.0f, 512.0f), lampAt(4096.0f, 512.0f), lampAt(2048.0f, 2560.0f) };
            const LightGrid grid(lights);

            ASSERT_EQ(grid.getOrigin(), osg::Vec3f(-512.0f, -2560.0f, -2560.0f));
            ASSERT_EQ(grid.getSize(), osg::Vec3ui(20u, 20u, 20u));
            EXPECT_FLOAT_EQ(grid.getInverseCell(), 1.0f / 256.0f);

            // A spans x cells [0, 5) and y and z cells [8, 13), which is 125 cells. B spans x cells
            // [16, 20) — its box reaches 21 and the grid is twenty across, so the clamp is what keeps
            // it inside — and the same five in y and z, which is 100. C covers all eight thousand.
            EXPECT_EQ(entriesIn(grid), 125u + 100u + 8000u);

            EXPECT_EQ(lampsIn(grid, 0, 8, 8), (std::vector<std::uint32_t>{ 0u, 2u }))
                << "the near lamp and the wide one";
            EXPECT_EQ(lampsIn(grid, 4, 12, 12), (std::vector<std::uint32_t>{ 0u, 2u })) << "the far corner of A's box";
            EXPECT_EQ(lampsIn(grid, 19, 10, 10), (std::vector<std::uint32_t>{ 1u, 2u })) << "the clamped edge cell";
            EXPECT_EQ(lampsIn(grid, 10, 10, 10), std::vector<std::uint32_t>{ 2u })
                << "the air between the two small ones";
            EXPECT_EQ(lampsIn(grid, 0, 0, 0), std::vector<std::uint32_t>{ 2u }) << "below A, which reaches only to y 8";
            EXPECT_EQ(lampsIn(grid, 19, 19, 19), std::vector<std::uint32_t>{ 2u });
        }

        /// A rebind of the same lamps goes nowhere near the allocator.
        ///
        /// **What a frame that moves does**, and the claim `rebuild` is written against: assigning a
        /// freshly built grid over this one threw the run list's vectors away and made them again,
        /// on every frame that moved.
        TEST(RtxLightGridTest, rebindingTheSameLampsDoesNotTouchTheHeap)
        {
            const std::array lights{ lampAt(0.0f, 512.0f), lampAt(4096.0f, 512.0f), lampAt(2048.0f, 2560.0f) };

            LightGrid grid;
            grid.rebuild(lights);
            grid.rebuild(lights);

            const std::size_t before = Testing::getAllocationCount();
            grid.rebuild(lights);
            const std::size_t spent = Testing::getAllocationCount() - before;

            EXPECT_EQ(spent, 0u) << spent << " allocations to bin the lamps a frame already held";
            EXPECT_EQ(entriesIn(grid), 125u + 100u + 8000u) << "and it binned them all the same";
        }

        /// A lamp that only flickered is not binned again, and one that moved is.
        ///
        /// **This is what the whole gate rests on**: the grid reads a light's position and its reach
        /// and nothing else, so a colour that changed leaves the same grid — and Morrowind's lamps
        /// change colour on nearly every frame, which is what made a per-frame rebind the ordinary
        /// case rather than the exception.
        ///
        /// The two are told apart by what the grid comes to: a lamp moved a whole cell lands in
        /// different cells, and the entry count says so.
        TEST(RtxLightGridTest, aLampThatOnlyFlickeredIsNotBinnedAgain)
        {
            std::array lights{ lampAt(0.0f, 512.0f), lampAt(4096.0f, 512.0f) };

            LightGrid grid;
            grid.rebuild(lights);

            const std::vector<std::uint32_t> first = lampsIn(grid, 0, 0, 0);

            // What a flicker writes, and nothing the grid ever reads.
            lights[0].mIntensity = osg::Vec3f(9.0f, 3.0f, 1.0f);
            lights[1].mSourceRadius = 7.0f;
            lights[1].mClearance = 2.0f;
            grid.rebuild(lights);

            EXPECT_EQ(lampsIn(grid, 0, 0, 0), first) << "a lamp that only changed colour moved the grid";
            EXPECT_FALSE(grid.wasMadeAgain());
            EXPECT_TRUE(grid.getRewritten().empty()) << "a flicker owed the device a record";

            // And a lamp that reaches further is a lamp the grid has to be made for again: 512 over
            // a cell of 256 spans five cells across, and 2048 spans seventeen.
            const std::size_t reachedTwo = entriesIn(grid);
            lights[0].mReach = 2048.0f;
            grid.rebuild(lights);

            EXPECT_TRUE(grid.wasMadeAgain());
            EXPECT_NE(entriesIn(grid), reachedTwo) << "a lamp that reaches further was not rebinned";
        }

        /// **A lamp that moved is binned again into the grid as it stands, and nothing else is.** A
        /// carried torch moved every lamp's binning from nothing on every frame. The grid is made
        /// again only where a reach leaves it, because a position outside the grid is lit by nothing.
        ///
        /// The grid of the first test, by hand: corner -100, seventeen cells of 256. The lamp moved
        /// to 50 spans -50 to 150, which is still cell 0: the list is the list it was. Moved to 300
        /// it spans 200 to 400, 300 to 500 from the corner, which is cell 1 alone, and the corner
        /// stays where it was, though a grid made from nothing would put it at 200. Moved to -500 it
        /// reaches out of the grid, which is made again with its corner at -600.
        TEST(RtxLightGridTest, aLampThatMovedIsBinnedAloneIntoTheGridAsItStands)
        {
            std::array lights{ lampAt(0.0f, 100.0f), lampAt(4096.0f, 100.0f) };
            LightGrid grid;
            grid.rebuild(lights);
            EXPECT_TRUE(grid.wasMadeAgain()) << "the first rebuild is a build";
            const std::vector<std::uint32_t> first(grid.getList().begin(), grid.getList().end());

            lights[0].mPosition.x() = 50.0f;
            std::size_t before = Testing::getAllocationCount();
            grid.rebuild(lights);
            EXPECT_EQ(Testing::getAllocationCount() - before, 0u);
            EXPECT_EQ(std::vector<std::uint32_t>(grid.getList().begin(), grid.getList().end()), first)
                << "a lamp that stayed in its cells changed the list";
            EXPECT_FALSE(grid.wasMadeAgain());
            EXPECT_TRUE(grid.getRewritten().empty());

            // The cell it left and the cell it entered, and no other record is owed.
            lights[0].mPosition.x() = 300.0f;
            before = Testing::getAllocationCount();
            grid.rebuild(lights);
            EXPECT_EQ(Testing::getAllocationCount() - before, 0u);
            EXPECT_FALSE(grid.wasMadeAgain());
            EXPECT_EQ(std::vector<Index>(grid.getRewritten().begin(), grid.getRewritten().end()),
                (std::vector<Index>{ 0u, 1u }));
            EXPECT_EQ(grid.getOrigin(), osg::Vec3f(-100.0f, -100.0f, -100.0f)) << "the grid was made again";
            EXPECT_EQ(grid.getSize(), osg::Vec3ui(17u, 1u, 1u));
            EXPECT_TRUE(lampsIn(grid, 0, 0, 0).empty());
            EXPECT_EQ(lampsIn(grid, 1, 0, 0), std::vector<std::uint32_t>{ 0u });
            EXPECT_EQ(lampsIn(grid, 16, 0, 0), std::vector<std::uint32_t>{ 1u });

            lights[0].mPosition.x() = -500.0f;
            grid.rebuild(lights);
            EXPECT_TRUE(grid.wasMadeAgain());
            EXPECT_EQ(grid.getOrigin(), osg::Vec3f(-600.0f, -100.0f, -100.0f)) << "a reach out of the grid kept it";
            EXPECT_EQ(lampsIn(grid, 0, 0, 0), std::vector<std::uint32_t>{ 0u });
        }

        /// **Every lamp that moved leaves before any enters**, so two lamps that trade places
        /// through a full run write their cells and nothing more; and a lamp entering a full run
        /// moves the run to a block twice its size at the list's end.
        ///
        /// By hand, with reaches of 100 and the corner at -100: x = 0, 10, 25 and 50 are cell 0,
        /// 1024 is cell 4, 2048 is cell 8 and 4096 cell 16, seventeen cells across. A run of one
        /// lamp has a block of two, so the four lie at 0, 2, 4 and 6, in cell order.
        TEST(RtxLightGridTest, lampsTradingPlacesThroughAFullRunLeaveBeforeTheyEnter)
        {
            std::array lights{ lampAt(2048.0f, 100.0f), lampAt(0.0f, 100.0f), lampAt(1024.0f, 100.0f),
                lampAt(4096.0f, 100.0f) };
            LightGrid grid(lights);
            ASSERT_EQ(grid.getSize(), osg::Vec3ui(17u, 1u, 1u));
            ASSERT_EQ(grid.getCells()[0].mFirst[0], 0u);

            // Lamp 2 joins lamp 1, which fills cell 0's block.
            lights[2].mPosition.x() = 50.0f;
            grid.rebuild(lights);
            ASSERT_FALSE(grid.wasMadeAgain());
            ASSERT_EQ(lampsIn(grid, 0, 0, 0), (std::vector<std::uint32_t>{ 1u, 2u }));
            ASSERT_EQ(grid.getCells()[0].mFirst[0], 0u);

            // Lamp 0 enters the full run as lamp 2 leaves it. Lamp 0 is walked first, so a lamp
            // that entered as it was walked found the run full and moved it.
            lights[0].mPosition.x() = 25.0f;
            lights[2].mPosition.x() = 2048.0f;
            grid.rebuild(lights);
            EXPECT_FALSE(grid.wasMadeAgain());
            std::vector<Index> rewritten(grid.getRewritten().begin(), grid.getRewritten().end());
            std::ranges::sort(rewritten);
            EXPECT_EQ(rewritten, (std::vector<Index>{ 0u, 8u }));
            EXPECT_EQ(grid.getCells()[0].mFirst[0], 0u) << "a lamp entered the full run before the other left it";
            EXPECT_EQ(lampsIn(grid, 0, 0, 0), (std::vector<std::uint32_t>{ 0u, 1u }));
            EXPECT_EQ(lampsIn(grid, 8, 0, 0), std::vector<std::uint32_t>{ 2u });

            // And lamp 2 back into the full run moves it past the four blocks, to 8, with room for
            // four.
            lights[2].mPosition.x() = 10.0f;
            grid.rebuild(lights);
            EXPECT_FALSE(grid.wasMadeAgain());
            EXPECT_EQ(grid.getCells()[0].mFirst[0], 8u);
            EXPECT_EQ(lampsIn(grid, 0, 0, 0), (std::vector<std::uint32_t>{ 0u, 1u, 2u }));
        }

        /// **A list with no room left for a run makes the grid again**, packed.
        ///
        /// By hand: lamps of reach 100 at x = 0 and 4096 hold seventeen cells with the corner at
        /// -100, and a third at 1024 + 256k stands in cell 4 + k alone. Three blocks of two is six
        /// words, and the list is 2 * 6 + 2 * 17 = 46. Every cell the third lamp enters for the
        /// first time takes a block of two from the end, under one key and then under the other:
        /// cells 5 to 15 lit take 22 and bring the end to 28, turned to take light away in cell 15
        /// it takes 2 more, and walked back, cells 14 to 7 take 16 and bring the end to 46. Cell 6
        /// has no room, and the grid is made again.
        TEST(RtxLightGridTest, aListWithNoRoomLeftMakesTheGridAgain)
        {
            std::array lights{ lampAt(0.0f, 100.0f), lampAt(4096.0f, 100.0f), lampAt(1024.0f, 100.0f) };
            LightGrid grid(lights);
            ASSERT_EQ(grid.getList().size(), 46u);

            const auto carry = [&](std::uint32_t cell) {
                lights[2].mPosition.x() = 1024.0f + 256.0f * static_cast<float>(cell - 4);
                grid.rebuild(lights);
                return !grid.wasMadeAgain();
            };
            for (std::uint32_t cell = 5; cell <= 15; ++cell)
                ASSERT_TRUE(carry(cell)) << "lit, into cell " << cell;

            lights[2].mIntensity = osg::Vec3f(-1.0f, -1.0f, -1.0f);
            grid.rebuild(lights);
            ASSERT_FALSE(grid.wasMadeAgain()) << "turned in cell 15";
            EXPECT_EQ(runIn(grid, 15, 1), std::vector<std::uint32_t>{ 2u });

            for (std::uint32_t cell = 14; cell >= 7; --cell)
                ASSERT_TRUE(carry(cell)) << "taking light, into cell " << cell;
            EXPECT_FALSE(carry(6)) << "cell 6 found room in a list that has none";

            EXPECT_EQ(grid.getList().size(), 2u * 6u + 2u * 17u) << "packed again, the turned lamp's block among them";
            EXPECT_EQ(runIn(grid, 6, 1), std::vector<std::uint32_t>{ 2u });
        }

        /// **What a rebuild leaves is what a build from nothing makes**, run by run, over a walk of
        /// lamps that move, trade places and turn to take light away; and every record that changed
        /// is one the rebuild says it wrote, which is the whole of what the device's copies are told.
        ///
        /// Two lamps fixed at opposite corners hold the extent, and every other lamp moves inside
        /// it, so a build from nothing has the same cells to compare against.
        TEST(RtxLightGridTest, aRebuildMakesTheRunsABuildFromNothingWould)
        {
            std::uint32_t state = 12345u;
            const auto next = [&](float low, float high) {
                state = state * 1664525u + 1013904223u;
                return low + (high - low) * static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
            };

            std::vector<Light> lights{ Light{ .mPosition = osg::Vec3f(0.0f, 0.0f, 0.0f), .mReach = 500.0f },
                Light{ .mPosition = osg::Vec3f(4096.0f, 4096.0f, 1024.0f), .mReach = 500.0f } };
            const auto place = [&](Light& light) {
                light.mPosition = osg::Vec3f(next(500.0f, 3596.0f), next(500.0f, 3596.0f), next(400.0f, 624.0f));
                light.mReach = next(50.0f, 400.0f);
            };
            for (int lamp = 0; lamp < 30; ++lamp)
                place(lights.emplace_back());

            const auto runsOf = [](const LightGrid& grid) {
                std::vector<std::vector<std::uint32_t>> runs;
                const std::uint32_t cells = grid.getSize().x() * grid.getSize().y() * grid.getSize().z();
                for (std::uint32_t cell = 0; cell < cells; ++cell)
                    for (std::uint32_t key = 0; key < 2; ++key)
                        runs.push_back(runIn(grid, cell, key));
                return runs;
            };

            LightGrid grid(lights);
            std::vector<std::vector<std::uint32_t>> had = runsOf(grid);
            std::size_t madeAgain = 0;
            std::size_t traded = 0;
            for (int step = 0; step < 300; ++step)
            {
                for (int moves = 0; moves < 4; ++moves)
                {
                    Light& light = lights[2 + static_cast<std::size_t>(next(0.0f, 30.0f))];
                    const float roll = next(0.0f, 1.0f);
                    if (roll < 0.1f)
                        light.mIntensity = -light.mIntensity - osg::Vec3f(0.0f, 0.0f, 1.0f);
                    else if (roll < 0.2f)
                    {
                        std::swap(light, lights[2 + static_cast<std::size_t>(next(0.0f, 30.0f))]);
                        ++traded;
                    }
                    else
                        place(light);
                }

                grid.rebuild(lights);
                const LightGrid fresh(lights);
                ASSERT_EQ(grid.getOrigin(), fresh.getOrigin()) << "the corners stopped holding the extent";
                ASSERT_EQ(grid.getSize(), fresh.getSize());

                const std::vector<std::vector<std::uint32_t>> runs = runsOf(grid);
                ASSERT_EQ(runs, runsOf(fresh)) << "at step " << step;

                if (grid.wasMadeAgain())
                    ++madeAgain;
                else
                {
                    for (std::size_t at = 0; at < runs.size(); ++at)
                    {
                        if (runs[at] != had[at])
                        {
                            EXPECT_NE(std::ranges::find(grid.getRewritten(), static_cast<Index>(at / 2)),
                                grid.getRewritten().end())
                                << "cell " << at / 2 << " changed at step " << step << " and was not owed";
                        }
                    }
                }
                had = runs;
            }

            // The walk has to have taken the incremental path, and the trades, to prove anything.
            EXPECT_LT(madeAgain, 30u);
            EXPECT_GT(traded, 30u);
        }
    }
}
