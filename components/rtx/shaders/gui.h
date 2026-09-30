#ifndef OPENMW_COMPONENTS_RTX_SHADERS_GUI_H
#define OPENMW_COMPONENTS_RTX_SHADERS_GUI_H

#include "hosttypes.h"
#include "portable.h"

// What the interface's fragment module is specialized on, for the module that declares it and the
// pass that fills its table.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The module's specialization constants, by `constant_id`: whether the texture it draws holds
    /// premultiplied colour — `Rtx::AlphaForm`.
    const uint GUI_SPEC_PREMULTIPLIED = 0u;
    const uint GUI_SPEC_COUNT = 1u;

#ifdef RTX_HOST
}
#endif

#endif
