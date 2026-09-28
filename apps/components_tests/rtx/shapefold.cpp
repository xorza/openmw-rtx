#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <components/rtx/contentpreprocessor.hpp>
#include <components/rtx/pockettree.hpp>
#include <components/rtx/shapefold.hpp>

namespace Rtx
{
    namespace
    {
        /// The fold as the renderer asks it, with `indices` replaced by what it kept.
        FoldedShape foldInPlace(
            ContentPreprocessor& content, std::span<const osg::Vec3f> positions, std::vector<std::uint32_t>& indices)
        {
            const std::vector<std::uint32_t> named = indices;
            return content.fold(positions, named, indices);
        }
        /// A unit quad, and the same four positions again as the vertices its back was modelled
        /// with — which is how the content spells a card: eight vertices, not four.
        const std::array<osg::Vec3f, 8> sCard{
            osg::Vec3f(0.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 1.0f, 0.0f),
            osg::Vec3f(0.0f, 1.0f, 0.0f),
            osg::Vec3f(0.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 0.0f, 0.0f),
            osg::Vec3f(1.0f, 1.0f, 0.0f),
            osg::Vec3f(0.0f, 1.0f, 0.0f),
        };

        const std::vector<std::uint32_t> sFront{ 0, 1, 2, 0, 2, 3 };

        TEST(RtxShapeFoldTest, aCardDoubledForItsBackKeepsTheFrontAndIsASheet)
        {
            ContentPreprocessor fold;

            // The back wound the other way, on the second set of vertices.
            std::vector<std::uint32_t> indices{ 0, 1, 2, 0, 2, 3, 6, 5, 4, 7, 6, 4 };
            EXPECT_TRUE(foldInPlace(fold, sCard, indices).mSheet);
            EXPECT_EQ(indices, sFront) << "the copy the file wrote first is the one kept";

            // Folded again there is nothing left to pair, so a sheet is not a sheet twice.
            EXPECT_FALSE(foldInPlace(fold, sCard, indices).mSheet);
            EXPECT_EQ(indices, sFront);
        }

        TEST(RtxShapeFoldTest, aTwinIsMatchedByItsCornersAndNotByWhereTheFileStartedIt)
        {
            ContentPreprocessor fold;

            // (0, 1, 2) reversed is (0, 2, 1), which the file may as well spell (2, 1, 0) or
            // (1, 0, 2): every rotation of it is the same back.
            for (const std::array<std::uint32_t, 3> back : { std::array<std::uint32_t, 3>{ 4, 6, 5 },
                     std::array<std::uint32_t, 3>{ 6, 5, 4 }, std::array<std::uint32_t, 3>{ 5, 4, 6 } })
            {
                std::vector<std::uint32_t> indices{ 0, 1, 2, back[0], back[1], back[2] };
                EXPECT_TRUE(foldInPlace(fold, sCard, indices).mSheet);
                EXPECT_EQ(indices, (std::vector<std::uint32_t>{ 0, 1, 2 }));
            }

            // And the same triangle again with the same winding is a second front, not a back.
            std::vector<std::uint32_t> twice{ 0, 1, 2, 4, 5, 6 };
            EXPECT_FALSE(foldInPlace(fold, sCard, twice).mSheet);
            EXPECT_EQ(twice, (std::vector<std::uint32_t>{ 0, 1, 2, 4, 5, 6 }));
        }

