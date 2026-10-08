#include "memorykey.hpp"

#include <ostream>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <apps/openmw/mwrender/rtx/worldmirror.hpp>
#include <apps/rtxtool/model/benchrecord.hpp>
#include <components/debug/debugging.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtx/renderer/renderer.hpp>

namespace RtxTool
{
    void MemoryKey::listen()
    {
        const bool down = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_END];
        if (down && !mHeld)
            mAsked = true;
        mHeld = down;
    }

    void MemoryKey::answer(const MWRender::FrameContext& context)
    {
        if (!mAsked)
            return;

        mAsked = false;
        Debug::getRawStdout() << "# memory\n  scene  " << describeSceneHeld(context.mBackend.getSceneStats()) << '\n'
                              << Rtx::describeMemory(context.mBackend.getMemoryReport())
                              << describeHostHeld(context.mMirror.getContentMemory()) << std::flush;
    }
}
