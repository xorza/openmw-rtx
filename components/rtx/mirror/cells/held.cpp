#include "held.hpp"

#include <cassert>
#include <utility>

#include <components/crashcatcher/crash.hpp>

namespace Rtx
{
    namespace
    {
        /// The models a ring of cells names at the most: Seyda Neen's, the busiest landing met, is
        /// 908.
        constexpr std::size_t sModelBudget = 4096;
    }

    CellHolds::CellHolds()
    {
        mModels.reserve(sModelBudget);
    }

    CellHolds::HeldModel& CellHolds::know(PreparedModel& model)
    {
        // One search either way: a cell arriving asks this once per model it names.
        const auto [at, fresh] = mModels.try_emplace(&model);
        if (fresh)
        {
            at->second = mSpareModels.take();
            at->second.mModel = &model;
        }
        return at->second;
    }

    CellHolds::HeldModel& CellHolds::knownOf(const PreparedModel& model)
    {
        const auto known = mModels.find(&model);
        Crash::contract(known != mModels.end(), "a model read that the frame does not know of");

        return known->second;
    }

    void CellHolds::adoptParts(HeldModel& held, SceneAdopter& into)
    {
        const PreparedModel& model = *held.mModel;
        held.mParts.reserve(model.mParts.size());

        for (const PreparedPart& part : model.mParts)
        {
            const MaterialResolver::Resolved material = into.adoptMaterial(part.mMaterial, model.chainOf(part));
            const Index mesh = into.adoptMesh(*part.mDrawable, model.readingOf(part));

            held.mParts.push_back(AdoptedPart{
                .mMesh = mesh,
                .mMaterial = material.mIndex,
                .mDrawable = part.mDrawable.get(),
                .mKey = material.mKey,
            });
        }
    }

    void CellHolds::release(PreparedModel& model)
    {
        const auto known = mModels.find(&model);
        Crash::contract(known != mModels.end(), "a model released that the frame does not know of");

        HeldModel& held = known->second;
        assert(held.mNamed > 0 && "a model released by more cells than named it");
        if (--held.mNamed > 0)
            return;

        mReleasing.insert(mReleasing.end(), held.mParts.begin(), held.mParts.end());
        held.reuse();
        mSpareModels.give(std::move(held));
        mModels.erase(known);
    }

    void CellHolds::releaseParts(SceneAdopter& into)
    {
        for (const AdoptedPart& part : mReleasing)
        {
            into.releaseMesh(*part.mDrawable);
            into.releaseMaterial(part.mKey);
        }

        mReleasing.clear();
    }

    void CellHolds::forget()
    {
        // The holds on the parts outlive the models they were adopted from, which die with the
        // reader: `AdoptedPart` says why it keeps what the release needs.
        for (auto& [model, held] : mModels)
        {
            mReleasing.insert(mReleasing.end(), held.mParts.begin(), held.mParts.end());

            // The row's room is kept for the next world's models, as `release` keeps a row's.
            held.reuse();
            mSpareModels.give(std::move(held));
        }

        mModels.clear();
    }
}