        TEST(RtxShapeFoldTest, aSolidHasNoTwinsAndAMixedShapeLosesOnlyItsTwins)
        {
            ContentPreprocessor fold;

            // A tetrahedron: four faces, no two over the same three corners.
            const std::array<osg::Vec3f, 4> tetra{
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
            };
            std::vector<std::uint32_t> solid{ 0, 2, 1, 0, 1, 3, 1, 2, 3, 2, 0, 3 };
            const std::vector<std::uint32_t> before = solid;
            EXPECT_FALSE(foldInPlace(fold, tetra, solid).mSheet);
            EXPECT_EQ(solid, before);

            // A doubled card with one lone triangle beside it: the twin goes, the lone one stays,
            // and the shape is not a sheet — a leaf's stem is not lit through. It is folded all the
            // same, which is what keeps a ray that draws from culling the card the twin went from.
            std::vector<std::uint32_t> mixed{ 0, 1, 2, 2, 1, 0, 1, 2, 3 };
            const FoldedShape part = foldInPlace(fold, sCard, mixed);
            EXPECT_FALSE(part.mSheet);
            EXPECT_TRUE(part.mFolded);
            EXPECT_EQ(mixed, (std::vector<std::uint32_t>{ 0, 1, 2, 1, 2, 3 }));

            EXPECT_FALSE(foldInPlace(fold, tetra, solid).mFolded) << "a solid the fold took nothing from";

            std::vector<std::uint32_t> none;
            EXPECT_FALSE(foldInPlace(fold, sCard, none).mSheet);
            EXPECT_TRUE(none.empty());
        }

        /// A shape built out of quads, for the pocket cases: positions and the triangles over them.
        struct QuadShape
        {
            std::vector<osg::Vec3f> mPositions;
            std::vector<std::uint32_t> mIndices;

            /// The quad `a`, `b`, `c`, `d`, wound so it faces along `facing`.
            void addQuad(const osg::Vec3f& a, const osg::Vec3f& b, const osg::Vec3f& c, const osg::Vec3f& d,
                const osg::Vec3f& facing)
            {
                const bool turned = ((b - a) ^ (c - a)) * facing < 0.0f;
                const auto base = static_cast<std::uint32_t>(mPositions.size());
                for (const osg::Vec3f& corner : turned ? std::array{ a, d, c, b } : std::array{ a, b, c, d })
                    mPositions.push_back(corner);
                mIndices.insert(mIndices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
            }

            /// A square `side` across in the plane z = `height`, facing along z by `facing`'s sign.
            void addLevel(float side, float height, float facing)
            {
                const float h = 0.5f * side;
                addQuad(osg::Vec3f(-h, -h, height), osg::Vec3f(h, -h, height), osg::Vec3f(h, h, height),
                    osg::Vec3f(-h, h, height), osg::Vec3f(0.0f, 0.0f, facing));
            }

            /// A closed box from `low` to `high`, every face turned out.
            void addBox(const osg::Vec3f& low, const osg::Vec3f& high)
            {
                const auto at = [&](bool x, bool y, bool z) {
                    return osg::Vec3f(x ? high.x() : low.x(), y ? high.y() : low.y(), z ? high.z() : low.z());
                };
                for (const bool side : { false, true })
                {
                    const float out = side ? 1.0f : -1.0f;
                    addQuad(at(side, false, false), at(side, true, false), at(side, true, true), at(side, false, true),
                        osg::Vec3f(out, 0.0f, 0.0f));
                    addQuad(at(false, side, false), at(true, side, false), at(true, side, true), at(false, side, true),
                        osg::Vec3f(0.0f, out, 0.0f));
                    addQuad(at(false, false, side), at(true, false, side), at(true, true, side), at(false, true, side),
                        osg::Vec3f(0.0f, 0.0f, out));
                }
            }
        };

