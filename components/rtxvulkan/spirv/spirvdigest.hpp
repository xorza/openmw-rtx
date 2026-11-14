#pragma once

#include <string_view>

#include <components/misc/digestwords.hpp>

namespace Rtx
{
    /// A digest of the program a module holds, read off `spirv-dis --raw-id --no-header`: two modules digest alike when
    /// they hold the same instructions, whatever ids they number them by and in whatever order they declare their
    /// types, constants and variables.
    ///
    /// Its own canonical form, because `spirv-opt --canonicalize-ids` (a `spirv-remap` port made for compression)
    /// hashes a type from part of its operands and settles a collision by declaration order, so an unchanged kernel
    /// came out with renumbered types. A global is named by its opcode, literals and decorations, refined round by
    /// round by what its operands are named (colour refinement, which reads no id and no order); an id a function
    /// defines is named by where it is defined. What has no order is sorted. The digest is `MurmurHash3_x64_128`'s, as
    /// `digestShaders`' is. Instructions and not values; debug instructions are skipped.
    ///
    /// Throws `Rtx::InputError` for a line it cannot read and for an id nothing defines, naming it.
    Misc::DigestWords digestProgram(std::string_view disassembly);
}
