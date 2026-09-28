#include "meantexels.hpp"

#include <utility>

#include <osg/Image>

#include "contentpreprocessor.hpp"

namespace Rtx
{
    const MeanTexel& MeanTexels::of(const osg::Image& image)
    {
        if (image.getFileName().empty())
        {
            mUnnamed = mContent.meanTexel(image);
            return mUnnamed;
        }

        // Normalised as the texture table normalises it, so one file under two spellings is one
        // entry. The string is built once per image met and never per ask: a caller keeps the
        // reference, which this never invalidates.
        VFS::Path::Normalized file(image.getFileName());
        if (const auto known = mByFile.find(file); known != mByFile.end())
            return known->second;

        return mByFile.emplace(std::move(file), mContent.meanTexel(image)).first->second;
    }
}
