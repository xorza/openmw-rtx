#pragma once

#include <string>

#include "context.hpp"

namespace Interpreter
{
    std::string fixDefinesDialog(std::string_view text, Context& context);
    std::string fixDefinesMsgBox(std::string_view text, Context& context);
    std::string fixDefinesBook(std::string_view text, Context& context);
}
