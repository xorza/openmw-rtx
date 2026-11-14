#pragma once

#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace MWRender
{
    class Renderer;

    /// What a player is told where the ray tracer could not start: what failed, the folder whose
    /// log says the rest, and, where the machine cannot run it (`Rtx::Unsupported`), the hardware
    /// the build needs. A fault of the renderer's own does not name the card, which is not its
    /// cause.
    std::string describeStartFailure(const std::exception& failure, const std::filesystem::path& logFolder);

    /// Makes the ray tracer: `createRenderer` in the game, a renderer of its own in a test.
    using RendererFactory = std::function<std::unique_ptr<Renderer>()>;

    /// **The ray tracer or nothing.** The game starts no other renderer: the OpenGL renderer stays
    /// in the build, where upstream's code keeps it, and is never chosen. A failure of any kind,
    /// a machine that cannot run the ray tracer (`Rtx::Unsupported`) among them, ends the start
    /// with `describeStartFailure`'s words, which the fatal error box shows.
    std::unique_ptr<Renderer> startRenderer(const RendererFactory& create, const std::filesystem::path& logFolder);
}
