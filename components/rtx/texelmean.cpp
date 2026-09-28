#include "texelmean.hpp"

#include <optional>

#include "alphaimage.hpp"

namespace Rtx
{
    void TexelMean::run(const osg::Image&, MeanTexel& mean)
    {
        const std::optional<TextureData>& finest = mFinest.get();
        mean = finest.has_value() ? meanTexel(*finest, mFinest.getScratch()) : MeanTexel();
    }
}
