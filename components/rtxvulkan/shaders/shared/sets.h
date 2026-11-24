#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SETS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SETS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/scene.h>

// Which descriptor set is which, for the shaders that declare them and the layouts and binds that
// place them.
//
// **One number per set, shared by both sides**: a literal in the shaders and a place in a list on the host
// meant the layouts a pipeline was made with, the sets a trace bound and the first set a bind began at had to
// agree, and the layers caught a disagreement only in a validated run, since the three shared sets happen to
// hold different descriptor types. `Rtx::SharedSets` names each by what it holds and puts it at the number
// here, and every stage's module is held to every set its layout names (`Rtx::bindingDisagreement`) in every
// run.

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

    /// Where the texture set binds its two arrays: the samplers a read pairs an image with, and the images, every
    /// slot's texture and after them every slot's companion (`TextureCompanion`, a normal map's spread).
    ///
    /// **No binding's offset grows with `TEXTURE_SLOTS`**: RADV lays a set's bindings out in the order of their
    /// numbers (`vk_create_sorted_bindings`), and an offset past what a scalar load's immediate reaches (under 1 MiB
    /// on AMD's RDNA, `smem_offset_max` in ACO) is added in a register of every load. So the samplers' fixed eight
    /// first, and one image array last: an array after another stood `TEXTURE_SLOTS` descriptors in for each before
    /// it, and at 16384 slots the third started 1.5 MiB in, where AMD's layers' kernel spilled
    /// (`docs/research/2026-10-22-amd-integrated-spill-ceilings.md`).
    const uint TEXTURE_BIND_SAMPLERS = 0;
    const uint TEXTURE_BIND_IMAGES = 1;

    /// How many images a slot takes in `TEXTURE_BIND_IMAGES`: its texture and its companion.
    const uint TEXTURE_IMAGES_A_SLOT = 2;

    /// Where `slot`'s texture stands in `TEXTURE_BIND_IMAGES`: at the slot, so a texture's read indexes by the slot
    /// alone.
    RTX_CONSTEXPR uint textureImageOf(uint slot)
    {
        return slot;
    }

    /// Where `slot`'s companion stands in `TEXTURE_BIND_IMAGES`: `TEXTURE_SLOTS` on, which a read adds in its index
    /// and no binding's offset holds. **After the textures and not beside each**: a slot's pair side by side cost
    /// NVIDIA's bounce two registers over its ceiling, 114 against 112 with the sea and a mod's maps, and RADV's
    /// counts were the same either way.
    RTX_CONSTEXPR uint companionImageOf(uint slot)
    {
        return TEXTURE_SLOTS + slot;
    }

    /// The samplers at `TEXTURE_BIND_SAMPLERS`: one per wrap a file states (`Rtx::TextureWrap`),
    /// indexed by it, and from `TEXTURE_SAMPLERS_ALONG` the same four filtering along a footprint.
    const uint TEXTURE_SAMPLERS_ALONG = 4;
    const uint TEXTURE_SAMPLERS = 8;

#ifdef RTX_HOST
}
#endif

#endif
