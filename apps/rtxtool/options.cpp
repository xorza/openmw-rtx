#include "options.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <boost/program_options/option.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>

#include <osg/Vec3f>

#include <apps/rtxtool/instruments/wholenumber.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/benchspec.hpp>
#include <apps/rtxtool/model/blockfile.hpp>
#include <apps/rtxtool/model/film.hpp>
#include <apps/rtxtool/model/finding.hpp>
#include <apps/rtxtool/model/skycrossing.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/result.hpp>
#include <components/platform/folder.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/surfaceview.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/weather.hpp>

#include "framed.hpp"
#include "noise.hpp"
#include "numbervalue.hpp"
#include "verbs.hpp"

namespace bpo = boost::program_options;

namespace RtxTool
{
    namespace
    {
        /// How long a bench draws and throws away once the world stands whole, before it measures, where nobody says.
        /// Two rather than three, because the GPU's clock ramp and the scene's residency are over well inside it:
        /// measured interleaved on a hot card, three seconds ran 20 s and two ran 19.
        constexpr float sWarmupByDefault = 2.0f;

        using StringsVector = std::vector<std::string>;

        /// How each of `sReconstructionSwitches` is written into a request, in that order: the one rule
        /// the line's switches and the one `--versus` names are both read by.
        struct SwitchReader
        {
            std::string_view mName;
            void (*mRead)(const bpo::variable_value& value, Rtx::ReconstructionRequest& request);
        };

        using Request = Rtx::ReconstructionRequest;
        using Value = bpo::variable_value;

        constexpr std::array<SwitchReader, sReconstructionSwitches.size()> sSwitchReaders{ {
            { "filter", [](const Value& value, Request& request) { request.mDenoise = value.as<bool>(); } },
            { "jitter", [](const Value& value, Request& request) { request.mJitter = value.as<bool>(); } },
            { "noise",
                [](const Value& value, Request& request) {
                    const std::string& noise = value.as<std::string>();
                    request.mNoise
                        = noise == "auto" ? Request{}.mNoise : Rtx::sNoiseSourceNames.require(noise, "a noise source");
                } },
            { "level-epsilon",
                [](const Value& value, Request& request) { request.mLevelEpsilon = value.as<float>(); } },
            { "shadow-floor",
                [](const Value& value, Request& request) { request.mSampling.mShadowFloor = value.as<float>(); } },
            { "lamp-candidates",
                [](const Value& value, Request& request) {
                    request.mSampling.mLampCandidates = value.as<std::uint32_t>();
                } },
            { "indirect",
                [](const Value& value, Request& request) { request.mSampling.mIndirect = value.as<bool>(); } },
            { "bounce-rays",
                [](const Value& value, Request& request) {
                    request.mSampling.mBounceRays = value.as<std::uint32_t>();
                } },
            { "relief", [](const Value& value, Request& request) { request.mSampling.mRelief = value.as<bool>(); } },
            { "soft-edge-dither",
                [](const Value& value, Request& request) {
                    const std::string& dither = value.as<std::string>();
                    if (dither != "auto" && dither != "true" && dither != "false")
                        throw std::runtime_error("--soft-edge-dither takes auto, true or false, not " + dither);
                    request.mSoftEdgeDither = dither == "auto" ? std::nullopt : std::optional(dither == "true");
                } },
            { "water-interleave",
                [](const Value& value, Request& request) { request.mSampling.mWaterInterleave = value.as<bool>(); } },
            { "antilag", [](const Value& value, Request& request) { request.mFilters.mAntilag = value.as<bool>(); } },
            { "history-fix",
                [](const Value& value, Request& request) { request.mFilters.mHistoryFix = value.as<bool>(); } },
            { "dual-motion",
                [](const Value& value, Request& request) { request.mFilters.mDualMotion = value.as<bool>(); } },
            { "antifirefly",
                [](const Value& value, Request& request) { request.mFilters.mAntiFirefly = value.as<bool>(); } },
        } };

        constexpr bool readsEverySwitch()
        {
            for (std::size_t at = 0; at < sSwitchReaders.size(); ++at)
                if (sSwitchReaders[at].mName != sReconstructionSwitches[at])
                    return false;
            return true;
        }
        static_assert(readsEverySwitch(), "a switch `--versus` may name that no reader writes, or one out of order");

        /// Parses `WIDTHxHEIGHT`.
        FrameSize parseSize(const std::string_view text)
        {
            const std::size_t cross = text.find('x');
            const std::optional<std::uint32_t> width
                = cross != std::string_view::npos ? wholeNumber<std::uint32_t>(text.substr(0, cross)) : std::nullopt;
            const std::optional<std::uint32_t> height
                = cross != std::string_view::npos ? wholeNumber<std::uint32_t>(text.substr(cross + 1)) : std::nullopt;

            if (!width.has_value() || !height.has_value() || *width == 0 || *height == 0)
                throw std::runtime_error(std::format("--size={} is not WIDTHxHEIGHT", text));

            return FrameSize{ .mWidth = *width, .mHeight = *height };
        }

        /// What `--exposure` asked for: a number to hold it at, or nothing to measure it.
        Rtx::ExposureRule parseExposure(const std::string_view text)
        {
            // **Settled at every stop**, where the game opens from a day: a stop starts with no past
            // and warms up for less time than an eye takes to open in a room, and a measured picture
            // is of the eye that place settles on.
            if (text == "auto")
                return Rtx::MeasuredExposure{ .mStart = Rtx::EyeStart::Settled };

            const std::optional<float> value = wholeNumber<float>(text);
            if (!value.has_value() || !(*value > 0.0f))
                throw std::runtime_error(std::format("--exposure={} is neither auto nor a number over nought", text));

            return Rtx::FixedExposure{ *value };
        }

        /// The weather the line names, read as a view file's is; refused with the option that named
        /// it where it is none of the ten.
        Rtx::Weather weatherNamed(const std::string_view option, const std::string_view weather)
        {
            const std::optional<Rtx::Weather> named = Rtx::weatherNamed(weather);
            if (!named.has_value())
                throw std::runtime_error(
                    std::format("--{}: \"{}\" {}: {}", option, weather, checkWeather(weather).error(), listWeathers()));
            return *named;
        }

        /// The air `text` names, or nothing where it names none. Text that names no air is refused,
        /// quoted whole.
        std::optional<Rtx::AirClock> airNamed(const std::string& text)
        {
            if (text.empty())
                return std::nullopt;

            const std::optional<Rtx::AirClock> air = parseAir(text);
            if (!air.has_value())
                throw std::runtime_error(
                    std::format("--air is not the sky's seconds from nought, the deck's scroll "
                                "from nought up to but not including four, and the drift's two "
                                "coordinates, separated by commas: \"{}\"",
                        text));

            return air;
        }

        /// The point `--<name>` names in `text`, or nothing where it names none. Text that names no
        /// point is refused, quoted whole.
        std::optional<osg::Vec3f> pointNamed(const std::string_view name, const std::string& text)
        {
            if (text.empty())
                return std::nullopt;

            const std::optional<osg::Vec3f> point = parseVec3(text);
            if (!point.has_value())
                throw std::runtime_error(
                    std::format("--{} is not three numbers separated by commas: \"{}\"", name, text));

            return point;
        }

