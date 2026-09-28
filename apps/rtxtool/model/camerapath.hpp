#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include <osg/Vec3d>

namespace RtxTool
{
    struct TrackKey;

    /// The line a take's eye flies along: through every key's eye, smooth where it passes one and
    /// broken where it stands still, and measured by its length.
    ///
    /// **A centripetal Catmull-Rom spline** (Yuksel, Schaefer and Keyser, 2011): knots spaced by the
    /// square root of the distance between two eyes, which of the Catmull-Rom family is the one
    /// that neither cusps nor loops inside a segment and hugs its eyes where they bunch up. Each
    /// segment is the cubic Hermite between its two eyes with that spline's tangents, scaled to the
    /// segment's own knot interval.
    ///
    /// **Broken where the eye stands still** — at a take's first and last key, at a key that rests,
    /// and on either side of a segment that goes nowhere, a hold or a turn on the spot. The curve
    /// leaves or reaches such a key along the chord to the next, as though an eye stood mirrored
    /// beyond it: the tangent a flight from rest would take, and one no key it never reaches bends.
    ///
    /// **By length, and not by the cubic's own parameter**, because a camera at one speed covers
    /// equal lengths in equal times and the parameter runs faster on the straights than on the
    /// bends. A length is the integral of the speed along the cubic, Gauss–Legendre over halves
    /// until two estimates agree to `sLengthTolerance`, and a point at a length is found by Newton's
    /// method on that integral, kept inside the bracket its own steps narrow.
    class CameraPath
    {
    public:
        /// How closely two estimates of a length agree before one is taken, in world units: a
        /// hundred-millionth of one against segments thousands long.
        static constexpr double sLengthTolerance = 1e-8;

        /// Through `keys`' eyes, in order. Their frames play no part: the line is where the eye
        /// goes, and when is the track's.
        explicit CameraPath(std::span<const TrackKey> keys);

        /// The segments, one fewer than the keys.
        std::size_t getSegments() const { return mSegments.size(); }

        /// The length of the `segment`th, from key `segment` to the next, in world units. Nought
        /// for one that goes nowhere.
        double getLength(std::size_t segment) const { return mSegments[segment].mLength; }

        /// The `key`th's own eye.
        const osg::Vec3d& getEye(std::size_t key) const { return mEyes[key]; }

        /// Whether the eye stands still at the `key`th: an end of the take, a key that rests, or one
        /// beside a segment that goes nowhere.
        bool restsAt(std::size_t key) const { return mRests[key] != 0; }

        /// Where the eye is `along` world units into `segment`, from nought to its length.
        osg::Vec3d at(std::size_t segment, double along) const;

    private:
        /// `c₀ + c₁u + c₂u² + c₃u³`, for `u` from nought at the segment's first eye to one at its last.
        struct Segment
        {
            std::array<osg::Vec3d, 4> mCoefficients;
            double mLength = 0.0;

            osg::Vec3d pointAt(double u) const;

            /// How fast the point moves with `u`: the length of the cubic's derivative.
            double speedAt(double u) const;

            /// The length from `u = 0` to `to`.
            double lengthTo(double to) const;
        };

        std::vector<osg::Vec3d> mEyes;
        std::vector<Segment> mSegments;

        /// Per key, whether the eye stands still there: bytes, since a `std::vector<bool>` is no
        /// container.
        std::vector<char> mRests;
    };
}
