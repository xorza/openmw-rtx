#pragma once

#include <chrono>
#include <optional>

namespace MWRender
{
    /// The wall time between two frames of the world that drew no world: the interface's own frames, and the
    /// loop's frames while the window stood hidden.
    struct AsideSpan
    {
        /// From the first frame the interface opened of its own — a loading screen's, a blocking message box's, a
        /// video's — to the end of the last, less what of it stood hidden: the game behind its own screens, its
        /// waits between those frames included, since the screen stood over them.
        std::chrono::steady_clock::duration mInterface{};

        /// The loop's frames that drew nothing, as `Engine::frame` draws nothing while the window is minimized
        /// or covered, and the interface's frames that stood hidden (`Renderer::skipGuiFrame`): time nobody
        /// could see, which is no frame's cost.
        std::chrono::steady_clock::duration mHidden{};
    };

    /// **What a frame of the world stood aside for since the last**, read off the moments the seam sees: each
    /// loop frame opening, each frame of the interface's own opening and ending, and the world's frame arriving.
    /// A loop frame that neither drew the world nor opened an interface frame before the next one opened is
    /// one the window stood hidden through. The interface's frames all fall in the arriving loop frame's update,
    /// after every hidden one, since a hidden loop frame returns before its update; so the two spans never
    /// overlap.
    class FramesAside
    {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        /// How a frame of the interface ended: drawn, or standing hidden.
        enum class NestedEnd
        {
            Drawn,
            Hidden,
        };

        /// The game's loop opened a frame at `now`.
        void openLoop(TimePoint now);

        /// The interface opened a frame of its own at `now`.
        void openNested(TimePoint now);

        /// The interface's frame opened last ended at `now`.
        void endNested(TimePoint now, NestedEnd end);

        /// The world's frame arrived: what stood aside since the last one, and the count starts again.
        AsideSpan takeAtWorldFrame();

    private:
        std::optional<TimePoint> mLoopOpened;
        bool mLoopDrew = false;

        std::optional<TimePoint> mNestedFirst;
        TimePoint mNestedOpened;
        TimePoint mNestedLast;
        std::chrono::steady_clock::duration mNestedHidden{};

        std::chrono::steady_clock::duration mLoopsHidden{};
    };
}
