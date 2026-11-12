#version 460

#extension GL_GOOGLE_include_directive : require

// The GUI's vertices arrive in clip space already: MyGUI multiplies widget pixels by the view size itself,
// so there is no matrix here and nothing for this shader to do but hand them on.
//
// Except the depth, which is dropped: the pass has no depth attachment and widgets are drawn in the order
// MyGUI hands them over. Vulkan would clip z outside `[0, w]` though, and OpenMW's book page writes minus
// one for every glyph (`mwgui/bookpage.cpp`, built against a GL projection that keeps it).

#include "shared/gui.h"

#include "lib/census.glsl"


layout(location = GUI_ATTRIBUTE_POSITION) in vec3 inPosition;
layout(location = GUI_ATTRIBUTE_COLOUR) in vec4 inColour;
layout(location = GUI_ATTRIBUTE_TEXCOORD) in vec2 inTexCoord;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outTexCoord;

void main()
{
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);

    outColour = inColour;
    outTexCoord = inTexCoord;

    countNotFinite(gl_Position);
    countNotFinite(outColour);
    countNotFinite(outTexCoord);
}
