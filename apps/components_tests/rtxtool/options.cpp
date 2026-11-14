#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/refusal.hpp>
#include <apps/rtxtool/framed.hpp>
#include <apps/rtxtool/model/film.hpp>
#include <apps/rtxtool/options.hpp>
#include <apps/rtxtool/runfolder.hpp>
#include <apps/rtxtool/toolline.hpp>
#include <apps/rtxtool/verbs.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/world/weather.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        /// What a command line comes to, parsed against the harness's own description.
        bpo::parsed_options parse(const ToolOptions& options, const std::vector<std::string>& line)
        {
            return bpo::command_line_parser(line).options(options.mDescription).run();
        }

        /// What `arguments` come to for `verb`, read as the harness reads a line: parsed against its own
        /// description, with no configuration file.
        ToolLine lineOf(const ToolOptions& options, const std::vector<std::string>& arguments, const Verb verb)
        {
            bpo::variables_map variables;
            bpo::store(parse(options, arguments), variables);
            bpo::notify(variables);
            return options.read(variables, verb);
        }

        /// What refuses a line read for `verb`, or that nothing did.
        std::string refusalOf(const ToolOptions& options, const std::vector<std::string>& arguments, const Verb verb)
        {
            return Rtx::Testing::refusalOf([&] { lineOf(options, arguments, verb); });
        }

        /// What `--validation` comes to when nobody names it, which is what `makeOptions` is told.
        std::string_view defaultValidation(const Rtx::ValidationLevel byDefault)
        {
            const ToolLine line = lineOf(makeOptions(byDefault), {}, Verb::Shot);
            EXPECT_FALSE(line.mValidation.mDemanded) << "a level nobody named is demanded";
            return Rtx::sValidationNames.name(line.mValidation.mLevel);
        }

        /// **The other side of `noise --versus` changes the one switch it names**, read as the line would
        /// read it, and keeps every other where the line put it.
        /// A switch named alone takes its implicit value; naming the line's own value is the same side
        /// again. Each switch `--versus` may name is one the reader reads: at a value other than the
        /// default, it moves the request. A name no reconstruction reads, and a value its switch
        /// refuses, are refused.
        TEST(RtxToolOptionsTest, theOtherSideChangesTheOneSwitchItNames)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);
            const std::vector<std::string> playedLine{ "--antilag=false", "--lamp-candidates=4",
                "--antifirefly=false" };
            const Rtx::ReconstructionRequest played = lineOf(options, playedLine, Verb::Noise).mFrame.mReconstruction;
            ASSERT_FALSE(played.mFilters.mAntilag);
            ASSERT_EQ(played.mSampling.mLampCandidates, 4u);

            Rtx::ReconstructionRequest ringed = played;
            ringed.mFilters.mAntiFirefly = true;
            EXPECT_EQ(options.versus(played, "antifirefly=true"), ringed);
            EXPECT_EQ(options.versus(played, "antifirefly"), ringed) << "the implicit value";
            std::vector<std::string> versusLine = playedLine;
            versusLine.emplace_back("--versus=antifirefly");
            const ToolLine::Noise noise = lineOf(options, versusLine, Verb::Noise).mNoise;
            EXPECT_EQ(noise.mAsked, "antifirefly");
            EXPECT_EQ(noise.mVersus, ringed) << "the line's own other side is over the line's own switches";
            EXPECT_EQ(lineOf(options, {}, Verb::Noise).mNoise.mVersus, std::nullopt);
            Rtx::ReconstructionRequest every = played;
            every.mSampling.mLampCandidates = 0;
            EXPECT_EQ(options.versus(played, "lamp-candidates=0"), every);
            EXPECT_EQ(options.versus(played, "antilag=false"), played) << "the same side again";

            const std::array<std::pair<std::string_view, std::string_view>, sReconstructionSwitches.size()> moved{ {
                { "filter", "false" },
                { "jitter", "true" },
                { "noise", "white-hash" },
                { "level-epsilon", "0.5" },
                { "shadow-floor", "0.25" },
                { "lamp-candidates", "0" },
                { "indirect", "false" },
                { "bounce-rays", "4" },
                { "relief", "false" },
                { "soft-edge-dither", "false" },
                { "water-interleave", "false" },
                { "antilag", "false" },
                { "history-fix", "false" },
                { "dual-motion", "false" },
                { "antifirefly", "false" },
            } };
            const Rtx::ReconstructionRequest plain = lineOf(options, {}, Verb::Noise).mFrame.mReconstruction;
            EXPECT_EQ(plain, Rtx::ReconstructionRequest{});
            for (std::size_t at = 0; at < moved.size(); ++at)
            {
                EXPECT_EQ(moved[at].first, sReconstructionSwitches[at]);
                EXPECT_NE(options.versus(plain, std::format("{}={}", moved[at].first, moved[at].second)), plain)
                    << moved[at].first << " is not read";
            }

            EXPECT_EQ(Rtx::Testing::refusalOf([&] { options.versus(played, "gamma=2.2"); }),
                "--versus=gamma=2.2 names no switch of the reconstruction, which are filter, jitter, noise, "
                "level-epsilon, shadow-floor, lamp-candidates, indirect, bounce-rays, relief, soft-edge-dither, "
                "water-interleave, antilag, history-fix, dual-motion, antifirefly");
            EXPECT_EQ(Rtx::Testing::refusalOf([&] { options.versus(played, "noise=sideways"); }),
                "\"sideways\" is not a noise source: blue-noise or white-hash");
            EXPECT_EQ(Rtx::Testing::refusalOf([&] { options.versus(played, "antifirefly=maybe"); }),
                "--versus=antifirefly=maybe: the argument ('maybe') for option '--antifirefly' is invalid. Valid "
                "choices are 'on|off', 'yes|no', '1|0' and 'true|false'");
            EXPECT_EQ(Rtx::Testing::refusalOf([&] { options.versus(played, "bounce-rays=0"); }),
                "--versus=bounce-rays=0: the argument ('0') for option '--bounce-rays' is not a number 1..4")
                << "no bounce is `indirect`'s";
            EXPECT_EQ(Rtx::Testing::refusalOf([&] { options.versus(played, "bounce-rays=9"); }),
                "--versus=bounce-rays=9: the argument ('9') for option '--bounce-rays' is not a number 1..4")
                << "past the landing's heads";
            EXPECT_EQ(Rtx::Testing::refusalOf([&] { options.versus(played, "shadow-floor=0.5"); }),
                "--versus=shadow-floor=0.5: the argument ('0.5') for option '--shadow-floor' is not a number 0..0.25")
                << "a floor over a quarter, which can leave a pixel nothing to draw";
        }

        /// **A stop runs `--frames` where it is named and `--seconds` otherwise**, and never both:
        /// each says how long a stop runs. The seconds nobody named are twenty.
        TEST(RtxToolOptionsTest, aStopRunsItsFramesOrItsSecondsAndNeverBoth)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);
            const auto span
                = [&](const std::vector<std::string>& line) { return lineOf(options, line, Verb::Bench).mBench.mSpan; };

            const BenchSpan plain = span({});
            EXPECT_EQ(plain.mFrames, 0u);
            EXPECT_EQ(plain.mSeconds, 20.0f);
            const BenchSpan counted = span({ "--frames=300" });
            EXPECT_EQ(counted.mFrames, 300u);
            EXPECT_EQ(counted.mSeconds, 0.0f);
            EXPECT_EQ(span({ "--seconds=5" }).mSeconds, 5.0f);
            EXPECT_EQ(Rtx::Testing::refusalOf([&] { span({ "--frames=300", "--seconds=5" }); }),
                "--frames and --seconds each say how long a stop runs, so only one can be named");
        }

        TEST(RtxToolOptionsTest, anUpscalerCanBeSelectedWithoutChangingTheQualityMode)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);
            const Named<Rtx::UpscalerChoice> unnamed = lineOf(options, {}, Verb::Bench).mFrame.mUpscaler;
            EXPECT_EQ(unnamed.mValue, Rtx::UpscalerChoice::Fsr);
            EXPECT_FALSE(unnamed.mNamed);
            for (const auto& [kind, name] : Rtx::sUpscalerNames.mNames)
            {
                const ToolLine line
                    = lineOf(options, { std::format("--upscaler={}", name), "--upscale=quality" }, Verb::Bench);
                EXPECT_EQ(line.mFrame.mUpscaler.mValue, kind);
                EXPECT_TRUE(line.mFrame.mUpscaler.mNamed);
                EXPECT_EQ(line.mFrame.mUpscale.mValue, Rtx::Upscale::Quality);
            }
            EXPECT_EQ(Rtx::Testing::refusalOf([&] { lineOf(options, { "--upscaler=unknown" }, Verb::Bench); }),
                "\"unknown\" is not an upscaler: auto, fsr or dlss");
        }

        /// **The line is read once into what each option means, and a condition nobody named is none**:
        /// no hour, weather, air, places of the file's or folder, and the day, the hold and the upscaler
        /// unnamed at their defaults. `--views` with nothing in it is every view, not none. A place named
        /// beside `--views`, a weather that is none of the ten, a point that is not three numbers, and a
        /// size or exposure that is none are refused with the option that named them.
        TEST(RtxToolOptionsTest, aLineIsReadOnceAndWhatNobodyNamedIsNone)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);

            const ToolLine plain = lineOf(options, {}, Verb::Shot);
            EXPECT_FALSE(plain.mSession.mSpellHands);
            EXPECT_TRUE(lineOf(options, { "--spell-hands" }, Verb::Shot).mSession.mSpellHands);
            EXPECT_EQ(plain.mSky.mHour, std::nullopt);
            EXPECT_EQ(plain.mSky.mWeather, std::nullopt);
            EXPECT_FALSE(plain.mSky.mAir.has_value());
            EXPECT_TRUE(plain.mSky.mTurn.empty());
            EXPECT_EQ(plain.mPlaces.mViews, std::nullopt);
            EXPECT_EQ(plain.mPlaces.mSuite, std::nullopt);
            EXPECT_TRUE(plain.mPlaces.mCell.empty());
            EXPECT_EQ(plain.mPlaces.mPos, std::nullopt);
            EXPECT_EQ(plain.mOut, std::nullopt);
            EXPECT_EQ(plain.mFrame.mDay.mValue, 0);
            EXPECT_FALSE(plain.mFrame.mDay.mNamed);
            EXPECT_FALSE(plain.mFrame.mHold.mNamed);
            EXPECT_EQ(plain.mFrame.mUpscale.mValue, sUpscaleByDefault);
            EXPECT_FALSE(plain.mFrame.mUpscale.mNamed);
            EXPECT_FALSE(plain.mFrame.mBouncesNamed.mIndirect);
            EXPECT_FALSE(plain.mFrame.mBouncesNamed.mBounceRays);

            // Each named apart, so a window takes the other from the player's settings.
            const ToolLine rays = lineOf(options, { "--bounce-rays=3" }, Verb::View);
            EXPECT_EQ(rays.mFrame.mReconstruction.mSampling.mBounceRays, 3u);
            EXPECT_TRUE(rays.mFrame.mBouncesNamed.mBounceRays);
            EXPECT_FALSE(rays.mFrame.mBouncesNamed.mIndirect);
            EXPECT_TRUE(lineOf(options, { "--indirect=false" }, Verb::View).mFrame.mBouncesNamed.mIndirect);
            EXPECT_EQ(plain.mFrame.mSize.mWidth, 1920u);
            EXPECT_EQ(plain.mFrame.mSize.mHeight, 1080u);

            const ToolLine named = lineOf(options,
                { "--hour=6.5", "--weather=Rain", "--turn-weather=Clear,Ashstorm", "--day=3", "--hold=false",
                    "--upscale=off", "--cell=-2,-9", "--pos=1,2,3", "--out=pictures", "--size=640x360" },
                Verb::Shot);
            EXPECT_EQ(named.mSky.mHour, 6.5f);
            EXPECT_EQ(named.mSky.mWeather, Rtx::Weather::Rain);
            EXPECT_EQ(named.mSky.mTurn, (std::vector{ Rtx::Weather::Clear, Rtx::Weather::Ashstorm }));
            EXPECT_EQ(named.mFrame.mDay.mValue, 3);
            EXPECT_TRUE(named.mFrame.mDay.mNamed);
            EXPECT_FALSE(named.mFrame.mHold.mValue);
            EXPECT_TRUE(named.mFrame.mHold.mNamed) << "a hold named off is named";
            EXPECT_EQ(named.mFrame.mUpscale.mValue, Rtx::Upscale::Off);
            EXPECT_TRUE(named.mFrame.mUpscale.mNamed);
            EXPECT_TRUE(named.mPlaces.mCell.isExterior());
            EXPECT_EQ(named.mPlaces.mPos, osg::Vec3f(1.0f, 2.0f, 3.0f));
            EXPECT_EQ(named.mOut, std::filesystem::path("pictures"));
            EXPECT_EQ(named.mFrame.mSize.mWidth, 640u);
            EXPECT_EQ(named.mFrame.mSize.mHeight, 360u);

            EXPECT_EQ(
                lineOf(options, { "--views=a,b" }, Verb::Shot).mPlaces.mViews, (std::vector<std::string>{ "a", "b" }));
            EXPECT_EQ(lineOf(options, { "--views=," }, Verb::Shot).mPlaces.mViews, std::vector<std::string>{})
                << "named with nothing in it, which is every view";

            EXPECT_EQ(refusalOf(options, { "--views=a", "--pos=1,2,3" }, Verb::Shot),
                "--views takes its places from the file, and --pos names one beside them");
            EXPECT_EQ(refusalOf(options, { "--pos=1,2" }, Verb::Shot),
                R"(--pos is not three numbers separated by commas: "1,2")");
            EXPECT_EQ(refusalOf(options, { "--size=640" }, Verb::Shot), "--size=640 is not WIDTHxHEIGHT");
            EXPECT_EQ(refusalOf(options, { "--exposure=0" }, Verb::Shot),
                "--exposure=0 is neither auto nor a number over nought");
            EXPECT_NE(
                refusalOf(options, { "--weather=Sunny" }, Verb::Shot).find(R"(--weather: "Sunny")"), std::string::npos);
        }

        /// **A film is twenty seconds unless its length or its speed is named**, and never both: the
        /// length sets the speed, so two named would be two answers to one question. The twenty are
        /// nobody's, which is what lets them stand aside where they cannot be filled.
        TEST(RtxToolOptionsTest, aFilmIsItsLengthUnlessItsSpeedIsNamed)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);
            const auto length = [&](const std::vector<std::string>& line) {
                return lineOf(options, line, Verb::Film).mFilm.mPacing.mLength;
            };
            const auto refusal
                = [&](const std::vector<std::string>& line) { return refusalOf(options, line, Verb::Film); };

            EXPECT_EQ(length({}), (FilmLength{ .mSeconds = 20.0f, .mSource = FilmLengthSource::ByDefault }));
            EXPECT_EQ(FilmPacing::sLengthByDefault, 20.0f);
            EXPECT_EQ(length({ "--length=35" }), (FilmLength{ .mSeconds = 35.0f, .mSource = FilmLengthSource::Named }));
            EXPECT_EQ(length({ "--speed=500" }), std::nullopt);
            EXPECT_EQ(refusal({ "--length=35", "--speed=500" }),
                "--length sets the speed, so --speed cannot be named beside it");
            EXPECT_EQ(refusal({ "--length=0" }), "the argument ('0') for option '--length' is not a number >0");
        }

        /// The build decides the level nobody named, and the level is spelled the way the table
        /// spells it, so a script that names `sync` and a build that defaults to it agree.
        TEST(RtxToolOptionsTest, theBuildDecidesTheValidationLevelNobodyNamed)
        {
            EXPECT_EQ(defaultValidation(Rtx::ValidationLevel::Sync), "sync");
            EXPECT_EQ(defaultValidation(Rtx::ValidationLevel::Off), "off");
        }

        /// **`check` draws a frame of its own where the line names none, and no other command does.** 957x539, no
        /// extent of which is a multiple of the 8 a workgroup is a side, nor of 638x359, what the default upscale
        /// traces it at; a size the line names is the line's under `check` as under any.
        TEST(RtxToolOptionsTest, checkDrawsItsOwnFrameWhereTheLineNamesNone)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);
            const auto size = [&](const std::vector<std::string>& line, Verb verb) {
                const FrameSize read = lineOf(options, line, verb).mFrame.mSize;
                return std::pair(read.mWidth, read.mHeight);
            };

            EXPECT_EQ(size({}, Verb::Check), std::pair(957u, 539u));
            EXPECT_EQ(size({ "--size=1920x1080" }, Verb::Check), std::pair(1920u, 1080u));
            for (const Verb other : { Verb::Scene, Verb::Shot, Verb::View, Verb::Bench, Verb::Film, Verb::Noise })
                EXPECT_EQ(size({}, other), std::pair(1920u, 1080u)) << verbName(other);
            EXPECT_EQ(size({ "--size=957x539" }, Verb::Bench), std::pair(957u, 539u));

            const Rtx::FrameExtents traced
                = Rtx::extentsFor(sCheckFrame.mWidth, sCheckFrame.mHeight, sUpscaleByDefault);
            EXPECT_EQ(std::pair(traced.mOutputWidth, traced.mOutputHeight), std::pair(957u, 539u));
            EXPECT_EQ(std::pair(traced.mRenderWidth, traced.mRenderHeight), std::pair(638u, 359u));
            for (const std::uint32_t extent :
                { traced.mOutputWidth, traced.mOutputHeight, traced.mRenderWidth, traced.mRenderHeight })
                EXPECT_NE(extent % 8u, 0u) << extent;
        }

        /// **`check` is the run that holds every kernel against its shaders**, which the GPU tests lean on since they
        /// leave the layers' shader checks to it (`Testing::describeValidation`): its line asks for them, at the
        /// level the build validates at, and it is no measuring command, whose layers `frameFrom` takes away.
        TEST(RtxToolOptionsTest, checkKeepsTheLayersAndTheirShaderChecks)
        {
            const ToolLine line = lineOf(makeOptions(Rtx::ValidationLevel::Sync), {}, Verb::Check);
            EXPECT_EQ(line.mValidation.mLevel, Rtx::ValidationLevel::Sync);
            EXPECT_EQ(line.mValidation.mShaders, Rtx::ShaderValidation::Checked);
            EXPECT_FALSE(policyOf(Verb::Check).mMeasures);
        }

        /// An option belonging to one command, given to another, is refused: every option is declared on one
        /// description and each command reads the ones it knows about, so `shot --views=balmora` would
        /// otherwise render the default view without a word.
        TEST(RtxToolOptionsTest, aCommandRefusesAnOptionItDoesNotRead)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);

            EXPECT_EQ(options.complainAbout(parse(options, { "--views=balmora" }), Verb::View),
                "`view` does not read --views, which belongs to every command but `info`, `view`, `film` and "
                "`tour`.\n");

            EXPECT_EQ(options.complainAbout(parse(options, { "--views=balmora" }), Verb::Bench), "")
                << "the command the option belongs to takes it";
            EXPECT_EQ(options.complainAbout(parse(options, { "--views=balmora" }), Verb::Shot), "");
            EXPECT_EQ(options.complainAbout(parse(options, { "--views=balmora" }), Verb::Check), "");

            // The same mistake the other way round: a run of places takes its cell from `--views`,
            // and `--view` is what a command that stands at one place reads.
            EXPECT_EQ(options.complainAbout(parse(options, { "--view=balmora" }), Verb::Bench),
                "`bench` does not read --view, which belongs to `scene`, `shot` and `view`.\n");
            EXPECT_EQ(options.complainAbout(parse(options, { "--view=balmora" }), Verb::Shot), "");

            // A film's keys each name their own sky, and a tour turns through every weather on its own clock, so one
            // sky on the line would be thrown away.
            EXPECT_EQ(options.complainAbout(parse(options, { "--hour=6" }), Verb::Film),
                "`film` does not read --hour, which belongs to every command but `info`, `film` and `tour`.\n");
            EXPECT_EQ(options.complainAbout(parse(options, { "--weather=Rain" }), Verb::Tour),
                "`tour` does not read --weather, which belongs to every command but `info`, `film` and `tour`.\n");
            EXPECT_EQ(options.complainAbout(parse(options, { "--keys=tour.keys" }), Verb::View), "")
                << "a window writes the keys a film reads";
            EXPECT_EQ(options.complainAbout(parse(options, { "--keys=tour.keys", "--day=3" }), Verb::Film), "");
        }

        /// Every option on the line is answered for, and each of them once.
        TEST(RtxToolOptionsTest, aLineIsAnsweredForOptionByOption)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);

            // Two the command does not read, around one it does and one nobody restricted.
            const bpo::parsed_options line
                = parse(options, { "--suite=default", "--upscale=off", "--find=barrel", "--seconds=4" });

            EXPECT_EQ(
                options.complainAbout(line, Verb::Bench), "`bench` does not read --find, which belongs to `scene`.\n");
            EXPECT_EQ(options.complainAbout(line, Verb::Scene),
                "`scene` does not read --suite, which belongs to `bench`, `check` and `noise`.\n"
                "`scene` does not read --seconds, which belongs to `bench` and `check`.\n");

            // An option written twice is worth one complaint.
            const bpo::parsed_options twice = parse(options, { "--out=a", "--out=b" });
            EXPECT_EQ(options.complainAbout(twice, Verb::Scene),
                "`scene` does not read --out, which belongs to every command but `info` and `scene`.\n");
        }

        /// Every option says which commands read it, and the ones that say "all of them" say it.
        ///
        /// What this holds shut: a knob with no owner would answer `VerbSet::every()`, so `info --size=800x600
        /// --delight=0` would be taken and thrown away.
        TEST(RtxToolOptionsTest, everyOptionSaysWhichCommandsReadIt)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);

            // Upstream's own — `--config` and its three siblings — reach the same description
            // through `Files::ConfigurationManager` and are every command's by nature. They are the
            // only names `readsOption` is allowed to answer by falling through.
            bpo::options_description upstream("");
            Files::ConfigurationManager::addCommonOptions(upstream);

            std::set<std::string> owned;
            for (const OptionOwner& owner : options.mOwners)
                owned.insert(std::string(owner.mName));

            for (const auto& declared : options.mDescription.options())
            {
                const std::string& name = declared->long_name();
                const bool theirs = upstream.find_nothrow(name, false) != nullptr;

                EXPECT_TRUE(theirs || owned.contains(name)) << name << " reached the description with no owner";
            }

            EXPECT_EQ(options.readsOption("validation"), VerbSet::every());
            EXPECT_EQ(options.readsOption("data"), VerbSet::every()) << "the engine's own, read by every command";
            EXPECT_EQ(options.readsOption("load-savegame"), VerbSet{ Verb::Info }.complement())
                << "info starts no game";
            EXPECT_EQ(options.readsOption("random-seed"), VerbSet{ Verb::Info }.complement());
            EXPECT_EQ(options.readsOption("views"),
                (VerbSet{ Verb::Scene, Verb::Shot, Verb::Bench, Verb::Check, Verb::Noise }));
            EXPECT_EQ(options.readsOption("suite"), (VerbSet{ Verb::Bench, Verb::Check, Verb::Noise }));

            // Every command but `info` builds a frame, and `info` reports on a device. A film is
            // always made under one mode, and names none.
            EXPECT_EQ(options.readsOption("upscale"), (VerbSet{ Verb::Info, Verb::Film }.complement()));
            EXPECT_EQ(options.readsOption("size"), VerbSet{ Verb::Info }.complement());
            EXPECT_EQ(options.complainAbout(parse(options, { "--size=8x8" }), Verb::Info),
                "`info` does not read --size, which belongs to every command but `info`.\n");
            EXPECT_EQ(options.complainAbout(parse(options, { "--size=8x8" }), Verb::Check), "")
                << "`check` frames a camera through the same request every other command does";

            for (const std::string_view name : { "info", "scene", "shot", "view", "bench", "check", "film" })
                EXPECT_EQ(options.complainAbout(parse(options, { "--validation=off" }), verbNamed(name).value()), "")
                    << name;
        }

        /// The help line and the check are one statement, so a reader is told what the tool
        /// enforces.
        TEST(RtxToolOptionsTest, anOwnedOptionSaysSoInItsHelpLine)
        {
            const ToolOptions options = makeOptions(Rtx::ValidationLevel::Off);

            const auto lineFor
                = [&](const std::string& name) { return options.mDescription.find(name, false).description(); };

            EXPECT_TRUE(lineFor("views").starts_with("with every command but `info`, `view`, `film` and `tour`, "))
                << lineFor("views");
            EXPECT_TRUE(lineFor("find").starts_with("with `scene`, ")) << lineFor("find");
            EXPECT_TRUE(lineFor("warmup").starts_with("with `bench`, ")) << "the clock is a bench's to wait for";

            // Six of the seven read a camera, so the line names the one that does not rather than
            // the six that do.
            EXPECT_TRUE(lineFor("fov").starts_with("with every command but `info`, ")) << lineFor("fov");

            EXPECT_FALSE(lineFor("validation").starts_with("with ")) << "nothing to say where every command reads it";

            // **A number a line states is the number the code reads**, formatted from it: eight
            // milliseconds, four seconds, sixty frames a second for
            // twenty seconds, 800 units at 69.99 a metre, a film of twenty seconds, 16384 units of
            // 8192-unit cells, and the encoder's own three settings.
            EXPECT_NE(lineFor("hold").find("`check` holds 8 milliseconds unless"), std::string::npos)
                << lineFor("hold");
            EXPECT_NE(lineFor("turn-weather").find("each crossing takes 4 seconds"), std::string::npos);
            EXPECT_NE(lineFor("clock").find("the timescale the session starts at"), std::string::npos)
                << "the game's own speed stated as the session's, which no help line can know";
            EXPECT_NE(lineFor("seconds").find("steps 1/60 of a second"), std::string::npos) << lineFor("seconds");
            EXPECT_NE(lineFor("seconds").find("the 20 seconds nobody named are 1200 frames"), std::string::npos);
            EXPECT_NE(lineFor("speed").find("11 metres a second is a drone"), std::string::npos) << lineFor("speed");
            EXPECT_NE(
                lineFor("length").find("taken out. 20 where neither this nor --speed is named"), std::string::npos)
                << lineFor("length");
            EXPECT_NE(lineFor("cut-distance").find(": 2 exterior cells by default"), std::string::npos);
            EXPECT_NE(lineFor("encode").find("with libx264 at CRF 18 in yuv420p"), std::string::npos);
            EXPECT_EQ(lineFor("warmup").find("forty-five"), std::string::npos) << "the settle it described is gone";
        }

        /// The names the two tables share: an option's owner and the dispatch's row are the same
        /// word for the same command.
        TEST(RtxVerbsTest, everyCommandHasOneName)
        {
            EXPECT_EQ(verbName(Verb::Shot), "shot");
            EXPECT_EQ(verbNamed("shot"), Verb::Shot);
            EXPECT_EQ(verbName(Verb::Check), "check");
            EXPECT_EQ(verbNamed("check"), Verb::Check);
            EXPECT_EQ(verbName(Verb::Film), "film");
            EXPECT_EQ(verbNamed("film"), Verb::Film);
            EXPECT_EQ(verbName(Verb::Noise), "noise");
            EXPECT_EQ(verbNamed("noise"), Verb::Noise);
            EXPECT_EQ(verbName(Verb::Tour), "tour");
            EXPECT_EQ(verbNamed("tour"), Verb::Tour);
            EXPECT_EQ(verbNamed("nonesuch"), std::nullopt);
            EXPECT_EQ(verbNamed(""), std::nullopt);

            // Nine commands, each its own bit: a set holds exactly what it was given, and its complement the
            // rest.
            EXPECT_EQ(VerbSet::every().size(), 9u) << "the nine `--help` prints";
            EXPECT_EQ(VerbSet{}.size(), 0u);
            EXPECT_EQ(VerbSet::every().complement(), VerbSet{});
            EXPECT_EQ(VerbSet{}.complement(), VerbSet::every());
            EXPECT_EQ(VerbSet{ Verb::Shot }.complement().size(), 8u);
            EXPECT_FALSE(VerbSet{ Verb::Shot }.complement().contains(Verb::Shot));
            EXPECT_TRUE(VerbSet{ Verb::Shot }.complement().contains(Verb::Noise));
            EXPECT_TRUE((VerbSet{ Verb::Bench, Verb::Check }.contains(Verb::Check)));
            EXPECT_FALSE((VerbSet{ Verb::Bench, Verb::Check }.contains(Verb::Shot)));
            EXPECT_EQ((VerbSet{ Verb::Check, Verb::Check }), VerbSet{ Verb::Check }) << "a command named twice is one";

            EXPECT_EQ(describeVerbs({ Verb::Shot }), "`shot`");
            EXPECT_EQ(describeVerbs({ Verb::Bench, Verb::Check }), "`bench` and `check`");
            EXPECT_EQ(describeVerbs({ Verb::Check, Verb::Shot, Verb::Scene }), "`scene`, `shot` and `check`")
                << "in the order --help prints them, whatever order they were written in";
            EXPECT_EQ(describeVerbs({}), "");
        }

        /// What each command does with a place is one row: which freeze the world, which fly a
        /// route, which follow a track, which measure without the layers, which hash every frame, which
        /// somebody plays and which run the counting shaders. **Every command has one**, which the table's declaration
        /// holds: a command added to the enum and forgotten there does not compile.
        TEST(RtxVerbsTest, everyCommandHasOneRowOfPolicy)
        {
            const auto row = [](Verb verb) {
                const VerbPolicy& policy = policyOf(verb);
                return std::array{ policy.mFreezes, policy.mFliesRoutes, policy.mFollowsTracks, policy.mMeasures,
                    policy.mHashes, policy.mPlayed, policy.mShaders == VerbShaders::Counting };
            };

            //                                     freezes routes tracks measures hashes played counts
            EXPECT_EQ(row(Verb::Info), (std::array{ false, false, false, false, false, false, true }));
            EXPECT_EQ(row(Verb::Scene), (std::array{ true, false, false, false, false, false, true }));
            EXPECT_EQ(row(Verb::Shot), (std::array{ true, true, false, false, true, false, true }));
            EXPECT_EQ(row(Verb::View), (std::array{ false, false, false, false, false, true, true }))
                << "a window takes no route and keeps the game's interface: somebody is flying it";
            EXPECT_EQ(row(Verb::Bench), (std::array{ false, true, false, true, false, false, false }));
            EXPECT_EQ(row(Verb::Check), (std::array{ true, true, false, false, false, false, true }));
            EXPECT_EQ(row(Verb::Film), (std::array{ false, false, true, true, false, false, false }));
            EXPECT_EQ(row(Verb::Noise), (std::array{ true, false, false, false, false, false, true }))
                << "a reference is many frames of one frame, and a route would make them many";
            EXPECT_EQ(row(Verb::Tour), (std::array{ false, false, true, true, false, false, true }))
                << "the game's frames as a player sees them, without the layers, under the census";

            // Traced at the game's width where a player sees the frames — a window, a bench, a film —
            // and at the reference's wherever a picture may be summed.
            for (const Verb verb : { Verb::Info, Verb::Scene, Verb::Shot, Verb::View, Verb::Bench, Verb::Check,
                     Verb::Film, Verb::Noise, Verb::Tour })
            {
                const bool shown
                    = verb == Verb::View || verb == Verb::Bench || verb == Verb::Film || verb == Verb::Tour;
                EXPECT_EQ(
                    policyOf(verb).radianceWidth(), shown ? Rtx::RadianceWidth::Shown : Rtx::RadianceWidth::Summed)
                    << verbName(verb);
            }
        }
    }

    namespace
    {
        namespace bpo = boost::program_options;

        bpo::variables_map parse(const std::vector<const char*>& arguments)
        {
            bpo::options_description description;
            Files::ConfigurationManager::addCommonOptions(description);

            bpo::variables_map variables;
            bpo::store(bpo::command_line_parser(static_cast<int>(arguments.size()), arguments.data())
                           .options(description)
                           .run(),
                variables);
            bpo::notify(variables);
            return variables;
        }

        std::vector<std::filesystem::path> configDirectories(const bpo::variables_map& variables)
        {
            return Files::asPathContainer(variables.at("config").as<Files::MaybeQuotedPathContainer>());
        }

        /// The directory is created, and it is the last one whatever `--config` named before it —
        /// because the last configuration directory is the one the engine writes into.
        TEST(RtxOwnConfigTest, theDirectoryIsMadeAndComesLast)
        {
            const std::filesystem::path own = TestingOpenMW::currentTestDirPath() / "own-config-test";

            bpo::variables_map bare = parse({ "openmw-rtxtool" });
            adoptConfigDirectory(bare, own);
            EXPECT_TRUE(std::filesystem::is_directory(own)) << "made, because the engine writes into it";
            EXPECT_EQ(configDirectories(bare), std::vector<std::filesystem::path>{ own });

            bpo::variables_map named = parse({ "openmw-rtxtool", "--config", "/one", "--config", "/two" });
            adoptConfigDirectory(named, own);
            EXPECT_EQ(configDirectories(named), (std::vector<std::filesystem::path>{ "/one", "/two", own }))
                << "after what the command line named, so it is the one the engine saves into";
        }

        /// **A run's own directory is named for this process**: under `rtxtool`, `<time>-<process id>`, with its
        /// saves inside it.
        TEST(RtxOwnConfigTest, aRunsDirectoryIsItsOwn)
        {
            const Files::ConfigurationManager config;
            const std::string run = nameThisRun();
            EXPECT_TRUE(run.ends_with(std::format("-{}", Platform::Process::currentId()))) << run;
            const std::string time = run.substr(0, run.find('-'));
            EXPECT_FALSE(time.empty()) << run;
            EXPECT_EQ(time.find_first_not_of("0123456789"), std::string::npos) << run;

            const std::filesystem::path own = ownConfigDirectory(config, run);
            EXPECT_EQ(own, config.getCachePath() / "rtxtool" / run);
            EXPECT_EQ(ownSavesFolder(own), own / "saves") << "a run's saves outside its own directory";
        }

        /// **Everything but a live run's directory is swept**: process nought, which no system hands
        /// out, has ended, and this one has not. A name that is not `<time>-<process id>` whole — the hex
        /// an earlier build named its runs in, a letter past the number, no time, no process — is left
        /// over, and so is a file whatever its name, as the layout before runs had folders left them.
        TEST(RtxOwnConfigTest, everythingButALiveRunsDirectoryIsSwept)
        {
            const std::filesystem::path runs = TestingOpenMW::currentTestDirPath();
            const std::string running = std::format("6-{}", Platform::Process::currentId());
            for (const std::string& name :
                std::vector<std::string>{ "5-0", running, "18e6dd74d457d5e1-1e241e", "7-12x", "-3", "8-", "crashes" })
                std::filesystem::create_directories(runs / name / "settings");
            std::ofstream(runs / "settings.cfg") << "a file";
            std::ofstream(runs / std::format("9-{}", Platform::Process::currentId())) << "a file named as a live run";

            sweepAllButLiveRuns(runs);

            EXPECT_TRUE(std::filesystem::exists(runs / running / "settings")) << "the live run was swept";
            std::size_t left = 0;
            for ([[maybe_unused]] const std::filesystem::directory_entry& entry :
                std::filesystem::directory_iterator(runs))
                ++left;
            EXPECT_EQ(left, 1u) << "only the live run stays";

            // A relative folder is the working directory's, which is no folder of runs.
            Rtx::Testing::expectDies(
                [] { sweepAllButLiveRuns("rtxtool"); }, "the runs' folder is not an absolute path");
        }
    }
}
