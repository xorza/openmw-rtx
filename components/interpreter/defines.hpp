#pragma once

#include "context.hpp"
#include <string>

namespace Interpreter
{
    std::string fixDefinesDialog(std::string_view text, Context& context);
    std::string fixDefinesMsgBox(std::string_view text, Context& context);
    std::string fixDefinesBook(std::string_view text, Context& context);
}
