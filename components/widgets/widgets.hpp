#pragma once

#include <cstdint>

extern std::int32_t GuiFontHeight;

namespace Gui
{
    /// Register all widgets from this component with MyGUI's factory manager.
    void registerAllWidgets();

}
