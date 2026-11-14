#include "rtxstart.hpp"

#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <components/crashcatcher/crash.hpp>
#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/common/error.hpp>

#include <apps/openmw/mwrender/renderer.hpp>

namespace MWRender
{
    std::string describeStartFailure(const std::exception& failure, const std::filesystem::path& logFolder)
    {
        std::string said = "Ray tracing could not start: ";
        said += failure.what();
        if (dynamic_cast<const Rtx::Unsupported*>(&failure) != nullptr)
            said += "\n\nThis build draws with hardware ray tracing alone. It needs an NVIDIA GeForce RTX 20 series "
                    "or later, or an AMD Radeon RX 6000 series (RDNA 2) or later, with a current graphics driver.";
        if (dynamic_cast<const Rtx::UpscalerRefused*>(&failure) != nullptr)
            said += "\n\nThe upscaler chosen in the launcher cannot run here. Choose Auto or FSR there, or set "
                    "[RTX] upscaler to auto in settings.cfg.";
        said += "\n\nThe log in ";
        said += Files::pathToUnicodeString(logFolder);
        said += " says each step of the start.";
        return said;
    }

    std::unique_ptr<Renderer> startRenderer(const RendererFactory& create, const std::filesystem::path& logFolder)
    {
        Log(Debug::Info) << "Renderer: " << nameOf(RendererKind::RayTraced);
        Crash::annotate("renderer", nameOf(RendererKind::RayTraced));
        try
        {
            return create();
        }
        catch (const std::exception& failure)
        {
            throw std::runtime_error(describeStartFailure(failure, logFolder));
        }
    }
}
