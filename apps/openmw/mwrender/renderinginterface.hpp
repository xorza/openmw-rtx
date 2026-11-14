#pragma once

namespace MWRender
{
    class Objects;

    class RenderingInterface
    {
    public:
        virtual MWRender::Objects& getObjects() = 0;
        virtual ~RenderingInterface() = default;
    };
}