        /// The commands that can stand at one place named on the line — a cell, a camera, a view.
        /// A run of places — `bench` and `check`, and `shot` and `scene` under `--views` — takes
        /// its cells and its cameras from `views.cfg` instead.
        constexpr VerbSet sPlaces{ Verb::Scene, Verb::Shot, Verb::View };

        /// The commands that visit a list of places.
        constexpr VerbSet sRuns{ Verb::Scene, Verb::Shot, Verb::Bench, Verb::Check, Verb::Noise };

        /// The commands that frame the world, which is every one that builds a `Framed`.
        /// `info` is the one that does not: it reports on a device and draws nothing.
        constexpr VerbSet sFramed = VerbSet{ Verb::Info }.complement();

        /// The commands that stand under one sky the line names. A film's keys each name their own, and a tour turns
        /// through every weather on a clock of its own.
        constexpr VerbSet sOneSky = VerbSet{ Verb::Info, Verb::Film, Verb::Tour }.complement();

        /// How long a place runs where nobody says. Twenty because ten left the CPU medians moving
        /// by more than the changes being measured.
        constexpr float sSecondsByDefault = 20.0f;

        /// Which commands read an option, said the shorter of the two ways: or the ones that do not, where that is
        /// the shorter list, since naming eight commands to exclude one is a line nobody reads to the end. The help
        /// line and the complaint say it the same way, because they are the same fact before and after somebody gets
        /// it wrong.
        std::string describeReaders(const VerbSet verbs)
        {
            const VerbSet missing = verbs.complement();
            if (missing.size() < verbs.size())
                return std::format("every command but {}", describeVerbs(missing));

            return describeVerbs(verbs);
        }

        /// One option's help line, opening with the commands that read it.
        ///
        /// An option every command reads opens with nothing at all: there is no command it would
        /// be telling the reader about.
        std::string describeOption(const VerbSet verbs, const std::string_view description)
        {
            if (verbs == VerbSet::every())
                return std::string(description);

            return std::format("with {}, {}", describeReaders(verbs), description);
        }
    }

    Rtx::ReconstructionRequest ToolOptions::versus(
        const Rtx::ReconstructionRequest& played, const std::string_view asked) const
    {
        const std::string_view name = asked.substr(0, asked.find('='));
        const auto reader = std::ranges::find(sSwitchReaders, name, &SwitchReader::mName);
        if (reader == sSwitchReaders.end())
        {
            std::string switches;
            for (const std::string_view known : sReconstructionSwitches)
                switches += std::format("{}{}", switches.empty() ? "" : ", ", known);
            throw std::runtime_error(
                std::format("--versus={} names no switch of the reconstruction, which are {}", asked, switches));
        }

        // Read as the line reads it, so a value is refused or taken by the switch's own rule.
        bpo::variables_map read;
        try
        {
            bpo::store(bpo::command_line_parser(std::vector<std::string>{ "--" + std::string(asked) })
                           .options(mDescription)
                           .run(),
                read);
        }
        catch (const bpo::error& refused)
        {
            throw std::runtime_error(std::format("--versus={}: {}", asked, refused.what()));
        }

        Rtx::ReconstructionRequest request = played;
        reader->mRead(read[std::string(name)], request);
        return request;
    }

    VerbSet ToolOptions::readsOption(const std::string_view name) const
    {
        for (const OptionOwner& owner : mOwners)
        {
            if (owner.mName == name)
                return owner.mVerbs;
        }

        return VerbSet::every();
    }

    std::string ToolOptions::complainAbout(const bpo::parsed_options& line, const Verb verb) const
    {
        std::string complaint;
        std::vector<std::string_view> said;

        for (const bpo::option& given : line.options)
        {
            const VerbSet reads = readsOption(given.string_key);
            if (reads.contains(verb))
                continue;

            // A composing option arrives once per time it was written, and a run that named three
            // creatures is not owed three complaints about `--actor`.
            if (std::find(said.begin(), said.end(), given.string_key) != said.end())
                continue;

            said.push_back(given.string_key);
            complaint += std::format("`{}` does not read --{}, which belongs to {}.\n", verbName(verb),
                given.string_key, describeReaders(reads));
        }

        return complaint;
    }

