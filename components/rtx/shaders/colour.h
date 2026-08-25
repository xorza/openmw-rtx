// `#pragma once` everywhere else in this tree, and an include guard here for the reason
// `portable.h` gives.
#ifndef OPENMW_COMPONENTS_RTX_SHADERS_COLOUR_H
#define OPENMW_COMPONENTS_RTX_SHADERS_COLOUR_H

#include "portable.h"

#ifdef RTX_HOST

#include <osg/Vec3f>

namespace Rtx::Shaders
{
    using vec3 = osg::Vec3f;

#endif

    /// How this renderer weighs a colour into a brightness.
    ///
    /// Rec. 709, which is what these primaries are.
    ///
    /// **Shared because two shaders now decide something by it**, and a pair of weights that
    /// disagreed would be two different ideas of which of two things is brighter. The exposure
    /// histogram measures the frame with it; the trace asks whether the sprites over a pixel put
    /// more light into it than the surface behind them left.
    RTX_CONST vec3 LUMINANCE_WEIGHTS = vec3(0.2126f, 0.7152f, 0.0722f);

#ifdef RTX_HOST
}
#endif

#endif
