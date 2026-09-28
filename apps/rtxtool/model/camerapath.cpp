#include "camerapath.hpp"

#include <algorithm>
#include <cmath>

#include <components/rtx/contract.hpp>

#include "cameratrack.hpp"

namespace RtxTool
{
    namespace
    {
        /// The eight-point Gauss–Legendre rule on [-1, 1]: its nodes' positive half, and their
        /// weights. Exact for a polynomial up to degree fifteen, which a cubic's speed is not: it is
        /// the square root of a quartic, so the rule is taken over halves until the two halves and
        /// the whole agree.
        constexpr std::array<double, 4> sNodes{ 0.1834346424956498, 0.5255324099163290, 0.7966664774136267,
            0.9602898564975363 };
        constexpr std::array<double, 4> sWeights{ 0.3626837833783620, 0.3137066458778873, 0.2223810344533745,
            0.1012285362903763 };

        /// How deep the halving goes before it takes what it has: two to the thirtieth pieces, far
        /// past where a cubic's speed stops changing between two of them.
        constexpr int sMaximumDepth = 30;

        /// How close a point found by length stands to the length asked for, in world units.
        constexpr double sPointTolerance = 1e-6;

        template <class Speed>
        double gaussLegendre(const Speed& speed, const double from, const double to)
        {
            const double middle = 0.5 * (from + to);
            const double half = 0.5 * (to - from);
            double sum = 0.0;
            for (std::size_t at = 0; at < sNodes.size(); ++at)
                sum += sWeights[at] * (speed(middle - half * sNodes[at]) + speed(middle + half * sNodes[at]));
            return half * sum;
        }

        template <class Speed>
        double integrate(const Speed& speed, const double from, const double to, const double whole, const int depth)
        {
            const double middle = 0.5 * (from + to);
            const double left = gaussLegendre(speed, from, middle);
            const double right = gaussLegendre(speed, middle, to);
            if (depth >= sMaximumDepth || std::abs(left + right - whole) <= CameraPath::sLengthTolerance)
                return left + right;
            return integrate(speed, from, middle, left, depth + 1) + integrate(speed, middle, to, right, depth + 1);
        }

        template <class Speed>
        double lengthOver(const Speed& speed, const double from, const double to)
        {
            return integrate(speed, from, to, gaussLegendre(speed, from, to), 0);
        }

        /// The eye beyond `edge` as seen from `inner`, mirrored: what a curve that ends at `edge`
        /// takes its tangent from, so the tangent there is the chord.
        osg::Vec3d mirrored(const osg::Vec3d& edge, const osg::Vec3d& inner)
        {
            return edge * 2.0 - inner;
        }
    }

    osg::Vec3d CameraPath::Segment::pointAt(const double u) const
    {
        return mCoefficients[0] + (mCoefficients[1] + (mCoefficients[2] + mCoefficients[3] * u) * u) * u;
    }

    double CameraPath::Segment::speedAt(const double u) const
    {
        return (mCoefficients[1] + (mCoefficients[2] * 2.0 + mCoefficients[3] * (3.0 * u)) * u).length();
    }

    double CameraPath::Segment::lengthTo(const double to) const
    {
        return to <= 0.0 ? 0.0 : lengthOver([this](const double u) { return speedAt(u); }, 0.0, to);
    }

    CameraPath::CameraPath(const std::span<const TrackKey> keys)
    {
        Rtx::contract(!keys.empty(), "a camera path needs a key");

        const std::size_t count = keys.size();
        mEyes.reserve(count);
        for (const TrackKey& key : keys)
            mEyes.emplace_back(key.mEye);

        const auto eye = [&](const std::size_t at) { return mEyes[at]; };
        const auto goes = [&](const std::size_t segment) { return mEyes[segment] != mEyes[segment + 1]; };

        mRests.resize(count);
        for (std::size_t at = 0; at < count; ++at)
            mRests[at] = at == 0 || at + 1 == count || keys[at].mRests || !goes(at - 1) || !goes(at);

        mSegments.resize(count - 1);
        for (std::size_t at = 0; at + 1 < count; ++at)
        {
            Segment& segment = mSegments[at];
            const osg::Vec3d p1 = eye(at);
            const osg::Vec3d p2 = eye(at + 1);
            if (!goes(at))
            {
                segment.mCoefficients = { p1, osg::Vec3d(), osg::Vec3d(), osg::Vec3d() };
                continue;
            }

            const osg::Vec3d p0 = restsAt(at) ? mirrored(p1, p2) : eye(at - 1);
            const osg::Vec3d p3 = restsAt(at + 1) ? mirrored(p2, p1) : eye(at + 2);

            // Centripetal knot intervals, and the tangents Barry and Goldman's pyramid gives at the
            // two inner knots, times the segment's own interval so they are per unit of `u`.
            const double d01 = std::sqrt((p1 - p0).length());
            const double d12 = std::sqrt((p2 - p1).length());
            const double d23 = std::sqrt((p3 - p2).length());
            const osg::Vec3d m1 = ((p1 - p0) / d01 - (p2 - p0) / (d01 + d12) + (p2 - p1) / d12) * d12;
            const osg::Vec3d m2 = ((p2 - p1) / d12 - (p3 - p1) / (d12 + d23) + (p3 - p2) / d23) * d12;

            segment.mCoefficients = { p1, m1, (p2 - p1) * 3.0 - m1 * 2.0 - m2, (p1 - p2) * 2.0 + m1 + m2 };
            segment.mLength = segment.lengthTo(1.0);
        }
    }

    osg::Vec3d CameraPath::at(const std::size_t segment, const double along) const
    {
        const Segment& piece = mSegments[segment];
        if (piece.mLength <= 0.0 || along <= 0.0)
            return piece.pointAt(0.0);
        if (along >= piece.mLength)
            return piece.pointAt(1.0);

        // Newton on the length, from where an even pace would put it, and bisection wherever a step
        // would leave the bracket the steps before have narrowed.
        double low = 0.0;
        double high = 1.0;
        double u = along / piece.mLength;
        for (int step = 0; step < 64; ++step)
        {
            const double error = piece.lengthTo(u) - along;
            if (std::abs(error) <= sPointTolerance)
                break;

            (error < 0.0 ? low : high) = u;
            const double speed = piece.speedAt(u);
            const double next = speed > 0.0 ? u - error / speed : low - 1.0;
            u = next > low && next < high ? next : 0.5 * (low + high);
        }

        return piece.pointAt(u);
    }
}