        /// A pocket loses the wall the file wrote second, and nothing else that faces itself does.
        ///
        /// **A pocket is two walls facing into a gap that is inside out**: the winding number at its
        /// middle is minus one, because each wall covers nearly a hemisphere of it and faces it. A
        /// slot is the same two walls with the solid behind each — the winding there is nought — and
        /// a plank's two faces turn away from each other, so no ray leaving either finds the other.
        /// Two quads a hundred across, two apart, are a pocket; the same pair ten across is a small
        /// thing's form, since its reach is five hundredths of its 14.3-unit diagonal, 0.72 units;
        /// and twelve apart is wider than the eight units any pocket stands.
        TEST(RtxShapeFoldTest, aPocketLosesItsLaterWallAndASlotOrAPlankKeepsBoth)
        {
            struct Case
            {
                const char* mName;
                QuadShape mShape;
                std::vector<std::uint32_t> mKept;
            };

            std::vector<Case> cases;

            const auto pocket = [](float side, float gap, bool upperFirst) {
                QuadShape shape;
                if (upperFirst)
                    shape.addLevel(side, gap, -1.0f);
                shape.addLevel(side, 0.0f, 1.0f);
                if (!upperFirst)
                    shape.addLevel(side, gap, -1.0f);
                return shape;
            };

            cases.push_back(
                { "a pocket keeps the wall written first", pocket(100.0f, 2.0f, false), { 0, 1, 2, 0, 2, 3 } });
            cases.push_back({ "whichever wall that is", pocket(100.0f, 2.0f, true), { 0, 1, 2, 0, 2, 3 } });

            QuadShape slot;
            slot.addBox(osg::Vec3f(-50.0f, -50.0f, -50.0f), osg::Vec3f(-1.0f, 50.0f, 50.0f));
            slot.addBox(osg::Vec3f(1.0f, -50.0f, -50.0f), osg::Vec3f(50.0f, 50.0f, 50.0f));
            cases.push_back({ "a slot between two solids", slot, slot.mIndices });

            QuadShape plank;
            plank.addBox(osg::Vec3f(-50.0f, -50.0f, 0.0f), osg::Vec3f(50.0f, 50.0f, 2.0f));
            cases.push_back({ "a plank", plank, plank.mIndices });

            const QuadShape small = pocket(10.0f, 2.0f, false);
            cases.push_back({ "a small thing's two walls", small, small.mIndices });

            const QuadShape wide = pocket(100.0f, 12.0f, false);
            cases.push_back({ "two walls further apart than a pocket", wide, wide.mIndices });

            ContentPreprocessor fold;
            for (Case& test : cases)
            {
                const bool dropping = test.mKept.size() != test.mShape.mIndices.size();
                const FoldedShape shape = foldInPlace(fold, test.mShape.mPositions, test.mShape.mIndices);

                EXPECT_EQ(test.mShape.mIndices, test.mKept) << test.mName;
                EXPECT_EQ(shape.mPocketed, dropping) << test.mName << ": the wall left answers for both faces";
                EXPECT_FALSE(shape.mFolded) << test.mName << ": no twin went";
                EXPECT_FALSE(shape.mSheet) << test.mName;
            }
        }

