#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include "alphaimage.hpp"
#include "contentcache.hpp"
#include "contentpass.hpp"
#include "contentstats.hpp"
#include "shapefold.hpp"
#include "solidreach.hpp"
#include "texelmean.hpp"
#include "texels.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// The one way the renderer computes anything from what the content files hold. Every pass is
    /// asked through here: its key is made of everything it reads, the cache is asked for the
    /// output under that key, and the pass runs only where the cache has none — and then the cache
    /// is offered what it computed. `ContentCache` holds nothing, so for now every pass runs, and
    /// what each cost is counted.
    ///
    /// **One a thread**, as the passes' scratch is: the frame's walk has one and the cell ring's
    /// reader another. Not copyable, because the passes hold its scratch by reference.
    class ContentPreprocessor
    {
    public:
        ContentPreprocessor() = default;

        ContentPreprocessor(const ContentPreprocessor&) = delete;
        ContentPreprocessor& operator=(const ContentPreprocessor&) = delete;

        /// `ShapeFold`: what a drawable's `triangles` come to over `positions`, the kept ones
        /// written to `kept`.
        FoldedShape fold(std::span<const osg::Vec3f> positions, std::span<const std::uint32_t> triangles,
            std::vector<std::uint32_t>& kept);

        /// `SolidReach`: whether `image`'s alpha ever reaches solid.
        bool reachesSolid(const osg::Image& image);

        /// `TexelMean`: what a texel of `image` is worth on average.
        MeanTexel meanTexel(const osg::Image& image);

        /// What every pass cost since the last take, and nothing counted from here on.
        ContentStats takeStats();

    private:
        template <ContentPass Pass>
        void run(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output);

        /// What the two texture passes describe an image into, one after the other.
        AlphaScratch mAlphaScratch;

        ShapeFold mFold;
        SolidReach mSolid{ mAlphaScratch };
        TexelMean mMean{ mAlphaScratch };

        ContentCache mCache;
        ContentStats mStats;
    };
}
