#pragma once

#include <iosfwd>
#include <span>

namespace RtxBuild
{
    /// `openmw-rtx-build <verb> <arguments>`, `line` being the whole command line: the verb it names run, and the
    /// program's exit status. Nought where the verb ran; two, with what the line took and the usage on `errors`,
    /// where the line names no verb or the wrong count of arguments; one, with the verb's error, where it failed.
    int run(std::span<char* const> line, std::ostream& errors);
}
