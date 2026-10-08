#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SETS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SETS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// Which descriptor set is which, for the shaders that declare them and the layouts and binds that
// place them.
//
// **A set's number was a literal in the shaders and a place in a list on the host**, and three lists
// had to agree with each other and with the literals: the layouts a pipeline was made with, the sets
// a trace bound, and the first set a bind began at. The layers caught a disagreement only because
// the three shared sets happen to hold different descriptor types, and only in a validated run.
// `Rtx::SharedSets` names each by what it holds, and puts it at the number here, and every stage's
// module is held to every set its layout names (`Rtx::bindingDisagreement`) in every run.
//
// Set zero is each pass's own and is pushed; the others are made once and bound by every pass that
// reads them.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The pass's own bindings, pushed with every dispatch — `bindings.h` and each pass's header
    /// number them.
    const uint SET_PASS = 0;

    /// The bindless texture array and its shading maps — `TEXTURE_BIND_*` below.
    const uint SET_TEXTURES = 1;

    /// The channels the trace writes — `CHANNEL_*` in `gbuffer.h`.
    const uint SET_CHANNELS = 2;

    /// The air in front of the camera — `BIND_FOG_*` in `fogvolume.h`.
    const uint SET_VOLUME = 3;

    /// How many sets any pipeline may name.
    const uint SET_COUNT = 4;

    /// Where the texture set binds its three arrays: the textures, their shading maps at the same
    /// slots, and the textures again through samplers that filter along a footprint.
    ///
    /// **Named on both sides because a swap would be silent.** All are `TEXTURE_SLOTS` combined
    /// image samplers, so a layout and a shader that disagreed on which is which would pass every
    /// check the layers make, and the trace would sample companions as colour.
    const uint TEXTURE_BIND_IMAGES = 0;
    const uint TEXTURE_BIND_COMPANIONS = 1;
    const uint TEXTURE_BIND_ALONG = 2;

#ifdef RTX_HOST
}
#endif

#endif
