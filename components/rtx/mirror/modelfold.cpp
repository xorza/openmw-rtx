#include "modelfold.hpp"

#include <cassert>
#include <cstdint>

#include <osg/UserDataContainer>

namespace Rtx
{
    ModelFold::ModelFold(const ModelFold& copy, const osg::CopyOp& op)
        : Resource::DerivedBytes(copy, op)
        , mInput(copy.mInput)
        , mKept(copy.mKept)
        , mNormals(copy.mNormals)
        , mSources(copy.mSources)
        , mShape(copy.mShape)
    {
    }

    const ModelFold* ModelFold::of(const osg::Geometry& geometry)
    {
        const osg::UserDataContainer* const user = geometry.getUserDataContainer();
        if (user == nullptr)
            return nullptr;
        for (std::uint32_t at = 0; at < user->getNumUserObjects(); ++at)
            if (const auto* const fold = dynamic_cast<const ModelFold*>(user->getUserObject(at)))
                return fold;
        return nullptr;
    }

    void ModelFold::keep(osg::Geometry& geometry, const osg::ref_ptr<ModelFold>& fold)
    {
        assert(of(geometry) == nullptr && "a geometry folded twice");
        geometry.getOrCreateUserDataContainer()->addUserObject(fold.get());
    }

    std::uint64_t ModelFold::getBytes() const
    {
        return mKept.size() * sizeof(std::uint32_t) + mNormals.size() * sizeof(osg::Vec3f)
            + mSources.size() * sizeof(std::uint32_t);
    }
}
