#include "run.hpp"

#include <filesystem>
#include <ostream>
#include <string>
#include <utility>

#include <boost/program_options/variables_map.hpp>

#include <apps/openmw/engine.hpp>
#include <apps/openmw/startup.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <components/debug/debugging.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/result.hpp>
#include <components/settings/values.hpp>
#include <components/settings/windowmode.hpp>

#include "session.hpp"

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        /// Everything a hosted run writes into the settings before the engine reads them: the
        /// window it is presented in.
        ///
        /// **These are settings and not a second command line**, because both binaries have to
        /// reach one engine configured one way. What the *trace* and the *mirror* are configured by
        /// travels in the `RunSetup` the renderer is made with — `SessionRequest::mSetup` — and
        /// never through the registry, which is the player's.
        void applyHostedSettings(const WindowRequest& window)
        {
            // The window at the frame's size, so the frame is shown pixel for pixel.
            Settings::video().mResolutionX.set(static_cast<int>(window.mWidth));
            Settings::video().mResolutionY.set(static_cast<int>(window.mHeight));
            Settings::video().mWindowWidth.set(static_cast<int>(window.mWidth));
            Settings::video().mWindowHeight.set(static_cast<int>(window.mHeight));
            Settings::video().mWindowMode.set(Settings::WindowMode::Windowed);
            Settings::video().mVsyncMode.set(window.mVerticalSync);
            Settings::video().mFramerateLimit.set(window.mFramerateLimit);
            Settings::camera().mFieldOfView.set(window.mFieldOfView);

            // **Physics on the frame's own thread, so a run is the same run twice.** A physics
            // worker refreshes the AI's line-of-sight cache after each step
            // (`PhysicsTaskScheduler::refreshLOSCache`) while the AI on the main thread reads it,
            // and whether a refresh landed before or after a read is the worker's timing: an actor
            // that saw or did not see its target walks elsewhere from the next frame on. The step
            // is one physics step a frame either way, so nothing about the simulation changes but
            // who runs it and when the cache is read.
            Settings::physics().mAsyncNumThreads.set(0);
        }

        /// **What a measured run reads of the content's companion maps, from the shipped file**:
        /// whether to look for a model's normal and specular maps and what they are called, which
        /// `MWRender::Renderer::prepareResources` reads from the registry for both renderers. Two
        /// machines that differ only in their `settings.cfg` then bench one scene, and a played run
        /// keeps the player's.
        void applyShippedContentRules(const Files::ConfigurationManager& config)
        {
            Settings::ShadersCategory& shaders = Settings::shaders();
            shaders.mAutoUseObjectNormalMaps.set(
                shippedDefault<bool>(config, "Shaders", "auto use object normal maps"));
            shaders.mAutoUseObjectSpecularMaps.set(
                shippedDefault<bool>(config, "Shaders", "auto use object specular maps"));
            shaders.mNormalMapPattern.set(shippedDefault<std::string>(config, "Shaders", "normal map pattern"));
            shaders.mNormalHeightMapPattern.set(
                shippedDefault<std::string>(config, "Shaders", "normal height map pattern"));
            shaders.mSpecularMapPattern.set(shippedDefault<std::string>(config, "Shaders", "specular map pattern"));
        }
    }

    int runHosted(const bpo::variables_map& variables, Files::ConfigurationManager& config,
        const std::filesystem::path& resources, const WindowRequest& window, SessionRequest request,
        const bool printLeft)
    {
        std::ostream& out = Debug::getRawStdout();

        applyHostedSettings(window);
        if (!request.mPlayed)
            applyShippedContentRules(config);

        // **Whether the run was meant to end on its own**, which is what says an empty report is a
        // failure. A window somebody closes has finished no stop and owes no numbers.
        const bool scheduled = request.mQuitAtEnd;

        // Taken before the request is handed to the session that owns it from here on.
        const bool played = request.mPlayed;

        const unsigned int seed = request.mRandomSeed;

        // **The game's own reading of the installation** (`OpenMW::readInstallation`), with what
        // a played run adds to it: the keys exist where somebody plays the run —
        // `VerbPolicy::mPlayed`, and not wherever there is a window: a bench shows one, and a
        // key pressed there moved the clock, the hour or the weather under a measurement that
        // recorded none of it. A played run answers the brackets, the comma, the full stop, the
        // slash and the page keys with the weather and the clock, through the Lua scripts under
        // the harness's own data directory, and Home with where it stands, through the session;
        // the played game names neither the directory nor the file.
        OpenMW::InstallationExtras extras;
        if (played)
        {
            extras.mDataDirs.push_back(resources / "rtx" / "vfs");
            extras.mContent.emplace_back("rtxtool.omwscripts");
        }

        const Misc::Result<OpenMW::Installation, std::string> installation
            = OpenMW::readInstallation(variables, config, extras);
        if (!installation.isOk())
        {
            out << installation.error() << '\n';
            return 1;
        }

        // **Built before the engine and read after it.** A run that ends its last stop and a window
        // somebody closes both have to be reported, and only the first ever reaches `finish` — so
        // what the run came to is asked for once the engine has gone, off what the run noted on
        // every frame it still had a world to note it from.
        Session session(std::move(request));

        {
            OMW::Engine engine(config);
            engine.setRecastMaxLogLevel(Debug::getRecastMaxLogLevel());

            engine.setResourceDir(resources);

            installation.value().handTo(engine);

            // **Straight into the world, with no character generation.** `setSkipMenu(true, false)`
            // reaches `StateManager::newGame(true)`, which is the bypass a session wants: a stop says
            // where it stands, and standing anywhere at all is the only thing the start has to do.
            engine.setSkipMenu(true, false);
            engine.setSaveGameFile(variables["load-savegame"].as<Files::MaybeQuotedPath>().u8string());
            engine.setRandomSeed(seed);

            // **No sound and no mouse, because nobody is here.** A run measured with an audio device
            // open measures the mixer as well, and a grabbed pointer in a headless run is a pointer
            // somebody has to get back.
            engine.setSoundUsage(false);
            engine.setGrabMouse(false);

            // The session is the host: it makes the renderer, states the step and runs the
            // schedule, so the engine never reads `[RTX] enabled` and never sees the run.
            engine.setHost(session);

            engine.go();
        }

        const SessionResult result = session.describe();

        out << result.mReport;

        // **Where it was left, so a session that ended somewhere worth keeping did not lose it.**
        if (printLeft && result.mLeft.has_value())
            out << describeStanding(*result.mLeft);

        // **A run that reached no stop is a failure and not an empty report.** A cell that could
        // not be loaded and a save that would not open both end here, and each of them is a command
        // that did not do what it was asked. A window somebody closed is not one of them.
        if (scheduled && result.mPlaces.empty())
        {
            out << "\nnothing was measured: the run ended before a stop finished\n";
            return 1;
        }

        return result.mExitStatus;
    }
}