        /// The tree's winding number against the closed form, near a shape and far from it.
        ///
        /// **A closed box is one inside and nought outside**, exactly, and far enough off that every
        /// node answers by its dipole the dipoles of a closed shape sum to nought as well. **An open
        /// square is the solid angle it subtends**: a hundred across and one above its centre,
        /// `4 asin(a^2 / (a^2 + 4h^2))` = 4 asin(10000 / 10004) = 6.1700, over 4 pi, 0.49101 — negative,
        /// because it faces the point. And the first facing wall a ray meets is the one across from
        /// it, two units off, and not a wall turned the same way.
        TEST(RtxPocketTreeTest, theWindingNumberIsTheClosedFormAndARayMeetsTheWallAcross)
        {
            PocketTree tree;

            QuadShape box;
            box.addBox(osg::Vec3f(-50.0f, -50.0f, -50.0f), osg::Vec3f(50.0f, 50.0f, 50.0f));
            tree.build(box.mPositions, box.mIndices);
            EXPECT_NEAR(tree.windingAt(osg::Vec3f(10.0f, -20.0f, 5.0f)), 1.0, 1e-9) << "inside";
            EXPECT_NEAR(tree.windingAt(osg::Vec3f(60.0f, 0.0f, 0.0f)), 0.0, 1e-9) << "outside, near";
            EXPECT_NEAR(tree.windingAt(osg::Vec3f(5000.0f, 300.0f, 0.0f)), 0.0, 1e-9) << "outside, far";

            QuadShape square;
            square.addLevel(100.0f, 0.0f, 1.0f);
            tree.build(square.mPositions, square.mIndices);
            const double expected = -4.0 * std::asin(10000.0 / 10004.0) / (4.0 * std::numbers::pi);
            EXPECT_NEAR(tree.windingAt(osg::Vec3f(0.0f, 0.0f, 1.0f)), expected, 1e-6);
            EXPECT_NEAR(tree.windingAt(osg::Vec3f(0.0f, 0.0f, -1.0f)), -expected, 1e-6) << "and behind it";

            // Floor facing up, a ceiling two above facing down, and a second floor four above facing
            // up, which the ray passes: only a wall turned against the ray is across from it.
            QuadShape walls;
            walls.addLevel(100.0f, 0.0f, 1.0f);
            walls.addLevel(100.0f, 4.0f, 1.0f);
            walls.addLevel(100.0f, 2.0f, -1.0f);
            tree.build(walls.mPositions, walls.mIndices);
            const PocketTree::Facing met = tree.firstFacing(0, osg::Vec3f(10.0f, 10.0f, 0.0f), 1e-3f, 8.0f, -0.5f);
            EXPECT_FLOAT_EQ(met.mDistance, 2.0f);
            EXPECT_GE(met.mTriangle, 4u) << "a triangle of the ceiling, the third quad";
            EXPECT_EQ(tree.firstFacing(0, osg::Vec3f(10.0f, 10.0f, 0.0f), 1e-3f, 1.5f, -0.5f).mDistance,
                std::numeric_limits<float>::infinity())
                << "and nothing short of it";
        }