    ToolOptions makeOptions(const Rtx::ValidationLevel validationByDefault)
    {
        ToolOptions result{ bpo::options_description("Options"), {} };
        auto declare = result.mDescription.add_options();

        // Every option says which commands read it, and there is no second door that lets one skip the question:
        // the record and the help line come from that one statement, so a run that names an option under a command
        // that does not read it is stopped rather than quietly rendering something else.
        const auto option
            = [&](VerbSet verbs, const char* name, const bpo::value_semantic* semantic, std::string_view description) {
                  result.mOwners.push_back(OptionOwner{ .mName = name, .mVerbs = verbs });
                  declare(name, semantic, describeOption(verbs, description).c_str());
              };

        // What a frame is when nobody says, read from one statement rather than restated as a literal beside each
        // option, which drifted (`--distant-cells` defaulted to five cells where the request and
        // `settings-default.cfg` said four). The knobs the settings define, the distant cells and the statics on
        // them, have no default here at all: one not given reads `settings-default.cfg` once the settings are loaded
        // (`frameFrom`).
        const Framed byDefault;

        option(VerbSet::every(), "help", bpo::bool_switch(), "print this message and quit");

        option(VerbSet::every(), "validation",
            bpo::value<std::string>()->default_value(std::string(Rtx::sValidationNames.name(validationByDefault))),
            std::format("which of VK_LAYER_KHRONOS_validation's checks to load: {}. `on` is the core "
                        "checks; `sync` adds synchronization validation, which catches a missing "
                        "barrier; `gpu` adds GPU-assisted validation instead, which instruments every "
                        "shader at about half the frame rate, and which the layer asks not to run "
                        "beside the core checks. `sync` by default outside a Release build and `off` "
                        "in one; a run that names a level and cannot have it fails rather than "
                        "reporting nothing",
                Rtx::sValidationNames.list())
                .c_str());

        option(sPlaces, "cell", bpo::value<std::string>()->default_value(""),
            "cell to read, addressed the way Morrowind does: a pair of integers is an exterior, "
            "anything else is an interior's name. Write --cell=-2,-9 rather than --cell -2,-9, or "
            "the leading minus reads as an option. Left out, the default view decides.");

        option(sPlaces, "view", bpo::value<std::string>()->default_value(""),
            "a named viewpoint from rtxtool/views.cfg, which supplies the cell and usually the "
            "camera. Overrides --cell. A run of places names them with --views instead.");

        option(VerbSet::every(), "list-views", bpo::bool_switch(), "print the named viewpoints and quit");

        option({ Verb::Info }, "folders", bpo::bool_switch(),
            "print where the game reads its openmw.cfg and where it keeps its saves, one to a line as "
            "`config <path>` and `data <path>`, and quit, before any device: the answer `Files` gives "
            "on this system, which the driver's `setup` and `game` read rather than restating");

        option(sFramed, "filter",
            bpo::value<bool>()
                ->default_value(byDefault.mSetup.mRun.mProfile.mReconstruction.mDenoise)
                ->implicit_value(true),
            "run the denoisers over the light. Off shows the raw light, and is what a reference is "
            "made with");
        option({ Verb::Shot }, "doll", bpo::value<std::string>()->default_value(""),
            "also write the inventory doll of this person, by NPC record id -- fargoth, \"caius "
            "cosades\" -- traced against a scene of their own. They arrive dressed out of their "
            "own record, which is what the game equips them with. `scene --find=<text>` finds one");
        option({ Verb::Shot }, "map", bpo::bool_switch(),
            "also write one local-map tile of the place, traced straight down and framed the way "
            "the game's own compass frames one");
        option({ Verb::Shot }, "textures", bpo::bool_switch(),
            "also write every texture the world around the place uses, vanilla beside de-lit, as "
            "one sheet: what a frame there would actually sample, since a town's people wear "
            "textures the town itself never names");

        option(VerbSet{ Verb::Info, Verb::Film }.complement(), "upscale",
            bpo::value<std::string>()->default_value(
                std::string(Rtx::sUpscaleNames.name(byDefault.mSetup.mRun.mProfile.mUpscale))),
            std::format("put the upscaler, FSR 3.1, between the trace and the picture: {}. --size is "
                        "what comes out, and what gets traced is --size over the mode's ratio. `{}` "
                        "by default. `film` is always `{}`",
                Rtx::sUpscaleNames.list(), Rtx::sUpscaleNames.name(sUpscaleByDefault),
                Rtx::sUpscaleNames.name(sFilmUpscale))
                .c_str());

        option(sFramed, "upscaler",
            bpo::value<std::string>()->default_value(
                std::string(Rtx::sUpscalerNames.name(byDefault.mSetup.mRun.mProfile.mUpscaler))),
            std::format("reconstruction backend: {}. `{}` by default for repeatable measurements",
                Rtx::sUpscalerNames.list(), Rtx::sUpscalerNames.name(byDefault.mSetup.mRun.mProfile.mUpscaler))
                .c_str());

        option(sFramed, "exposure", bpo::value<std::string>()->default_value("auto"),
            "what to scale the frame by before the display curve: auto measures it off the frame, "
            "and a number holds it there. A pixel test and a converged reference want it held, "
            "because a measured exposure makes every value depend on the whole frame");

        option(sFramed, "show", bpo::value<std::string>()->default_value("shaded"),
            std::format("what every pixel is painted with: {}. shaded is the light; the rest write one "
                        "input of the surface with no shading over it — the albedo, the shading normal as "
                        "0.5 + 0.5n, the perceptual roughness, the reflectance at normal incidence — which is "
                        "what a texture or a map problem looks like when nothing else is in the way",
                Rtx::sSurfaceViewNames.list())
                .c_str());

        option(sOneSky, "weather", bpo::value<std::string>()->default_value(std::string(Rtx::nameOf(sDefaultWeather))),
            std::format("which weather's sun, sky and precipitation an exterior stands under, named as the "
                        "content files spell it: {}. The ones that drop something drop it here too. Given, "
                        "it beats a weather a view fixes for itself",
                listWeathers())
                .c_str());

        option({ Verb::Bench, Verb::Film }, "turn-weather", bpo::value<std::string>()->default_value(""),
            std::format("turn the sky through these weathers, comma separated and round again — "
                        "--turn-weather=Rain,Foggy. Under `bench`, while each place runs: each crossing takes {} "
                        "seconds of world in place of the weather's own Transition_Delta, a minute for most, "
                        "and the next is asked for as one lands, so the precipitation of the one arriving "
                        "replaces the one leaving halfway through every crossing. **A run under it is not a "
                        "benchmark**: no two places stand under the same sky. It is here because a weather "
                        "turning frees a whole emitter's meshes and textures on an ordinary frame, which is "
                        "the one thing the game does constantly that no other path in this tool could do. "
                        "Under `film`, over the whole film whatever the keys name: each weather stands for "
                        "--weather-hold and crosses into the next in --crossing, while the camera flies the "
                        "keys at its own pace",
                sTurnSeconds));

        option(sOneSky, "hour", number(between(0.0f, 24.0f, true))->default_value(sDefaultHour),
            "what time an exterior's sun is at, on a twenty-four hour clock. An interior is lit "
            "by its own lamps and does not care. Given, it beats an hour a view fixes for itself");

        option(sOneSky, "air", bpo::value<std::string>()->default_value(""),
            "where the clocks the air runs on stand: the sky's seconds, the deck's scroll, and how far "
            "the fog was carried east and north, as four numbers separated by commas. What Home "
            "prints, so the fog a window showed is the fog a picture is taken in; left out, it is "
            "wherever the run's own frames carried it, and a run started fresh stands in one fog "
            "whatever the window saw. Given, it beats an air a view fixes for itself");

        option(sFramed, "day", number(atLeast(0))->default_value(byDefault.mDay),
            "which day the world stands on, counted from the one a new game starts — 16 Last Seed, "
            "where both moons are full. It is the moons this decides and nothing else: their phase "
            "runs on a three-day cycle and the hour they rise on a twenty-four day one. A film's key "
            "that names a day of its own keeps it. In a `view`, Insert and Delete step it a day on and "
            "back");

        option({ Verb::View, Verb::Bench }, "frames",
            number(between(0u, BenchSpan::sUntilClosed, true))->default_value(0),
            "how many frames to run: `view` closes after this many instead of waiting to be "
            "closed, and `bench` measures this many at each place instead of deriving them from "
            "--seconds");

        option({ Verb::Bench, Verb::Check, Verb::Noise }, "suite", bpo::value<std::string>()->default_value(""),
            "which list of places in rtxtool/benches.cfg to visit: [default] for `bench`, "
            "[check] for `check` and [noise] for `noise` unless named. Overridden by --views");

        option({ Verb::Noise }, "strafe", number(atLeast(0.0f))->default_value(0.0f),
            std::format("how many world units to the side the frame starts: it flies into the place over "
                        "the last {} frames, facing it, through a world as still as the reference's, so the "
                        "frame is taken after a history the eye moved through, and held to as many frames "
                        "averaged as that history could hold samples a shown pixel. Nought takes it standing "
                        "still",
                sNoiseFlightFrames));

        option({ Verb::Noise }, "walk", number(anyNumber<float>())->default_value(0.0f),
            "how many world units behind the place the frame starts, as --strafe does to the side: it "
            "walks in along the level of its facing, and a negative distance starts in front and walks "
            "back. Adds to --strafe, which it flies in beside");

        option({ Verb::Noise }, "cut", number(anyNumber<std::uint32_t>())->default_value(0),
            "how many frames after the cut a stop begins with the frame is taken, standing: the first "
            "frames after a door, a load or a teleport, where a history is too short to tell a rare "
            "bright bounce from the light. The frame holds this many and one more, since the frame the "
            "cut resets draws the world unmeasured, and is held to as many frames averaged as that "
            "history could hold samples a shown pixel. Nought takes it after its history converged; not "
            "with --strafe or --walk");

        option({ Verb::Noise }, "versus", bpo::value<std::string>()->default_value(""),
            "a second side in the same run: <switch>=<value> takes the frame and its mean again with that "
            "one switch of the reconstruction changed, at the same draws, and judges both. The bar is "
            "the first side's where the switch changes nothing an unfiltered frame reads, and the "
            "reference where it changes nothing the truth traces; each its own where it does. What "
            "`omw noise --ab` runs");

        option(sRuns, "views", bpo::value<std::string>()->default_value(""),
            "which views.cfg views to visit, comma separated, by name rather than by suite. "
            "--views=all runs every view there is, which with `shot --against` is what says what "
            "a change moved");

        option({ Verb::Bench, Verb::Check }, "seconds", number(atLeast(0.0f, true))->default_value(sSecondsByDefault),
            std::format("how many seconds of world to run at each place. World and not wall: the world "
                        "steps 1/{} of a second per frame however long the frame took, so the {} seconds "
                        "nobody named are {} frames either way, and two builds render the same frames",
                sStepRate, sSecondsByDefault, sStepRate * sSecondsByDefault));

        option({ Verb::Bench }, "warmup", number(atLeast(0.0f))->default_value(sWarmupByDefault),
            "how many seconds of world to draw and throw away once the world stands whole, before "
            "measuring. A card's clock idles low and ramps under load, and a scene's first frames pay "
            "for its residency as well. Every other command waits out its histories "
            "instead, four of the accumulator's lengths");

        option(sFramed, "spell-hands", bpo::bool_switch(),
            "ready the saved player's selected spell instead of putting their hands away, at a stop "
            "that names no weapon");
        option(sFramed, "hud", bpo::value<bool>()->default_value(false)->implicit_value(true),
            "draw the game's HUD over the picture: the bars, the compass and the cell's name. Off "
            "unless asked for, and a window's F11 toggles it either way. Off, a command that writes "
            "a picture draws nothing of the interface at all, so a message or a mod's window a "
            "script opens stays out of it; a window keeps the game's menus and console");

        option(sFramed, "vanity", bpo::value<bool>()->default_value(false)->implicit_value(true),
            "let the game's vanity camera orbit the player after thirty idle seconds, as the played "
            "game does. Off unless asked for: a run is idle by nature");

        option(sFramed, "night-eye", number(between(0, 100))->default_value(0),
            "the magnitude of Night-Eye the player wears at every place, as a potion or the Khajiit's "
            "power puts it on them: the game lifts every lit surface by 0.7 of it over a hundred");

        option(sFramed, "memory-budget",
            number(between<std::uint64_t>(1, std::numeric_limits<std::uint64_t>::max() >> 20)),
            "run as though the card's video memory budget were this many MiB, where it is more: "
            "what a smaller card does with the place. Textures and structures stop where they "
            "would stop there, textures coming down to a smaller side first, and what still does "
            "not fit is refused and reported as the card would refuse it. What the frame itself "
            "holds is never refused. Not given, the card's own budget");

        option(sFramed, "pipeline-code", bpo::value<std::string>()->default_value(""),
            "write what the driver compiled each pipeline to, as text, to <dir>/<pipeline>.txt, "
            "numbered where pipelines share a name: on "
            "RADV its NIR, its compiler's IR and the instructions the card runs, which a profiler's "
            "instruction view shows. Every pipeline is then compiled past the driver's cache. "
            "NVIDIA's driver gives no such text, and nothing is written");

        option(sFramed, "content-cache", bpo::value<std::string>()->default_value("game"),
            "where the content's derived data is found and kept across runs (`ContentStore::Store`): "
            "game, the store the game keeps under the user's cache directory, so a run reads what the "
            "game wrote; off, none, so every pass runs; or a directory of the run's own");

        option({ Verb::Bench, Verb::Tour }, "window", bpo::value<bool>()->default_value(true)->implicit_value(true),
            "show the run while it happens. The swapchain is mailbox, so it does not "
            "pace the loop; --window=false is one fewer thing between the trace and the number. A tour's window "
            "titles the leg, the cell and the findings so far, and Home marks where the watcher is looking");

        option(sFramed, "variants", bpo::value<bool>()->default_value(true)->implicit_value(true),
            "keep one launch per tuple of the frame's facts, the sun, the moons and the sea; "
            "--variants=false makes the full tuple's launch alone and traces every frame with it, "
            "the same picture to a rounding by lib/variants.glsl's argument, for measuring what the "
            "tuples are worth against the sixteen launches the driver compiles for them");

        option(VerbSet::every(), "shader-source", bpo::value<bool>()->default_value(false)->implicit_value(true),
            "hand the driver the shaders with their source in them, for a profiler that shows a "
            "shader's lines. The driver keys its cache on the modules it is handed, so these keep a "
            "cache of their own, apart from the one every other run reads");

        option(sFramed, "noise", bpo::value<std::string>()->default_value("auto"),
            std::format("where the trace's per-pixel draws come from: auto, {}. Auto is the tile, "
                        "which the wavelet is built to read and Ray Reconstruction measured cleaner "
                        "on. Naming one is the A/B",
                Rtx::sNoiseSourceNames.list())
                .c_str());

        option(sFramed, "antilag",
            bpo::value<bool>()
                ->default_value(byDefault.mSetup.mRun.mProfile.mReconstruction.mFilters.mAntilag)
                ->implicit_value(true),
            "hold the denoiser's slow mean of the bounce to its fast one, so a change of the light on "
            "a surface that did not move is followed and not dragged. Off is the A/B");

        option(sFramed, "history-fix",
            bpo::value<bool>()
                ->default_value(byDefault.mSetup.mRun.mProfile.mReconstruction.mFilters.mHistoryFix)
                ->implicit_value(true),
            std::format("rebuild the denoiser's mean of the bounce, where it holds {:g} frames or fewer, from "
                        "the surface around it, so what the eye uncovers shows the light beside it and not one "
                        "bounce spread into blotches. Off is the A/B",
                Rtx::Shaders::ACCUMULATE_FIX_FRAMES)
                .c_str());

        option(sFramed, "dual-motion",
            bpo::value<bool>()
                ->default_value(byDefault.mSetup.mRun.mProfile.mReconstruction.mFilters.mDualMotion)
                ->implicit_value(true),
            "where the previous frame did not see a surface, take the denoiser's history of the bounce "
            "along the motion of what hid it, so what the eye uncovers starts with the history of the "
            "surface beside it (Zeng et al. 2021's dual motion vector). Off is the A/B");

        option(sFramed, "antifirefly",
            bpo::value<bool>()
                ->default_value(byDefault.mSetup.mRun.mProfile.mReconstruction.mFilters.mAntiFirefly)
                ->implicit_value(true),
            std::format("hold each sample of the bounce under {:g} times the frame's white, dropping what it carried "
                        "past that, and the denoiser's mean of the bounce, where it holds {:g} frames or fewer, under "
                        "the light around it, so a bounce that found a small bright thing is not a speck. Off is the "
                        "A/B",
                Rtx::Shaders::ACCUMULATE_FIREFLY_CEILING, Rtx::Shaders::ACCUMULATE_RING_FRAMES)
                .c_str());

        option(sFramed, "level-epsilon", number(anyNumber<float>())->default_value(0.0f),
            "levels added to the texture level bias past the ratio the upscaler sets, negative for "
            "sharper. Nought is the ratio alone, and off the upscaler nothing is biased");

        option(sFramed, "shadow-floor", number(between(0.0f, 0.25f))->default_value(Rtx::Shaders::SHADOW_DRAW_FLOOR),
            "the share of a pixel's light under which a sky source, or the sky against the lamps, is never "
            "the one its shadow bit is drawn from, and over all but which a bit's own source makes the bit "
            "exact rather than drawn. Its light rides the drawn source's bit");

        option(sFramed, "indirect",
            bpo::value<bool>()
                ->default_value(byDefault.mSetup.mRun.mProfile.mReconstruction.mSampling.mIndirect)
                ->implicit_value(true),
            "trace the bounces, or stand the record's ambient on the surface: the player's "
            "`[RTX] indirect lighting`");

        option(sFramed, "bounce-rays",
            number(between<std::uint32_t>(1, Rtx::Shaders::MAX_BOUNCE_RAYS))
                ->default_value(Rtx::SamplingSwitches{}.mBounceRays),
            "how many bounce rays the eye's surface traces a frame, each in a direction of its own and "
            "worth its share of the bounce: the player's `[RTX] bounce rays`");

        option(sFramed, "relief",
            bpo::value<bool>()->default_value(Rtx::SamplingSwitches{}.mRelief)->implicit_value(true),
            "march an `_nh` map's relief through the heights it holds, or shift it once as the rasterizer does: "
            "the A/B of the march, which the game always takes");

        option(sFramed, "soft-edge-dither", bpo::value<std::string>()->default_value("auto"),
            "meet a soft edge's texels under the cut by their alpha in a world's frames, a draw right only on "
            "average, or cut them as a picture does: auto, true or false. Auto is what the filter wants: the "
            "dither under the renderer's, the cut under an upscaler that filters the light itself");

        option(sFramed, "water-interleave",
            bpo::value<bool>()->default_value(Rtx::SamplingSwitches{}.mWaterInterleave)->implicit_value(true),
            "blend a split water pixel half with its four split neighbours, which followed the other path, or "
            "leave it on its own path at twice its share: the A/B, which the game always takes");

        option(sFramed, "lamp-candidates",
            number(atLeast<std::uint32_t>(0))->default_value(Rtx::Shaders::LAMP_CANDIDATES),
            "how many lamp candidates a shading point that composes its light — a path's far end, a pane, a "
            "water leg — draws from its cell where the cell holds more, resampled to the one it traces. "
            "Nought walks every lamp, which the eye's own surface always does");

        option(sFramed, "hold", bpo::value<bool>()->default_value(false)->implicit_value(true),
            std::format("hold the queue behind the host after every frame's trace, as `check` does: `check` holds "
                        "{} milliseconds unless told --hold=false. The other leg of `repeat` runs under it, and a "
                        "`shot --against` its own unheld pictures is the same question of a still: a picture that "
                        "is a function of the frames alone comes out the same however far the device trails, and "
                        "one that read the clock does not. One hold and not a number: nothing holds the card longer "
                        "than a frame, and a hold of seconds is the desktop frozen for every one of them",
                sCheckHoldMs));

        option({ Verb::Bench }, "counters", bpo::value<bool>()->default_value(false)->implicit_value(true),
            "also read the device's own counters around every zone — instructions run, how busy the "
            "vector and scalar units were, how the caches hit — and report each zone's median under its "
            "times: what a profiler shows of a dispatch, where none reads the device. As many as one "
            "pass reads, which the log names. The queries add work to every zone, and RADV holds the "
            "card at the kernel's profiling clock while it reads them, so a run with them is not the run "
            "its times are quoted from. Where the driver offers none, nothing is read");

        option({ Verb::Bench }, "json", bpo::value<std::string>()->default_value(""),
            "also write the run to this file as one record, for comparing against the "
            "same run on another commit");

        option({ Verb::Bench }, "frame-times", bpo::value<std::string>()->default_value(""),
            "also write every measured frame's figures, a frame a line and a row of the report a "
            "column, to <dir>/<place>.txt: the series the report summarises, for whether a tail "
            "is one mode or two and how far apart the frames that make it are");

        option({ Verb::Bench }, "perf-control", bpo::value<std::string>()->default_value(""),
            "turn a `perf record --delay=-1 --control=fifo:<path>` on around each "
            "place's measured frames, so the profile holds those frames and not the cell being "
            "loaded either side of them. `omw profile` passes this");

        option({ Verb::Shot }, "repeat", number(atLeast<std::uint32_t>(1))->default_value(8),
            "trace the frame this many times and report the best. One submit times "
            "the GPU's clock rather than the shader; a comparison worth making wants hundreds");

        option({ Verb::Scene }, "find", bpo::value<std::string>()->default_value(""),
            "print where every placement wearing a texture whose path contains this stands. A mesh "
            "keeps no name of its own once it is a run of triangles, so the material it arrived "
            "wearing is what it is found by. How the coordinates in a view are found.");

        option(sFramed, "distant-statics", bpo::value<bool>()->implicit_value(true),
            "stand on the distant ground what the content files put there — the buildings, trees "
            "and rocks — as instances of their templates, read ahead of the eye on a thread of the "
            "renderer's own (`Rtx::CellRing`); it is the game's `object paging` setting, which "
            "this renderer never pages by. **Off is the A/B that says what they cost**: the same "
            "ground with nothing on it. The ground itself always stands, read off the land records "
            "by the same ring. Not given, `settings-default.cfg`'s `[Terrain] object paging`, or "
            "the player's own under `view`");

        option(sFramed, "grass", bpo::value<bool>()->implicit_value(true),
            "stand the plants the `--groundcover` files place, each an instance of its model, within "
            "`[Groundcover] rendering distance` of the eye; it is the game's `[Groundcover] enabled`. "
            "**On is the A/B that says what they cost**: the same world with grass on it. Not given, "
            "`settings-default.cfg`'s, which is off, or the player's own under `view`");

        option(sFramed, "gamma", number(atLeast(0.0f, true)),
            "the player's display gamma over the finished picture, after the display curve: each "
            "encoded value to the power of one over this, so more than one lifts the darks and black "
            "and white stay. Not given, `settings-default.cfg`'s `[Video] gamma`, which is one and "
            "leaves the picture as the curve wrote it, or the player's own under `view`");

        option(sFramed, "distant-cells", number(between(0.0f, 10.0f)),
            "how far out the cell ring stands ground and statics, in cells. Outside the active grid "
            "a cell's layer stack is flattened into one baked texture, so this is also how many "
            "cells that path is reached for. Zero hands `viewing distance` back the decision, which "
            "is 7168 against a cell of 8192 and so barely leaves the active grid. Not given, "
            "`settings-default.cfg`'s `[RTX] distant land cells`, or the player's own under `view`");

        option({ Verb::Shot, Verb::Bench }, "against", bpo::value<std::string>()->default_value(""),
            "what to subtract this run from: the directory a previous `shot` or `bench --out` wrote, "
            "the `hashes.csv` in it, which says which frames of the run now trace "
            "something else, which frames now draw something else, and which parts of the scene "
            "moved. The reference is always a run of the previous build on this machine and never "
            "a corpus in the tree: the picture is a function of the driver and the card as much as "
            "of the code. The verdict is the trace's own images, what the frame handed the "
            "reconstruction, and the scene, which two runs of one build compute to the bit; the "
            "picture is the verdict only where nothing upscaled it, because an upscaler keeps "
            "a history and the smallest difference handed to it on one frame stays in its picture "
            "for the rest of the run, so that picture is reported beside the verdict and never as "
            "one. Under the filter the composed frame is the wavelet's, whose last bit is the "
            "card's and not the tree's (`docs/architecture.md`): a difference there alone is "
            "reported beside the verdict, and `shot` holds that picture to within one level of "
            "255, as it holds every doll and map tile, which are denoised whatever the filter. "
            "What each of the denoiser's stages left is hashed beside it and reported, never a "
            "verdict, in the order the frame writes it: the first that moved is where a difference "
            "under the composed frame began");

        option({ Verb::Bench }, "pictures", bpo::bool_switch(),
            "write every measured frame's picture into --out as <view>-<frame>.png, beside its "
            "hash, which is what a pair that differed is diffed pixel by pixel from: a hash names "
            "the frame and never where in it. A PNG a frame on the frame path, some sixty "
            "milliseconds each, so a run under this is further still from a benchmark");

        option({ Verb::Shot, Verb::Bench, Verb::Check, Verb::Film, Verb::View, Verb::Noise, Verb::Tour }, "out",
            bpo::value<std::string>()->default_value(""),
            "the directory to write into: a `bench`'s `hashes.csv`, one row a frame — the picture, "
            "every image the trace wrote, what the frame handed the reconstruction, every image the "
            "denoiser left where its filter ran and every part "
            "of the scene, each as a hash, the oracle a moving camera has instead of `shot`'s stills "
            "— which a `bench` writes only where this is named, since reading a frame back waits on "
            "the device and a run under this or --against is no benchmark; and every picture, as "
            "<view>.png beside <view>-doll.png, "
            "<view>-map.png and <view>-textures.png, a film's frames/000000.png onwards and "
            "<keys>.mp4, a tour's findings.txt and tour.json after every leg and the pictures its Home marks "
            "under marks/, the picture of each Home press in a `view` as <view>-<n>.png, with the "
            "block Home prints inside it as the PNG's Description, or `noise`'s five pictures of a "
            "place as <view>.png beside <view>-mean.png, <view>-reference.png, <view>-averaged.png "
            "and <view>-averaged-limit.png: the verb's own name unless named");

        const FilmPacing pacing;
        option({ Verb::View, Verb::Film }, "keys", bpo::value<std::string>()->default_value(""),
            "a film's keys file: `view` appends the key it stands at to it on every Home press, and "
            "`film` flies through its keys. A key is the block Home prints, so Home output pasted "
            "into a file is keys too; a key may add `seconds` (how long the flight to it takes), "
            "`at` (the second of the film it is reached at, which the flights since the key before "
            "with a time fill at one speed), `hold` (how long the camera rests on it) and `cut` (true "
            "to cut before it, false to fly to it however far)");
        const TourPacing touring;
        option({ Verb::Film, Verb::Tour }, "plan", bpo::bool_switch(),
            "print what the run would fly, then stop without drawing: a film's takes and the length of every "
            "segment and why, a tour's legs, rooms and frames, and the minutes they take at --frame-ms");
        option({ Verb::Film }, "fps", number(atLeast(0.0f, true))->default_value(sStepRate),
            "frames a second of film, which is also what the world steps by");
        option({ Verb::Film, Verb::Tour }, "speed", number(atLeast(0.0f, true)),
            std::format("world units a second the camera flies at. A film's, {:g} unless named, along its path "
                        "through the keys, one speed from the first key to the last, in place of --length: {:.0f} "
                        "metres a second is a drone and not a run; a turn, the clock or a crossing on a flight "
                        "takes the flight's time, and --plan says where one asks for longer. A tour's, {:g} unless "
                        "named, the `streaming` suite's: a cell the flight outran is a finding, and a slower speed "
                        "covers it",
                pacing.mSpeed, pacing.mSpeed / Constants::UnitsPerMeter, touring.mSpeed));
        option({ Verb::Film }, "length", number(atLeast(0.0f, true)),
            std::format("the film's length in seconds: every flight after the last key with an `at` at "
                        "the one speed that fills it, which is the path's whole length over what is left once "
                        "the holds, the stills, what stands on the spot and the keys' own seconds are taken "
                        "out. {:g} where neither this nor --speed is named, unless a key names its `at`, "
                        "every flight has its own seconds or the rest leave no frame to fly, where the film "
                        "is what the keys and --speed make it",
                FilmPacing::sLengthByDefault)
                .c_str());
        option({ Verb::Film }, "ease", number(atLeast(0.0f))->default_value(pacing.mEase),
            "seconds the camera takes to reach its speed from a rest and to come back to one: at a "
            "take's ends, at a hold, and beside a turn on the spot; and to change speed at a key it "
            "flies through, between a slower flight and a faster one, inside the faster. Nought "
            "flies at full speed from the first frame to the last");
        option({ Verb::Film }, "pan-seconds", number(atLeast(0.0f, true))->default_value(pacing.mPanSeconds),
            "how long a pan takes to sweep one image width, or a tilt one image height: the "
            "established limit before judder, which a frame with no motion blur shows sooner");
        option({ Verb::Film }, "hour-seconds", number(atLeast(0.0f, true))->default_value(pacing.mHourSeconds),
            "seconds of film a game hour takes where two keys' hours differ: the time-lapse's pace. "
            "The clock runs forward only, so a key at an earlier hour is reached the next day");
        option({ Verb::Film, Verb::Tour }, "crossing", number(atLeast(0.0f, true)),
            std::format("the least a crossing into another weather takes, {:g} s for a film and {:g} for a tour "
                        "unless named. A film's sky crosses over the whole of the segment between two keys whatever "
                        "it takes, and under --turn-weather each crossing takes this; a tour's circuit of every pair "
                        "of weathers takes this a crossing",
                pacing.mCrossingSeconds, touring.mCrossingSeconds));
        option({ Verb::Film, Verb::Tour }, "clock", number(atLeast(0.0f)),
            std::format("run the game clock at this many times the game's own speed — the timescale the session "
                        "starts at, which the ×N the clock keys set in a window multiplies too. A film's over the "
                        "whole film, whatever hours the keys after the first name: a time-lapse the camera flies "
                        "through at its own pace, across cuts, the keys' hours then setting no segment's length, "
                        "and --clock=0 holding the first key's hour. A tour's through every leg, {:g} unless named, "
                        "a day in two minutes of world at the game's own timescale",
                touring.mClock));
        option({ Verb::Film, Verb::Tour }, "weather-hold", number(atLeast(0.0f)),
            std::format("how long each weather of a film's --turn-weather, {:g} s unless named, or of a tour's "
                        "circuit, {:g} unless named, stands before it crosses into the next",
                pacing.mWeatherHold, touring.mWeatherHold));
        option({ Verb::Tour }, "from", number(anyNumber<std::uint32_t>())->default_value(1),
            "the leg a tour begins at, counted from one as --plan numbers them: the rest flown after a crash, each "
            "leg under the hour and the sky the whole tour would have stood it under");
        option({ Verb::Tour }, "rooms", number(anyNumber<std::uint32_t>())->default_value(touring.mRooms),
            std::format("how many rooms a tour shares out among the content files by the rooms each adds, never "
                        "under {} a file nor over what it has, each file's with the most references",
                touring.mRoomsLeast));
        option({ Verb::Tour }, "frame-ms", number(atLeast(0.0f, true))->default_value(7.0f),
            "the frame time --plan works a tour's minutes out at");
        option({ Verb::Tour }, "pace", bpo::value<std::string>()->default_value("card"),
            "card flies a tour as fast as the card draws, the hardest the streaming is driven; real holds every frame "
            "to the step, so the world's second is the wall's and a slow frame is the limiter's and no finding");
        option({ Verb::Tour }, "fail-on", bpo::value<std::string>()->default_value(""),
            "the costs that fail a tour beside its faults, comma separated: hitch, slow-frames, log-warning, growth. "
            "A fault always fails it: a store not finite, a refusal, a failed check, an error in the log, an outrun "
            "cell, a leg that failed");
        option({ Verb::Film }, "still", number(atLeast(0.0f, true))->default_value(pacing.mStillSeconds),
            "how long a key with no key either side of it stands, and a segment where nothing changes");
        option({ Verb::Film }, "cut-distance", number(atLeast(0.0f))->default_value(pacing.mCutDistance),
            std::format("how far apart two keys can be and still be flown between rather than cut: {:g} "
                        "exterior cells by default. A key in another interior, or inside where the last "
                        "was out, is always a cut",
                pacing.mCutDistance / static_cast<float>(Constants::CellSizeInUnits)));
        option({ Verb::Film }, "encode", bpo::value<bool>()->default_value(true)->implicit_value(true),
            std::format("run ffmpeg over the frames once they are drawn, with {} at CRF {} in {}, which "
                        "every player reads. --encode=false prints the command instead",
                sVideoCodec, sVideoQuality, sVideoPixels));
        option(sFramed, "size",
            bpo::value<std::string>()->default_value(
                std::format("{}x{}", byDefault.mWindow.mWidth, byDefault.mWindow.mHeight)),
            std::format("image size, as WIDTHxHEIGHT. `check` draws {}x{} where the line names none: no claim of its "
                        "but `finite` reads a pixel, and no extent of that frame is whole workgroups. `tour` draws "
                        "{}x{}: its findings are the content's faults, not its figures",
                sCheckFrame.mWidth, sCheckFrame.mHeight, sTourFrame.mWidth, sTourFrame.mHeight));
        option(sFramed, "fov", number(between(1.0f, 179.0f))->default_value(byDefault.mWindow.mFieldOfView),
            "vertical field of view, in degrees");
        option(sPlaces, "pos", bpo::value<std::string>()->default_value(""),
            "where to put the camera, as x,y,z. Defaults to the view's, and without a view to where the "
            "game puts a player arriving in the cell, facing where it faces them. Write "
            "--pos=-100,200,300, or a leading minus reads as an option.");
        option(sPlaces, "look", bpo::value<std::string>()->default_value(""),
            "what the camera looks at, as x,y,z, from --pos or the view's eye, and refused without "
            "either. Defaults to the view's, and without one to due north.");

        option({ Verb::Shot }, "accumulate", number(anyNumber<std::uint32_t>())->default_value(0),
            "average this many differently-seeded frames into one picture. A converged reference, "
            "which is the only ground truth a sampled renderer has: error falls as the square root "
            "of this, so a hundred is a clean picture and a thousand is a reference. Wants "
            "--upscale=off, because a denoiser resolves every frame towards its own opinion rather "
            "than towards the integral");

        option(sFramed, "jitter",
            bpo::value<bool>()
                ->default_value(byDefault.mSetup.mRun.mProfile.mReconstruction.mJitter)
                ->implicit_value(true),
            "sample a different point inside each pixel every frame. Only worth anything to "
            "something putting several frames together, and forced on whenever anything upscales");

        option(VerbSet::every(), "data",
            bpo::value<Files::MaybeQuotedPathContainer>()
                ->default_value(Files::MaybeQuotedPathContainer(), "data")
                ->multitoken()
                ->composing(),
            "set data directories (later directories have higher priority)");

        option(VerbSet::every(), "data-local",
            bpo::value<Files::MaybeQuotedPathContainer::value_type>()->default_value(
                Files::MaybeQuotedPathContainer::value_type(), ""),
            "set local data directory (highest priority)");

        option(VerbSet::every(), "fallback-archive",
            bpo::value<StringsVector>()->default_value(StringsVector(), "fallback-archive")->multitoken()->composing(),
            "set fallback BSA archives (later archives have higher priority)");

        option(VerbSet::every(), "content",
            bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "content file(s): esm/esp, or omwgame/omwaddon/omwscripts");

        option(VerbSet::every(), "groundcover",
            bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "groundcover content file(s): esm/esp, or omwgame/omwaddon");

        option(VerbSet::every(), "encoding", bpo::value<std::string>()->default_value("win1252"),
            "character encoding of the content files");

        option(VerbSet::every(), "fallback",
            bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing(),
            "fallback values");

        // The engine's own two, because a hosted run starts a real game: a savegame restores the player, the
        // camera, the hour and every cell the session had loaded, which no pair of coordinates can, and the seed is
        // what makes two runs of one build the same run.
        option(sFramed, "load-savegame",
            bpo::value<Files::MaybeQuotedPath>()->default_value(Files::MaybeQuotedPath(), ""),
            "start from this savegame rather than from a new game");

        option(sFramed, "random-seed", number(anyNumber<unsigned int>())->default_value(42),
            "seed the world's random draws, so two runs of one build draw the same world");

        Files::ConfigurationManager::addCommonOptions(result.mDescription);

        return result;
    }

