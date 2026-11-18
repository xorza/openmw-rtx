#pragma once

#include <cstdint>

#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/idcollection.hpp>

namespace CSMDoc
{
    class Messages;
}

namespace ESM
{
    struct Enchantment;
}

namespace CSMTools
{
    /// \brief Make sure that enchantment records are correct
    class EnchantmentCheckStage : public CSMDoc::Stage
    {
        const CSMWorld::IdCollection<ESM::Enchantment>& mEnchantments;
        bool mIgnoreBaseRecords;

    public:
        explicit EnchantmentCheckStage(const CSMWorld::IdCollection<ESM::Enchantment>& enchantments);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
