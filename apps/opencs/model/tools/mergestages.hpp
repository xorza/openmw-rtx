#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <components/toutf8/toutf8.hpp>

#include "mergestate.hpp"

namespace CSMDoc
{
    class Messages;
}

namespace CSMTools
{
    class StartMergeStage : public CSMDoc::Stage
    {
        MergeState& mState;

    public:
        explicit StartMergeStage(MergeState& state);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };

    class FinishMergedDocumentStage : public CSMDoc::Stage
    {
        MergeState& mState;
        ToUTF8::Utf8Encoder mEncoder;

    public:
        FinishMergedDocumentStage(MergeState& state, ToUTF8::FromType encoding);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };

    template <typename RecordType, typename Collection = CSMWorld::IdCollection<RecordType>>
    class MergeIdCollectionStage : public CSMDoc::Stage
    {
        MergeState& mState;
        Collection& (CSMWorld::Data::*mAccessor)();

    public:
        MergeIdCollectionStage(MergeState& state, Collection& (CSMWorld::Data::*accessor)());

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };

    template <typename RecordType, typename Collection>
    MergeIdCollectionStage<RecordType, Collection>::MergeIdCollectionStage(
        MergeState& state, Collection& (CSMWorld::Data::*accessor)())
        : mState(state)
        , mAccessor(accessor)
    {
    }

    template <typename RecordType, typename Collection>
    std::int32_t MergeIdCollectionStage<RecordType, Collection>::setup()
    {
        return (mState.mSource.getData().*mAccessor)().getSize();
    }

    template <typename RecordType, typename Collection>
    void MergeIdCollectionStage<RecordType, Collection>::perform(std::int32_t stage, CSMDoc::Messages& messages)
    {
        const Collection& source = (mState.mSource.getData().*mAccessor)();
        Collection& target = (mState.mTarget->getData().*mAccessor)();

        const CSMWorld::Record<RecordType>& record = source.getRecord(stage);

        if (!record.isDeleted())
            target.appendRecord(std::make_unique<CSMWorld::Record<RecordType>>(
                CSMWorld::Record<RecordType>(CSMWorld::RecordBase::State_ModifiedOnly, nullptr, &record.get())));
    }

    class MergeRefIdsStage : public CSMDoc::Stage
    {
        MergeState& mState;

    public:
        explicit MergeRefIdsStage(MergeState& state);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };

    class MergeReferencesStage : public CSMDoc::Stage
    {
        MergeState& mState;
        std::map<ESM::RefId, std::int32_t> mIndex;

    public:
        explicit MergeReferencesStage(MergeState& state);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };

    /// Adds all land texture records that could potentially be referenced when merging
    class PopulateLandTexturesMergeStage : public CSMDoc::Stage
    {
        MergeState& mState;

    public:
        explicit PopulateLandTexturesMergeStage(MergeState& state);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };

    class MergeLandStage : public CSMDoc::Stage
    {
        MergeState& mState;

    public:
        explicit MergeLandStage(MergeState& state);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };

    /// During this stage, the complex process of combining LandTextures from
    /// potentially multiple plugins is undertaken.
    class FixLandsAndLandTexturesMergeStage : public CSMDoc::Stage
    {
        MergeState& mState;

    public:
        explicit FixLandsAndLandTexturesMergeStage(MergeState& state);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };

    /// Removes base LandTexture records. This gets rid of the base records previously
    /// needed in FixLandsAndLandTexturesMergeStage.
    class CleanupLandTexturesMergeStage : public CSMDoc::Stage
    {
        MergeState& mState;

    public:
        explicit CleanupLandTexturesMergeStage(MergeState& state);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this stage will be appended to \a messages.
    };
}
