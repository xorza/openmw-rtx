#pragma once

namespace MWRender
{
    /// Which of a cell's groundcover references `[Groundcover] density` keeps: every one at one, and
    /// otherwise each whose turn takes the running sum to a whole. Shared by the rasterizer's chunks
    /// and the ray tracer's reader, so a density stands the same plants under either renderer.
    class DensityCalculator
    {
    public:
        DensityCalculator(float density)
            : mDensity(density)
        {
        }

        bool isInstanceEnabled()
        {
            if (mDensity >= 1.f)
                return true;

            mCurrentGroundcover += mDensity;
            if (mCurrentGroundcover < 1.f)
                return false;

            mCurrentGroundcover -= 1.f;

            return true;
        }
        void reset() { mCurrentGroundcover = 0.f; }

    private:
        float mCurrentGroundcover = 0.f;
        float mDensity = 0.f;
    };
}