    ToolLine ToolOptions::read(const bpo::variables_map& variables, const Verb verb) const
    {
        const auto named = [&](const char* name) { return variables.count(name) != 0 && !variables[name].defaulted(); };
        const auto text = [&](const char* name) -> const std::string& { return variables[name].as<std::string>(); };
        const auto real = [&](const char* name) { return variables[name].as<float>(); };
        const auto count = [&](const char* name) { return variables[name].as<std::uint32_t>(); };
        const auto on = [&](const char* name) { return variables[name].as<bool>(); };
        const auto strings = [&](const char* name) { return variables[name].as<StringsVector>(); };

        ToolLine line;
        line.mShaderSource = on("shader-source");
        line.mFolders = on("folders");
        line.mResources = variables["resources"].as<Files::MaybeQuotedPath>();
        if (named("out"))
            line.mOut = std::filesystem::path(text("out"));
        line.mAgainst = text("against");
        line.mKeys = text("keys");
        line.mFind = text("find");

        // A level somebody typed is demanded: a run that asked for the layers and cannot have them
        // fails naming what is missing, because an empty log reads as a clean pass, while a build
        // that turned them on by default only warns.
        line.mValidation = Rtx::ValidationOptions{
            .mLevel = Rtx::sValidationNames.require(text("validation"), "a validation level"),
            .mDemanded = named("validation"),
        };

        ToolLine::Frame& frame = line.mFrame;
        frame.mSize = named("size") ? parseSize(text("size"))
            : verb == Verb::Check   ? sCheckFrame
            : verb == Verb::Tour    ? sTourFrame
                                    : parseSize(text("size"));
        frame.mFieldOfView = real("fov");
        frame.mDay = { variables["day"].as<int>(), named("day") };
        if (const Result<void, std::string_view> checked = checkDay(frame.mDay.mValue); !checked.isOk())
            throw std::runtime_error(std::format("--day={} {}", frame.mDay.mValue, checked.error()));
        frame.mUpscale = { Rtx::sUpscaleNames.require(text("upscale"), "an upscale mode"), named("upscale") };
        frame.mUpscaler = { Rtx::sUpscalerNames.require(text("upscaler"), "an upscaler"), named("upscaler") };
        if (named("grass"))
            frame.mGrass = on("grass");
        if (named("distant-cells"))
            frame.mDistantCells = real("distant-cells");
        if (named("distant-statics"))
            frame.mDistantStatics = on("distant-statics");
        if (named("gamma"))
            frame.mGamma = real("gamma");
        if (named("memory-budget"))
            frame.mMemoryBudget = variables["memory-budget"].as<std::uint64_t>() * 1024 * 1024;
        frame.mPipelineCode = text("pipeline-code");
        if (const std::string& store = text("content-cache"); store == "off")
            frame.mContentCache = NoContentCache{};
        else if (store == "game")
            frame.mContentCache = GameContentCache{};
        else if (store.empty())
            throw std::runtime_error("--content-cache names nothing: game, off or a directory");
        else
            frame.mContentCache = std::filesystem::path(store);
        frame.mShow = Rtx::sSurfaceViewNames.require(text("show"), "a surface view");
        frame.mExposure = parseExposure(text("exposure"));
        frame.mHold = { on("hold"), named("hold") };
        frame.mVariants = on("variants");
        for (const SwitchReader& reader : sSwitchReaders)
            reader.mRead(variables[std::string(reader.mName)], frame.mReconstruction);
        frame.mBouncesNamed = { named("indirect"), named("bounce-rays") };

        ToolLine::Places& places = line.mPlaces;
        places.mView = text("view");
        places.mCell = CellAddress(text("cell"));
        places.mPos = pointNamed("pos", text("pos"));
        places.mLook = pointNamed("look", text("look"));
        if (!text("views").empty())
            places.mViews = splitNames(text("views"));
        if (named("suite"))
            places.mSuite = text("suite");
        places.mSave = variables["load-savegame"].as<Files::MaybeQuotedPath>();

        // **A place named on the line beside `--views` is refused**: every stop comes from the file,
        // and `shot --views=balmora --pos=...` drew Balmora at its file's eye and said nothing.
        if (places.mViews.has_value())
            for (const char* place : { "cell", "view", "pos", "look" })
                if (named(place))
                    throw std::runtime_error(
                        std::format("--views takes its places from the file, and --{} names one beside them", place));

        ToolLine::Sky& sky = line.mSky;
        if (named("hour"))
        {
            const float hour = real("hour");
            if (const Result<void, std::string_view> checked = checkHour(hour); !checked.isOk())
                throw std::runtime_error(std::format("--hour={} {}", hour, checked.error()));
            sky.mHour = hour;
        }
        if (named("weather"))
            sky.mWeather = weatherNamed("weather", text("weather"));
        sky.mAir = airNamed(text("air"));
        for (const std::string& weather : splitNames(text("turn-weather")))
            sky.mTurn.push_back(weatherNamed("turn-weather", weather));

        line.mSession = ToolLine::Session{
            .mHud = on("hud"),
            .mSpellHands = on("spell-hands"),
            .mVanity = on("vanity"),
            .mNightEye = variables["night-eye"].as<int>(),
            .mRandomSeed = variables["random-seed"].as<unsigned int>(),
        };

        // Each says how long a stop runs, which is one answer.
        ToolLine::Bench& bench = line.mBench;
        bench.mFrames = count("frames");
        bench.mSeconds = real("seconds");
        if (bench.mFrames == 0)
            bench.mSpan = BenchSpan{ .mSeconds = bench.mSeconds };
        else if (named("seconds"))
            throw std::runtime_error("--frames and --seconds each say how long a stop runs, so only one can be named");
        else
            bench.mSpan = BenchSpan{ .mFrames = bench.mFrames };
        bench.mWarmup = real("warmup");
        bench.mWindow = on("window");
        bench.mCounters = on("counters");
        bench.mJson = text("json");
        bench.mFrameTimes = text("frame-times");
        bench.mPerfControl = text("perf-control");
        bench.mPictures = on("pictures");

        line.mShot = ToolLine::Shot{
            .mAccumulate = count("accumulate"),
            .mRepeat = count("repeat"),
            .mDoll = text("doll"),
            .mMap = on("map"),
            .mTextures = on("textures"),
        };

        ToolLine::Noise& noise = line.mNoise;
        noise.mStrafe = real("strafe");
        noise.mWalk = real("walk");
        noise.mCut = count("cut");
        noise.mAsked = text("versus");
        if (!noise.mAsked.empty())
            noise.mVersus = versus(frame.mReconstruction, noise.mAsked);

        // The length sets the speed, so the two named are two answers to one question.
        ToolLine::Film& film = line.mFilm;
        FilmPacing& pacing = film.mPacing;
        film.mPlan = on("plan");
        pacing.mStep = 1.0f / real("fps");
        if (named("speed"))
            pacing.mSpeed = real("speed");
        if (named("length"))
        {
            if (named("speed"))
                throw std::runtime_error("--length sets the speed, so --speed cannot be named beside it");
            pacing.mLength = FilmLength{ .mSeconds = real("length"), .mSource = FilmLengthSource::Named };
        }
        else if (!named("speed"))
            pacing.mLength
                = FilmLength{ .mSeconds = FilmPacing::sLengthByDefault, .mSource = FilmLengthSource::ByDefault };
        pacing.mEase = real("ease");
        pacing.mPanSeconds = real("pan-seconds");
        pacing.mHourSeconds = real("hour-seconds");
        if (named("crossing"))
            pacing.mCrossingSeconds = real("crossing");
        if (named("clock"))
            pacing.mClock = real("clock");
        if (named("weather-hold"))
            pacing.mWeatherHold = real("weather-hold");
        pacing.mStillSeconds = real("still");
        pacing.mCutDistance = real("cut-distance");
        film.mEncode = on("encode");

        ToolLine::Tour& tour = line.mTour;
        TourPacing& touring = tour.mPacing;
        tour.mPlan = film.mPlan;
        if (named("speed"))
            touring.mSpeed = real("speed");
        if (named("crossing"))
            touring.mCrossingSeconds = real("crossing");
        if (named("clock"))
            touring.mClock = real("clock");
        if (named("weather-hold"))
            touring.mWeatherHold = real("weather-hold");
        touring.mRooms = count("rooms");
        tour.mFrameMs = real("frame-ms");
        if (count("from") == 0)
            throw std::runtime_error("--from=0 names no leg: a tour's legs are counted from one");
        tour.mFrom = count("from");
        if (const std::string& pace = text("pace"); pace == "real")
            tour.mPaced = true;
        else if (pace != "card")
            throw std::runtime_error(std::format("--pace={} is neither card nor real", pace));
        for (const std::string& kind : splitNames(text("fail-on")))
        {
            const std::optional<FindingKind> found = findingNamed(kind);
            if (!found.has_value())
                throw std::runtime_error(std::format("--fail-on names no kind of finding called \"{}\"", kind));
            tour.mFailOn.push_back(*found);
        }

        ToolLine::Game& game = line.mGame;
        game.mContent = strings("content");
        game.mData = Files::asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>());
        game.mDataLocal = variables["data-local"].as<Files::MaybeQuotedPathContainer::value_type>().u8string();
        game.mFallbackArchives = strings("fallback-archive");
        game.mFallback = variables["fallback"].as<Fallback::FallbackMap>();
        game.mGroundcover = strings("groundcover");
        game.mEncoding = text("encoding");

        return line;
    }
}
