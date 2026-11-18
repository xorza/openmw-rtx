#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <shared_mutex>
#include <thread>
#include <unordered_set>

#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>

#include <components/misc/budgetmeasurement.hpp>
#include <components/misc/framestats.hpp>

#include "physicssystem.hpp"
#include "ptrholder.hpp"

namespace Misc
{
    class Barrier;
}

namespace MWPhysics
{
    enum class LockingPolicy
    {
        NoLocks,
        ExclusiveLocksOnly,
        AllowSharedLocks,
    };

    class PhysicsTaskScheduler
    {
    public:
        PhysicsTaskScheduler(float physicsDt, btCollisionWorld* collisionWorld);
        ~PhysicsTaskScheduler();

        /// @brief move actors taking into account desired movements and collisions
        /// @param numSteps how much simulation step to run
        /// @param timeAccum accumulated time from previous run to interpolate movements
        /// @param actorsData per actor data needed to compute new positions
        /// @return new position of each actor
        void applyQueuedMovements(float& timeAccum, std::vector<Simulation>& simulations,
            Misc::FrameClock::time_point statsEpoch, std::uint32_t frameNumber, Misc::FrameStats& stats);

        void resetSimulation(const ActorMap& actors);

        // Thread safe wrappers
        void rayTest(const btVector3& rayFromWorld, const btVector3& rayToWorld,
            btCollisionWorld::RayResultCallback& resultCallback) const;
        void convexSweepTest(const btConvexShape* castShape, const btTransform& from, const btTransform& to,
            btCollisionWorld::ConvexResultCallback& resultCallback) const;
        void contactTest(btCollisionObject* colObj, btCollisionWorld::ContactResultCallback& resultCallback);
        std::optional<btVector3> getHitPoint(const btTransform& from, btCollisionObject* target);
        void aabbTest(const btVector3& aabbMin, const btVector3& aabbMax, btBroadphaseAabbCallback& callback);
        void getAabb(const btCollisionObject* obj, btVector3& min, btVector3& max);
        void setCollisionFilterMask(btCollisionObject* collisionObject, std::int32_t collisionFilterMask);
        void addCollisionObject(
            btCollisionObject* collisionObject, std::int32_t collisionFilterGroup, std::int32_t collisionFilterMask);
        void removeCollisionObject(btCollisionObject* collisionObject);
        void updateSingleAabb(const std::shared_ptr<PtrHolder>& ptr, bool immediate = false);
        bool getLineOfSight(const std::shared_ptr<Actor>& actor1, const std::shared_ptr<Actor>& actor2);
        void debugDraw();
        void* getUserPointer(const btCollisionObject* object) const;
        void releaseSharedStates(); // destroy all objects whose destructor can't be safely called from
                                    // ~PhysicsTaskScheduler()

    private:
        class WorkersSync;

        void doSimulation();
        void worker();
        void updateActorsPositions();
        bool hasLineOfSight(const Actor* actor1, const Actor* actor2);
        void refreshLOSCache();
        void updateAabbs();
        void updatePtrAabb(const std::shared_ptr<PtrHolder>& ptr);
        void updateStats(Misc::FrameClock::time_point statsEpoch, std::uint32_t frameNumber, Misc::FrameStats& stats);
        std::tuple<std::uint32_t, float> calculateStepConfig(float timeAccum) const;
        void afterPreStep();
        void afterPostStep();
        void afterPostSim();
        void syncWithMainThread();
        void waitForWorkers();
        void prepareWork(float& timeAccum, std::vector<Simulation>& simulations,
            Misc::FrameClock::time_point statsEpoch, std::uint32_t frameNumber, Misc::FrameStats& stats);

        std::unique_ptr<WorldFrameData> mWorldFrameData;
        std::vector<Simulation>* mSimulations = nullptr;
        std::unordered_set<const btCollisionObject*> mCollisionObjects;
        float mDefaultPhysicsDt;
        float mPhysicsDt;
        float mTimeAccum;
        btCollisionWorld* mCollisionWorld;
        std::vector<LOSRequest> mLOSCache;
        std::set<std::weak_ptr<PtrHolder>, std::owner_less<std::weak_ptr<PtrHolder>>> mUpdateAabb;

        // TODO: use std::experimental::flex_barrier or std::barrier once it becomes a thing
        std::unique_ptr<Misc::Barrier> mPreStepBarrier;
        std::unique_ptr<Misc::Barrier> mPostStepBarrier;
        std::unique_ptr<Misc::Barrier> mPostSimBarrier;

        LockingPolicy mLockingPolicy;
        std::uint32_t mNumThreads;
        std::int32_t mNumJobs;
        std::uint32_t mRemainingSteps;
        std::int32_t mLOSCacheExpiry;
        bool mAdvanceSimulation;
        std::atomic<std::int32_t> mNextJob;
        std::atomic<std::int32_t> mNextLOS;
        std::vector<std::thread> mThreads;

        mutable std::shared_mutex mSimulationMutex;
        mutable std::shared_mutex mCollisionWorldMutex;
        mutable std::shared_mutex mLOSCacheMutex;
        mutable std::mutex mUpdateAabbMutex;

        std::uint32_t mFrameNumber;

        std::uint32_t mPrevStepCount;
        Misc::BudgetMeasurement mBudget;
        Misc::BudgetMeasurement mAsyncBudget;
        std::uint32_t mBudgetCursor;
        Misc::FrameClock::time_point mAsyncStartTime;
        Misc::FrameClock::time_point mTimeBegin;
        Misc::FrameClock::time_point mTimeEnd;
        Misc::FrameClock::time_point mStatsEpoch;

        std::unique_ptr<WorkersSync> mWorkersSync;
    };

}
