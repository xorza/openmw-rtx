#pragma once

namespace MWLua
{
    struct Context;

    void initCellBindingsForLocalScripts(const Context& context);
    void initCellBindingsForGlobalScripts(const Context& context);
}
