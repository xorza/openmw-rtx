#pragma once

namespace MWLua
{
    struct Context;

    void initObjectBindingsForLocalScripts(const Context& context);
    void initObjectBindingsForGlobalScripts(const Context& context);
}
