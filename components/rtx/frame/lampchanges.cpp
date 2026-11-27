#include "lampchanges.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <tuple>

namespace Rtx
{
    namespace
    {
        using Shaders::GpuLampChange;
        using Shaders::GpuLampSphere;

        /// The order the two frames' lamps are merged in: any order that tells two lamps apart, and holds the
        /// same lamp the same.
        bool byPlace(const GpuLampSphere& a, const GpuLampSphere& b)
        {
            return std::tie(a.mCentre, a.mReach, a.mSource) < std::tie(b.mCentre, b.mReach, b.mSource);
        }

        /// How far from `eye` the light of `sphere` begins: nought inside its reach.
        float nearness(const GpuLampSphere& sphere, const osg::Vec3f& eye)
        {
            return std::max((sphere.mCentre - eye).length() - sphere.mReach, 0.0f);
        }

        /// No lamp where `sphere` is: what a change from or to nothing stands for, at the same place so the
        /// shader's arithmetic on it stays finite.
        GpuLampSphere absentAt(const GpuLampSphere& sphere)
        {
            return GpuLampSphere{ .mCentre = sphere.mCentre, .mReach = 0.0f, .mSource = sphere.mSource };
        }

        /// Puts the `Shaders::LAMP_CHANGES_MOST` of `items` that `near` puts nearest first, in that order, ties
        /// broken by `key`, and drops the rest.
        template <class T, class Near, class Key>
        void keepNearest(std::vector<T>& items, Near near, Key key)
        {
            const std::size_t kept = std::min<std::size_t>(items.size(), Shaders::LAMP_CHANGES_MOST);
            std::partial_sort(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(kept), items.end(),
                [&](const T& a, const T& b) {
                    const float nearA = near(a);
                    const float nearB = near(b);
                    if (nearA != nearB)
                        return nearA < nearB;
                    return key(a, b);
                });
            items.resize(kept);
        }
    }

    void LampChanges::see(const std::span<const Light> lights)
    {
        mSeen.clear();
        mSeen.reserve(lights.size());
        for (const Light& light : lights)
            mSeen.push_back(
                GpuLampSphere{ .mCentre = light.mPosition, .mReach = light.mReach, .mSource = light.mSourceRadius });
    }

    std::span<const GpuLampChange> LampChanges::spend(const osg::Vec3f& eye)
    {
        std::sort(mSeen.begin(), mSeen.end(), byPlace);

        mWent.clear();
        mWent.reserve(mSpent.size());
        std::set_difference(
            mSpent.begin(), mSpent.end(), mSeen.begin(), mSeen.end(), std::back_inserter(mWent), byPlace);
        mCame.clear();
        mCame.reserve(mSeen.size());
        std::set_difference(
            mSeen.begin(), mSeen.end(), mSpent.begin(), mSpent.end(), std::back_inserter(mCame), byPlace);
        mSpent.assign(mSeen.begin(), mSeen.end());

        // **The nearest of each side before they are paired**, which bounds the pairing's work: a change is
        // kept by the nearer of its two lamps, which is among these wherever the change is. Its partner may
        // not be, and a lamp whose partner was cut stands alone, which drops the history where it moved whole
        // and not by how far it moved.
        const auto sphereNearness = [&](const GpuLampSphere& sphere) { return nearness(sphere, eye); };
        keepNearest(mWent, sphereNearness, byPlace);
        keepNearest(mCame, sphereNearness, byPlace);

        mChanges.clear();
        mChanges.reserve(mWent.size() + mCame.size());
        mPaired.assign(mCame.size(), false);
        for (const GpuLampSphere& went : mWent)
        {
            std::optional<std::size_t> partner;
            float partnerDistance = std::numeric_limits<float>::infinity();
            for (std::size_t came = 0; came < mCame.size(); ++came)
            {
                const float distance = (mCame[came].mCentre - went.mCentre).length();
                if (!mPaired[came] && distance < went.mReach + mCame[came].mReach && distance < partnerDistance)
                {
                    partner = came;
                    partnerDistance = distance;
                }
            }

            if (partner.has_value())
                mPaired[*partner] = true;
            mChanges.push_back(
                GpuLampChange{ .mFrom = went, .mTo = partner.has_value() ? mCame[*partner] : absentAt(went) });
        }
        for (std::size_t came = 0; came < mCame.size(); ++came)
            if (!mPaired[came])
                mChanges.push_back(GpuLampChange{ .mFrom = absentAt(mCame[came]), .mTo = mCame[came] });

        keepNearest(
            mChanges,
            [&](const GpuLampChange& change) {
                return std::min(nearness(change.mFrom, eye), nearness(change.mTo, eye));
            },
            [](const GpuLampChange& a, const GpuLampChange& b) {
                if (byPlace(a.mFrom, b.mFrom) || byPlace(b.mFrom, a.mFrom))
                    return byPlace(a.mFrom, b.mFrom);
                return byPlace(a.mTo, b.mTo);
            });

        for (GpuLampChange& change : mChanges)
        {
            change.mFrom.mCentre -= eye;
            change.mTo.mCentre -= eye;
        }
        return mChanges;
    }
}
