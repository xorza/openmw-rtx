#include "instancerecord.hpp"

#include <span>

#include "shaders/scene.h"

namespace Rtx
{
    namespace
    {
        /// The motion of something that has not moved, written out rather than built from a matrix.
        constexpr Transform3x4 sStillTransform{ { { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f } } };
    }

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

        // **Resized and not cleared.** `clear` plus `resize` writes the whole array twice — once
        // with zeroes and once with the records — and at a hundred bytes a slot over fifty thousand
        // slots that is five megabytes of pointless stores a frame. The buffer is a caller's scratch
        // and keeps its size between frames; only a scene that grew or shrank pays anything here.
        records.resize(instances.size());

        for (std::size_t slot = 0; slot < instances.size(); ++slot)
        {
            const MeshInstance& instance = instances[slot];
            if (!instance.isPlaced())
            {
                records[slot].mPlaced = false;
                continue;
            }

            // Null where the instance carries no material, which the untextured test scenes place
            // and which every question below then answers the same way as a plain opaque surface.
            const Material* material = instance.mMaterial == sNoIndex ? nullptr : &materials[instance.mMaterial];
            const bool water = material != nullptr && material->mKind == MaterialKind::Water;

            records[slot] = InstanceRecord{
                .mTransform = toTransform3x4(instance.mTransform),
                // Overwritten below for the few that moved.
                .mMotion = sStillTransform,
                .mMesh = instance.mMesh,
                .mMask = water ? Shaders::MASK_WATER : Shaders::MASK_SOLID,
                .mCutout = material != nullptr && material->isCutout(),
                .mPlaced = true,
            };
        }

        // **The scene says what moved rather than every record being asked.** Comparing each
        // placement against where it was costs sixteen floats per instance per frame to discover
        // that a world of statics is still a world of statics; the walk that placed them already
        // knew, and this is where it is spent instead.
        //
        // **The inverse is taken here and never on the device.** A shader that inverted a transform
        // per hit would do it a million times a frame for an answer that changes once an instance.
        const std::span<const osg::Matrixf> previous = scene.getPrevious();
        for (const Index slot : scene.getMoved())
        {
            const MeshInstance& instance = instances[slot];
            if (!instance.isPlaced())
                continue;

            // **A slot can be on this list without having moved**: one just placed is here because
            // something has to upload it, and its previous transform is where it already is. It
            // takes the identity outright rather than an inverse times itself — `inverse(T) * T` is
            // the identity in arithmetic and not in floats, and a few ulps of a six-figure world
            // coordinate is a fraction of a pixel of drift under a static surface.
            if (previous[slot] == instance.mTransform)
                continue;

            records[slot].mMotion = toTransform3x4(osg::Matrixf::inverse(instance.mTransform) * previous[slot]);
        }
    }

    std::uint32_t countCutouts(std::span<const InstanceRecord> records)
    {
        std::uint32_t count = 0;
        for (const InstanceRecord& record : records)
            if (record.mPlaced && record.mCutout)
                ++count;

        return count;
    }
}
