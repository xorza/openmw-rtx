#pragma once

#include <string_view>

#include <MyGUI_UString.h>

namespace Gui
{

    /// Try to replace a tag. Returns true on success and writes the result to \a out.
    bool replaceTag(std::string_view tag, MyGUI::UString& out);

}
