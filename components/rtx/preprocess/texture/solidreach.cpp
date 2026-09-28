#include "solidreach.hpp"

#include <optional>

#include <components/rtx/image/alphaimage.hpp>

namespace Rtx
{
    void SolidReach::run(const osg::Image&, bool& solid)
    {
        const std::optional<TextureData>& finest = mFinest.get();
        solid = !finest.has_value() || reachesSolid(*finest);
    }
}
