#pragma once

#include <exception>
#include <string>

namespace Rtx::Testing
{
    /// What `run` was refused with, in its own words, or that nothing refused it: a refusal is held to its exact
    /// words, since a type alone passes for a refusal of something else.
    template <class Run>
    std::string refusalOf(Run&& run)
    {
        try
        {
            run();
        }
        catch (const std::exception& refused)
        {
            return refused.what();
        }
        return "nothing was refused";
    }
}
