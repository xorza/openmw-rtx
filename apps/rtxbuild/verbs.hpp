#pragma once

#include <span>

namespace RtxBuild
{
    /// What a verb is handed: its own arguments, past the verb.
    using Arguments = std::span<char* const>;

    /// `guard <module.spv>`: the module rewritten in place by `Rtx::guardFloatArithmetic`, the step between `glslc`
    /// and the optimizer.
    void guard(Arguments arguments);

    /// `pin <module.spv>`: the module rewritten in place by `Rtx::pinFloatArithmetic`, the step every shader takes
    /// before `spirv-val`. In place, as `guard` is, because each is a step over the one file of its stage.
    void pin(Arguments arguments);

    /// `embed <source.cpp> <module.spv>...`: the C++ source that compiles the modules into the program, as
    /// `Rtx::shippedVulkanShaders`.
    void embed(Arguments arguments);

    /// `digest`: `Rtx::digestProgram` of the disassembly on standard input, as 32 hex digits — what `omw kernels`
    /// names each specialized kernel by, after `spirv-dis --raw-id --no-header`.
    void digest(Arguments arguments);

    /// `blue-noise <table.cpp>`: the source that compiles `Rtx::sBlueNoise` in, as `Rtx::makeBlueNoise` makes it.
    void blueNoise(Arguments arguments);

    /// `fog-noise <table.cpp>`: the source that compiles `Rtx::sFogNoise` in, as `Rtx::makeFogNoise` makes it.
    void fogNoise(Arguments arguments);

    /// `specular-albedo <table.cpp>`: the source that compiles `Rtx::sSpecularAlbedo` in, as
    /// `Rtx::makeSpecularAlbedo` makes it.
    void specularAlbedo(Arguments arguments);
}
