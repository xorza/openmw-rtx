#pragma once

namespace Nif
{
    struct NiNode;

    struct Parent
    {
        const NiNode& mNiNode;
        const Parent* mParent;
    };
}
