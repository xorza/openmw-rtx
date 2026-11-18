#pragma once

#include <osg/ref_ptr>

#include <components/spt/file.hpp>

namespace osg
{
    class Node;
}

namespace SptOsg
{
    class Loader
    {
    public:
        static osg::ref_ptr<osg::Node> load(const Spt::SPTFile& file);
    };
}
