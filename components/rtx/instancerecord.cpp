#include "instancerecord.hpp"

#include <span>

#include "shaders/scene.h"

namespace Rtx
{
    Transform3x4 toTransform3x4(const osg::Matrixf& matrix)
    {
        Transform3x4 result{};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
                result.mRows[row][column] = matrix(column, row);

            result.mRows[row][3] = matrix(3, row);
        }
        return result;
    }

    void makeInstanceRecords(const SceneDesc& scene, std::vector<InstanceRecord>& records)
    {
        const std::span<const Material> materials = scene.getMaterials();
        const std::span<const MeshInstance> instances = scene.getInstances();

        records.clear();
        records.reserve(instances.size());

        for (const MeshInstance& instance : instances)
        {
            // Null where the instance carries no material, which the untextured test scenes place
            // and which every question below then answers the same way as a plain opaque surface.
            const Material* material = instance.mMaterial == sNoIndex ? nullptr : &materials[instance.mMaterial];
            const bool water = material != nullptr && material->mKind == MaterialKind::Water;

            // **The inverse is taken here and never on the device.** A shader that inverted a
            // transform per hit would do it a million times a frame for an answer that changes once
            // an instance.
            //
            // **And an instance that did not move gets the identity outright, not an inverse times
            // itself.** `inverse(T) * T` is the identity in arithmetic and not in floats: it comes
            // back a few ulps off, and a few ulps of a six-figure world coordinate is a fraction of
            // a pixel of drift under every static surface in the frame.
            const bool moved = instance.mPrevious.has_value() && *instance.mPrevious != instance.mTransform;
            const osg::Matrixf motion
                = moved ? osg::Matrixf::inverse(instance.mTransform) * *instance.mPrevious : osg::Matrixf::identity();

            records.push_back(InstanceRecord{
                .mTransform = toTransform3x4(instance.mTransform),
                .mMotion = toTransform3x4(motion),
                .mMesh = instance.mMesh,
                .mMask = water ? Shaders::MASK_WATER : Shaders::MASK_SOLID,
                .mCutout = material != nullptr && material->isCutout(),
            });
        }
    }

    std::uint32_t countCutouts(std::span<const InstanceRecord> records)
    {
        std::uint32_t count = 0;
        for (const InstanceRecord& record : records)
            if (record.mCutout)
                ++count;

        return count;
    }
}