        /// A shape is closed when every edge of what survives the fold carries a triangle each way.
        ///
        /// **The fact that says which of a surface's two normals is lying**, and every case here is
        /// one the content ships. A tetrahedron stands for the solids, and the same tetrahedron with
        /// a face taken off stands for what Morrowind actually models — a rock is a dome with no
        /// base, which is why so little of the game answers yes.
        ///
        /// **A card is open both before and after its twin goes.** Doubled, every edge carries two
        /// triangles the *same* way round the outline and none the other; folded, it is one quad
        /// with a boundary. Neither is a solid, and the difference matters: `mSheet` and `mClosed`
        /// are two facts and a shape may carry both, so neither can be read off the other.
        TEST(RtxShapeFoldTest, aShapeIsClosedWhenEveryEdgeCarriesATriangleEachWay)
        {
            ContentPreprocessor fold;

            const std::array<osg::Vec3f, 4> tetra{
                osg::Vec3f(0.0f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
            };

            std::vector<std::uint32_t> solid{ 0, 2, 1, 0, 1, 3, 1, 2, 3, 2, 0, 3 };
            EXPECT_TRUE(foldInPlace(fold, tetra, solid).mClosed);

            // The same solid with one face off, which is the shape of every rock in the game.
            std::vector<std::uint32_t> dome{ 0, 1, 3, 1, 2, 3, 2, 0, 3 };
            EXPECT_FALSE(foldInPlace(fold, tetra, dome).mClosed);

            // A single quad: three of its four edges carry one triangle, and the shared diagonal
            // carries two — but both the same way.
            std::vector<std::uint32_t> quad = sFront;
            EXPECT_FALSE(foldInPlace(fold, sCard, quad).mClosed);

            // And the doubled card the fold reduces to that quad.
            std::vector<std::uint32_t> card{ 0, 1, 2, 0, 2, 3, 6, 5, 4, 7, 6, 4 };
            const FoldedShape folded = foldInPlace(fold, sCard, card);
            EXPECT_TRUE(folded.mSheet);
            EXPECT_FALSE(folded.mClosed);

            // A tetrahedron doubled inside out is both at once, which is what stops either fact
            // being read off the other. No shipped shape is, but two exteriors hold one each.
            std::vector<std::uint32_t> twinned{ 0, 2, 1, 0, 1, 3, 1, 2, 3, 2, 0, 3, 0, 1, 2, 0, 3, 1, 1, 3, 2, 2, 3,
                0 };
            const FoldedShape both = foldInPlace(fold, tetra, twinned);
            EXPECT_TRUE(both.mSheet);
            EXPECT_TRUE(both.mClosed);

            std::vector<std::uint32_t> none;
            EXPECT_FALSE(foldInPlace(fold, sCard, none).mClosed);
        }

        /// The rule, written again the slow obvious way, for the cross-check below.
        ///
        /// **Independent of the real one on purpose.** It compares every triangle against every
        /// other rather than looking a spelling up, and it rotates corners with a sort rather than
        /// with an index — so the two agree only where the rule they share is the rule, and not
        /// because they share a mistake.
        struct Reference
        {
            /// Which of two positions the rule counts as first. Both halves below turn on it.
            static bool lower(const osg::Vec3f& l, const osg::Vec3f& r)
            {
                return std::make_tuple(l.x(), l.y(), l.z()) < std::make_tuple(r.x(), r.y(), r.z());
            }

            static std::array<osg::Vec3f, 3> spelling(const osg::Vec3f& a, const osg::Vec3f& b, const osg::Vec3f& c)
            {
                std::array<osg::Vec3f, 3> rotated{ a, b, c };
                for (int turn = 0; turn < 2; ++turn)
                    if (lower(rotated[1], rotated[0]) || lower(rotated[2], rotated[0]))
                        rotated = { rotated[1], rotated[2], rotated[0] };

                return rotated;
            }

            /// The indices that survive, and whether every triangle was one of a pair.
            static std::pair<std::vector<std::uint32_t>, bool> fold(
                std::span<const osg::Vec3f> positions, const std::vector<std::uint32_t>& indices)
            {
                const std::size_t count = indices.size() / 3;

                const auto corners = [&](std::size_t t, bool reversed) {
                    return spelling(positions[indices[3 * t]], positions[indices[3 * t + (reversed ? 2 : 1)]],
                        positions[indices[3 * t + (reversed ? 1 : 2)]]);
                };

                enum class Fate
                {
                    Alone,
                    Kept,
                    Dropped
                };
                std::vector<Fate> fates(count, Fate::Alone);

                for (std::size_t t = 0; t < count; ++t)
                {
                    if (fates[t] != Fate::Alone)
                        continue;

                    for (std::size_t other = 0; other < count; ++other)
                    {
                        if (other == t || fates[other] != Fate::Alone || corners(other, false) != corners(t, true))
                            continue;

                        fates[t] = Fate::Kept;
                        fates[other] = Fate::Dropped;
                        break;
                    }
                }

                std::vector<std::uint32_t> kept;
                bool sheet = count > 0;
                for (std::size_t t = 0; t < count; ++t)
                {
                    if (fates[t] == Fate::Dropped)
                        continue;
                    if (fates[t] == Fate::Alone)
                        sheet = false;

                    kept.insert(kept.end(), indices.begin() + static_cast<std::ptrdiff_t>(3 * t),
                        indices.begin() + static_cast<std::ptrdiff_t>(3 * t + 3));
                }

                return { kept, sheet };
            }

            /// Whether every edge carries a triangle each way, written the slow obvious way too.
            ///
            /// **Every edge against every other rather than a table**, for the reason the fold's
            /// reference is written that way: the two agree only where the rule they share is the
            /// rule. What this guards is the arithmetic `ShapeFold::closes` exits early on — an odd
            /// triangle count and a count of distinct edges past three halves of one — neither of
            /// which appears here at all.
            ///
            /// **It guards the refusals and not the answers.** The sweep draws from a plane, so no
            /// shape it makes closes, and every comparison below is a no against a no. That a shape
            /// which does close still does is
            /// `aShapeIsClosedWhenEveryEdgeCarriesATriangleEachWay`'s tetrahedron, which is four
            /// triangles and six edges — exactly the limit the second exit refuses to pass.
            static bool closes(std::span<const osg::Vec3f> positions, const std::vector<std::uint32_t>& indices)
            {
                const std::size_t count = indices.size() / 3;
                if (count == 0)
                    return false;

                using Ends = std::array<float, 6>;
                const auto ends = [](const osg::Vec3f& low, const osg::Vec3f& high) {
                    return Ends{ low.x(), low.y(), low.z(), high.x(), high.y(), high.z() };
                };
                std::vector<std::pair<Ends, bool>> edges;
                for (std::size_t t = 0; t < count; ++t)
                    for (std::size_t side = 0; side < 3; ++side)
                    {
                        const osg::Vec3f& from = positions[indices[3 * t + side]];
                        const osg::Vec3f& to = positions[indices[3 * t + (side + 1) % 3]];

                        // A degenerate edge belongs to no pair and would pair with itself.
                        if (from == to)
                            return false;

                        const bool forward = lower(from, to);
                        edges.emplace_back(forward ? ends(from, to) : ends(to, from), forward);
                    }

                for (const auto& [key, ignored] : edges)
                {
                    std::size_t forwards = 0;
                    std::size_t backwards = 0;
                    for (const auto& [other, otherForward] : edges)
                        if (other == key)
                            (otherForward ? forwards : backwards) += 1;

                    if (forwards != 1 || backwards != 1)
                        return false;
                }

                return true;
            }
        };

        /// Every shape the content can hand it, against the rule written the slow way.
        ///
        /// **What the three cases above cannot reach.** A doubled card is two triangles and the
        /// answer is obvious; a merged paging chunk is tens of thousands, with the same corner
        /// spelled by triangles far apart in the list, triangles doubled three and four times over,
        /// and degenerate ones whose reverse is their own spelling. Which copy survives depends on
        /// the order the pairing walks, and getting that wrong deletes geometry the player can see.
        ///
        /// A fixed seed, so a failure is a failure that can be run again.
        TEST(RtxShapeFoldTest, everyShapeFoldsTheWayTheRuleSaysItShould)
        {
            std::mt19937 random(20260830);
            ContentPreprocessor fold;

            // A small pool of positions, so triangles collide often and the awkward cases happen
            // rather than being hoped for.
            for (const std::size_t corners : { std::size_t{ 3 }, std::size_t{ 5 }, std::size_t{ 12 } })
            {
                std::vector<osg::Vec3f> positions;
                for (std::size_t at = 0; at < corners; ++at)
                    positions.push_back(osg::Vec3f(static_cast<float>(at % 3), static_cast<float>(at / 3), 0.0f));

                for (int attempt = 0; attempt < 200; ++attempt)
                {
                    const std::size_t count = 1 + random() % 24;

                    std::vector<std::uint32_t> indices;
                    for (std::size_t t = 0; t < count; ++t)
                    {
                        const auto corner = [&] { return static_cast<std::uint32_t>(random() % positions.size()); };
                        const std::uint32_t a = corner();
                        const std::uint32_t b = corner();
                        const std::uint32_t c = corner();

                        indices.insert(indices.end(), { a, b, c });

                        // Half of them doubled the way the content doubles a card, so most meshes
                        // here have twins to find rather than being noise.
                        if (random() % 2 == 0)
                            indices.insert(indices.end(), { a, c, b });
                    }

                    const auto [expected, expectedSheet] = Reference::fold(positions, indices);

                    // **On what survives**, because that is what `fold` asks the question of.
                    const bool expectedClosed = Reference::closes(positions, expected);

                    std::vector<std::uint32_t> folded = indices;
                    const FoldedShape shape = foldInPlace(fold, positions, folded);

                    EXPECT_EQ(folded, expected) << "corners " << corners << ", attempt " << attempt;
                    EXPECT_EQ(shape.mSheet, expectedSheet) << "corners " << corners << ", attempt " << attempt;
                    EXPECT_EQ(shape.mClosed, expectedClosed) << "corners " << corners << ", attempt " << attempt;
                }
            }
        }
    }
}
