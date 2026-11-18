#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include <components/debug/debuglog.hpp>

namespace DetourNavigator
{
    struct RecastSettings
    {
        float mCellHeight = 0;
        float mCellSize = 0;
        float mDetailSampleDist = 0;
        float mDetailSampleMaxError = 0;
        float mMaxClimb = 0;
        float mMaxSimplificationError = 0;
        float mMaxSlope = 0;
        float mRecastScaleFactor = 0;
        float mSwimHeightScale = 0;
        std::int32_t mBorderSize = 0;
        std::int32_t mMaxEdgeLen = 0;
        std::int32_t mMaxVertsPerPoly = 0;
        std::int32_t mRegionMergeArea = 0;
        std::int32_t mRegionMinArea = 0;
        std::int32_t mTileSize = 0;
        Debug::Level mMaxLogLevel = Debug::Error;
    };

    struct DetourSettings
    {
        std::int32_t mMaxPolys = 0;
        std::int32_t mMaxNavMeshQueryNodes = 0;
        std::size_t mMaxPolygonPathSize = 0;
        std::size_t mMaxSmoothPathSize = 0;
    };

    struct Settings
    {
        bool mEnableWriteRecastMeshToFile = false;
        bool mEnableWriteNavMeshToFile = false;
        bool mEnableRecastMeshFileNameRevision = false;
        bool mEnableNavMeshFileNameRevision = false;
        bool mEnableNavMeshDiskCache = false;
        RecastSettings mRecast;
        DetourSettings mDetour;
        std::int32_t mWaitUntilMinDistanceToPlayer = 0;
        std::int32_t mMaxTilesNumber = 0;
        std::size_t mAsyncNavMeshUpdaterThreads = 0;
        std::string mRecastMeshPathPrefix;
        std::string mNavMeshPathPrefix;
        std::chrono::milliseconds mMinUpdateInterval{};
    };

    Settings makeSettingsFromSettingsManager(Debug::Level maxLogLevel);
}
