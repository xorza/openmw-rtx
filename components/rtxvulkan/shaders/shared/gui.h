#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_GUI_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_GUI_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What the interface's modules are specialized on and fed, for the modules that declare it and the
// pass that fills their tables.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The texture a batch draws with — the pass's one binding, `bindings.h`'s rule.
    const uint GUI_BIND_TEXTURE = 0;

    /// The module's specialization constants, by `constant_id`: whether the texture it draws holds
    /// premultiplied colour — `Rtx::AlphaForm`.
    const uint GUI_SPEC_PREMULTIPLIED = 0u;
    const uint GUI_SPEC_COUNT = 1u;

    /// Where the vertex module reads each of a vertex's attributes, and the pass describes them.
    const uint GUI_ATTRIBUTE_POSITION = 0u;
    const uint GUI_ATTRIBUTE_COLOUR = 1u;
    const uint GUI_ATTRIBUTE_TEXCOORD = 2u;

#ifdef RTX_HOST
}
#endif

#endif
