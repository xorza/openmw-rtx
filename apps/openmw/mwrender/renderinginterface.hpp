#ifndef GAME_RENDERING_INTERFACE_H
#define GAME_RENDERING_INTERFACE_H

namespace MWRender
{
    class Objects;

    class RenderingInterface
    {
    public:
        virtual MWRender::Objects& getObjects() = 0;
        virtual ~RenderingInterface() {}
    };
}
#endif
