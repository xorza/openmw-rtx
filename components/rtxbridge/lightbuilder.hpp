#ifndef OPENMW_COMPONENTS_RTXBRIDGE_LIGHTBUILDER_H
#define OPENMW_COMPONENTS_RTXBRIDGE_LIGHTBUILDER_H

#include <optional>

#include <osg/Vec3f>

#include <components/rtx/scenedesc.hpp>

namespace ESM
{
    struct Light;
}

namespace RtxBridge
{
    /// The light a `LIGH` reference casts, or nothing where it casts none.
    ///
    /// Three kinds of record place no light. A **carried** one is a torch in a pack, lit only when
    /// something equips it. An **off by default** one does not burn while it sits in a cell. And a
    /// **negative** one *subtracts* illumination — a trick available to a renderer accumulating into
    /// a framebuffer and meaningless to anything that traces a ray to an emitter.
    std::optional<Rtx::Light> makeLight(const ESM::Light& record, const osg::Vec3f& position);

    /// A colour as the content files store one, decoded.
    ///
    /// Morrowind's colours are display-encoded, and the light transport downstream is linear. The
    /// two differ most in the middle, so mid grey is where a renderer that skips this is most
    /// obviously wrong and where a test pins it.
    osg::Vec3f decodeColour(std::uint32_t packed);
}

#endif
