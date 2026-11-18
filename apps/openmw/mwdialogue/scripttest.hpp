#pragma once

#include <cstdint>

#include <components/compiler/extensions.hpp>

namespace MWDialogue
{

    namespace ScriptTest
    {

        /// Attempt to compile all dialogue scripts, use for verification purposes
        /// @return A pair containing <total number of scripts, number of successfully compiled scripts>
        std::pair<std::int32_t, std::int32_t> compileAll(
            const Compiler::Extensions* extensions, std::int32_t warningsMode);

    }

}
