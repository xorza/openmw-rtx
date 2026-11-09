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

    /// The bindless texture array, its companions, a normal map's spread, and the samplers both are
    /// read through — `TEXTURE_BIND_*` below.
    const uint SET_TEXTURES = 1;

    /// The channels the trace writes — `CHANNEL_*` in `gbuffer.h`.
    const uint SET_CHANNELS = 2;

    /// The air in front of the camera — `BIND_FOG_*` in `fogvolume.h`.
    const uint SET_VOLUME = 3;

    /// How many sets any pipeline may name.
    const uint SET_COUNT = 4;

    /// Where the texture set binds its arrays: the textures, their companions at the same slots
    /// (`TextureCompanion`, a normal map's spread), both as images alone, and the samplers a read
    /// pairs either with.
    ///
    /// **Named on both sides because a swap would be silent.** Both image arrays are
    /// `TEXTURE_SLOTS` sampled images, so a layout and a shader that disagreed on which is which
    /// would pass every check the layers make, and the trace would sample companions as colour.
    const uint TEXTURE_BIND_IMAGES = 0;
    const uint TEXTURE_BIND_COMPANIONS = 1;
    const uint TEXTURE_BIND_SAMPLERS = 2;

    /// The samplers at `TEXTURE_BIND_SAMPLERS`: one per wrap a file states (`Rtx::TextureWrap`),
    /// indexed by it, and from `TEXTURE_SAMPLERS_ALONG` the same four filtering along a footprint.
    const uint TEXTURE_SAMPLERS_ALONG = 4;
    const uint TEXTURE_SAMPLERS = 8;

#ifdef RTX_HOST
}
#endif

#endif
