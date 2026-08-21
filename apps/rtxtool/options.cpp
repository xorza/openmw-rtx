#include "options.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <boost/program_options/value_semantic.hpp>

#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>

namespace bpo = boost::program_options;

namespace RtxTool
{
    namespace
    {
        using StringsVector = std::vector<std::string>;

        /// What `--upscale` reads when nobody names it.
        ///
        /// **It follows the build**, because the two are one decision: `-DOPENMW_RTX_DLSS=OFF` is a
        /// deliberate opt-out, and a tool that then refused every default invocation would be
        /// telling its user to turn on the thing they had just turned off.
        ///
        /// Quality rather than performance, so a plain run is the renderer with everything switched
        /// on and not one that quietly quartered the pixels it traced. `--upscale=performance` is
        /// the 1920x1080 to 3840x2160 the frame budget is written against.
#ifdef OPENMW_RTX_DLSS
        constexpr std::string_view sUpscaleByDefault = "quality";
#else
        constexpr std::string_view sUpscaleByDefault = "off";
#endif
    }

    boost::program_options::options_description makeOptionsDescription(const bool validationByDefault)
    {
        bpo::options_description result("Options");
        auto addOption = result.add_options();
        addOption("help", "print this message and quit");

        // On unless this was built for release, and `--validation=false` turns any of them off
        // again. An implicit value is what lets the bare `--validation` still mean "yes".
        addOption("validation", bpo::value<bool>()->default_value(validationByDefault)->implicit_value(true),
            "load VK_LAYER_KHRONOS_validation. On by default outside a Release build");
        addOption("sync-validation", bpo::value<bool>()->default_value(validationByDefault)->implicit_value(true),
            "add synchronization validation, which catches missing barriers (implies --validation)");
        addOption("gpu-validation", bpo::value<bool>()->default_value(validationByDefault)->implicit_value(true),
            "add GPU-assisted validation, which instruments shaders and catches what a ray query "
            "does with its own arguments (implies --validation). Costs about half the frame rate, "
            "and is left off by `view` unless asked for: a window under it loses the device");

        addOption("cell", bpo::value<std::string>()->default_value(""),
            "cell to read, addressed the way Morrowind does: a pair of integers is an exterior, "
            "anything else is an interior's name. Write --cell=-2,-9 rather than --cell -2,-9, or "
            "the leading minus reads as an option. Left out, the default view decides.");

        addOption("twice", bpo::bool_switch(),
            "extract the cell a second time and report what the second pass added, which should "
            "be nothing");

        addOption("view", bpo::value<std::string>()->default_value(""),
            "a named viewpoint from resources/rtx/views.cfg, which supplies the cell and usually the "
            "camera. Overrides --cell.");

        addOption("list-views", bpo::bool_switch(), "print the named viewpoints and quit");

        addOption("jitter", bpo::bool_switch(),
            "move each frame's sample inside its pixel, along a Halton sequence. With "
            "--accumulate this is what makes a reference antialiased");
        addOption("delight", bpo::value<float>()->default_value(1.0f),
            "how much of the lighting painted into each texture to divide back out, from 0 to 1. "
            "Zero is the A/B that says what it did");
        addOption("filter", bpo::value<bool>()->default_value(true)->implicit_value(true),
            "run the denoiser over the indirect light. Off shows the raw bounce, and is what a "
            "reference built with --accumulate has to be made with");
        addOption("radius", bpo::value<std::uint32_t>()->default_value(4),
            "how many cells out from the one asked for to load, so four is nine by nine. Zero is "
            "the cell alone, floating in sky. An interior ignores it — a room has no neighbours. "
            "Every cell costs its own geometry, so a wide region is a slow load and a large scene, "
            "and `view` brings the next ring in as the camera crosses into it");

        addOption("upscale", bpo::value<std::string>()->default_value(std::string(sUpscaleByDefault)),
            "put DLSS Ray Reconstruction between the trace and the picture: off, performance, "
            "balanced, quality or dlaa. --size is what comes out, and what gets traced is DLSS's "
            "answer for it. It denoises for itself, so --filter stops applying. Quality by default, "
            "so a plain run is the renderer with everything switched on without quartering the "
            "pixels it traced; --upscale=performance is the 1920x1080 to 3840x2160 the frame budget "
            "is written against, and --upscale=off is what an A/B against the unupscaled path "
            "needs. --accumulate turns it off unless this is named, because a reference cannot be "
            "built through a denoiser");

        addOption("exposure", bpo::value<std::string>()->default_value("auto"),
            "what to scale the frame by before the display curve: auto measures it off the frame, "
            "and a number holds it there. A pixel test and a converged reference want it held, "
            "because a measured exposure makes every value depend on the whole frame");

        addOption("albedo", bpo::bool_switch(),
            "write the albedo with no shading over it, which is what a texture problem looks like "
            "when nothing else is in the way");

        addOption("weather", bpo::value<std::string>()->default_value("Clear"),
            "which weather's sun and sky an exterior stands under, named as the content files "
            "spell it: Clear, Cloudy, Foggy, Overcast, Rain, Thunderstorm, Ashstorm, Blight");

        addOption("hour", bpo::value<float>()->default_value(12.0f),
            "what time an exterior's sun is at, on a twenty-four hour clock. An interior is lit "
            "by its own lamps and does not care");

        addOption("frames", bpo::value<std::uint32_t>()->default_value(0),
            "with `view`, close after this many frames instead of waiting to be closed");

        addOption("repeat", bpo::value<std::uint32_t>()->default_value(8),
            "with `shot`, trace the frame this many times and report the best. One submit times "
            "the GPU's clock rather than the shader; a comparison worth making wants hundreds");

        addOption("accumulate", bpo::value<std::uint32_t>()->default_value(0),
            "with `shot`, average this many differently-seeded frames into the picture. The way "
            "to a converged reference for a sampled renderer: error falls as the square root, so "
            "a hundred is a clean picture and a thousand is something to measure against");

        addOption("find", bpo::value<std::string>()->default_value(""),
            "with `scene`, print the world position of every object whose model path contains this. "
            "How the coordinates in a view are found.");

        addOption("out", bpo::value<std::string>()->default_value("shot.png"), "where to write the image");
        addOption("size", bpo::value<std::string>()->default_value("1920x1080"), "image size, as WIDTHxHEIGHT");
        addOption("fov", bpo::value<float>()->default_value(60.0f), "vertical field of view, in degrees");
        addOption("pos", bpo::value<std::string>()->default_value(""),
            "where to put the camera, as x,y,z. Defaults to a view of the whole cell from outside it, "
            "which is a poor view of an interior. Write --pos=-100,200,300, or a leading minus reads "
            "as an option.");
        addOption("look", bpo::value<std::string>()->default_value(""),
            "what the camera looks at, as x,y,z. Defaults to the centre of the cell.");

        addOption("data",
            bpo::value<Files::MaybeQuotedPathContainer>()
                ->default_value(Files::MaybeQuotedPathContainer(), "data")
                ->multitoken()
                ->composing(),
            "set data directories (later directories have higher priority)");

        addOption("data-local",
            bpo::value<Files::MaybeQuotedPathContainer::value_type>()->default_value(
                Files::MaybeQuotedPathContainer::value_type(), ""),
            "set local data directory (highest priority)");

        addOption("fallback-archive",
            bpo::value<StringsVector>()->default_value(StringsVector(), "fallback-archive")->multitoken()->composing(),
            "set fallback BSA archives (later archives have higher priority)");

        addOption("content", bpo::value<StringsVector>()->default_value(StringsVector(), "")->multitoken()->composing(),
            "content file(s): esm/esp, or omwgame/omwaddon/omwscripts");

        addOption(
            "encoding", bpo::value<std::string>()->default_value("win1252"), "character encoding of the content files");

        addOption("fallback",
            bpo::value<Fallback::FallbackMap>()->default_value(Fallback::FallbackMap(), "")->multitoken()->composing(),
            "fallback values");

        Files::ConfigurationManager::addCommonOptions(result);

        return result;
    }
}
