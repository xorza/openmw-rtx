#pragma once

namespace MWRender
{
    struct FrameContext;
}

namespace RtxTool
{
    /// What End does in a window: prints what the renderer holds of the device's memory and the
    /// host's, by what each part is for — the lines a bench prints under a place, read off the
    /// frame the key went down on.
    ///
    /// **SDL's own key state, as Home is read, and not a script**, because what it reads is the
    /// renderer's, which no script can reach. End is unbound in the game's defaults.
    class MemoryKey
    {
    public:
        /// Before a frame is stepped: End going down asks for the next frame's report.
        void listen();

        /// After a frame: prints the report where End asked for one. Walks every allocation the
        /// device holds, which is a press's cost and never a frame's.
        void answer(const MWRender::FrameContext& context);

    private:
        bool mHeld = false;
        bool mAsked = false;
    };
}
