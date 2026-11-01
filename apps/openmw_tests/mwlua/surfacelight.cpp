#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include <sol/sol.hpp>

#include <components/files/conversion.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/utilpackage.hpp>

namespace MWLua
{
    namespace
    {
        /// `scripts/omw/surfacelight/common.lua` run over the util package and over the names it
        /// reads of `openmw.core`, with every lamp and position read back as a string of its numbers.
        class SurfaceLightTest : public testing::Test
        {
        protected:
            LuaUtil::LuaState mLuaState{ nullptr, nullptr };

            SurfaceLightTest()
            {
                const std::filesystem::path root{ OPENMW_PROJECT_SOURCE_DIR };
                const std::filesystem::path script
                    = root / "files" / "data-mw" / "scripts" / "omw" / "surfacelight" / "common.lua";
                mLuaState.addInternalLibSearchPath(root / "components" / "lua");
                mLuaState.protectedCall([&](LuaUtil::LuaView& view) {
                    sol::state_view& lua = view.sol();
                    lua["util"] = LuaUtil::initUtilPackage(lua);
                    lua.safe_script(R"(
                    local packages = {
                        ['openmw.util'] = util,
                        ['openmw.core'] = {
                            magic = {
                                EFFECT_TYPE = { Light = 'light', FireDamage = 'firedamage' },
                                RANGE = { Self = 0, Touch = 1, Target = 2 },
                            },
                        },
                    }
                    require = function(name) return assert(packages[name], name) end
                    rolls = {}
                    rollMax = function(min, max) rolls[#rolls + 1] = min .. '..' .. max return max end
                    rollMin = function(min, max) rolls[#rolls + 1] = min .. '..' .. max return min end
                    light = function(range, min, max, duration)
                        return { id = 'light', range = range, magnitudeMin = min, magnitudeMax = max, duration = duration }
                    end
                    describe = function(lamps)
                        local out = {}
                        for _, lamp in ipairs(lamps) do
                            out[#out + 1] = string.format('%g, %g off, for %g', lamp.radius, lamp.standOff, lamp.duration)
                        end
                        return table.concat(out, '; ')
                    end
                    at = function(v) return string.format('(%g, %g, %g)', v.x, v.y, v.z) end
                )");
                    lua["surfaceLight"] = lua.safe_script_file(Files::pathToUnicodeString(script));
                });
            }

            void exec(const std::string& code)
            {
                mLuaState.protectedCall([&](LuaUtil::LuaView& view) { view.sol().safe_script(code); });
            }

            std::string run(const std::string& code)
            {
                std::string result;
                mLuaState.protectedCall(
                    [&](LuaUtil::LuaView& view) { result = view.sol().safe_script(code).get<std::string>(); });
                return result;
            }
        };

        /// **A Light effect on Target leaves a lamp where its bolt struck**, and no other effect does:
        /// of three feet of radius a point of magnitude, at 22 units a foot, a quarter of that radius
        /// off the surface, for the effect's duration. A roll of 10 is 660 units, 165 off; a roll of 5
        /// is 330, 82.5 off.
        TEST_F(SurfaceLightTest, aLightOnTargetIsThriceTheActorsGlowAQuarterOfItOffTheSurface)
        {
            exec(R"(
                effects = {
                    light(0, 5, 10, 30),
                    light(1, 5, 10, 30),
                    { id = 'firedamage', range = 2, magnitudeMin = 5, magnitudeMax = 10, duration = 30 },
                    light(2, 5, 10, 30),
                }
            )");

            EXPECT_EQ(run("return describe(surfaceLight.lamps(effects, rollMax))"), "660, 165 off, for 30");
            EXPECT_EQ(run("return table.concat(rolls, ' ')"), "5..10") << "only the Light on Target rolled";
            EXPECT_EQ(run("return describe(surfaceLight.lamps(effects, rollMin))"), "330, 82.5 off, for 30");
        }

        /// **The magnitude is the engine's**: a range of one value, or a maximum under its minimum,
        /// is the minimum and rolls nothing, as `roll` in `spelleffects.cpp` gives it. 7 is 462
        /// units, 115.5 off; 8 is 528, 132 off.
        TEST_F(SurfaceLightTest, anEmptyRangeIsItsMinimumAndRollsNothing)
        {
            EXPECT_EQ(run("return describe(surfaceLight.lamps({ light(2, 7, 7, 4), light(2, 8, 3, 6) }, rollMax))"),
                "462, 115.5 off, for 4; 528, 132 off, for 6");
            EXPECT_EQ(run("return table.concat(rolls, ' ')"), "");
        }

        /// **An effect of no magnitude or no duration lights nothing**, as on an actor, and every
        /// other Light on Target stands its own lamp, in the spell's order.
        TEST_F(SurfaceLightTest, onlyALightWithMagnitudeAndDurationStandsALamp)
        {
            EXPECT_EQ(run(R"(
                return describe(surfaceLight.lamps(
                    { light(2, 0, 0, 30), light(2, 1, 1, 0), light(2, 1, 1, 2), light(2, 2, 2, 3) }, rollMax))
            )"),
                "66, 16.5 off, for 2; 132, 33 off, for 3");
            EXPECT_EQ(run("return describe(surfaceLight.lamps({}, rollMax))"), "");
        }

        /// **A lamp stands its distance off the surface along the normal, or midway to what is in the
        /// way**: 165 off a wall at y = 200 facing -y is y = 35, and off a floor at z = 300 is z = 465.
        /// An obstacle 400 away leaves it there, as 165 < 400 / 2; one 200 away stands it at 100, and
        /// one at 330, exactly twice the distance, at 165 either way.
        TEST_F(SurfaceLightTest, aLampStandsItsDistanceOffOrMidwayToAnObstacle)
        {
            exec("hit = util.vector3(100, 200, 300)");
            EXPECT_EQ(run("return at(surfaceLight.stand(hit, util.vector3(0, -1, 0), 165, nil))"), "(100, 35, 300)");
            EXPECT_EQ(run("return at(surfaceLight.stand(hit, util.vector3(0, 0, 1), 165, nil))"), "(100, 200, 465)");
            EXPECT_EQ(run("return at(surfaceLight.stand(hit, util.vector3(0, 0, 1), 165, 400))"), "(100, 200, 465)");
            EXPECT_EQ(run("return at(surfaceLight.stand(hit, util.vector3(0, 0, 1), 165, 200))"), "(100, 200, 400)");
            EXPECT_EQ(run("return at(surfaceLight.stand(hit, util.vector3(0, 0, 1), 165, 330))"), "(100, 200, 465)");
        }
    }
}
