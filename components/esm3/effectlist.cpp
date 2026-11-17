#include "effectlist.hpp"

#include <cstddef>
#include <cstdint>
#include <format>

#include <components/esm/attr.hpp>
#include <components/misc/concepts.hpp>

#include "esmreader.hpp"
#include "esmwriter.hpp"
#include "loadmgef.hpp"
#include "loadskil.hpp"

namespace ESM
{
    namespace
    {
        // ENAM format defined by Morrowind.esm
        struct EsmENAMstruct
        {
            std::int16_t mEffectID;
            std::int8_t mSkill, mAttribute;
            std::int32_t mRange, mArea, mDuration, mMagnMin, mMagnMax;
        };

        // Struct with residual binary fields from ENAM
        struct EffectParams
        {
            std::int32_t mRange, mArea, mDuration, mMagnMin, mMagnMax;
        };

        void toBinary(const ENAMstruct& src, EsmENAMstruct& dst)
        {
            auto index = static_cast<std::int16_t>(ESM::MagicEffect::refIdToIndex(src.mEffectID));
            if (index < 0 || index >= ESM::MagicEffect::Length)
                throw std::runtime_error(std::format("Cannot serialize effect {}", src.mEffectID.toDebugString()));
            dst.mEffectID = index;
            dst.mSkill = static_cast<std::int8_t>(ESM::Skill::refIdToIndex(src.mSkill));
            dst.mAttribute = static_cast<std::int8_t>(ESM::Attribute::refIdToIndex(src.mAttribute));
            dst.mRange = src.mRange;
            dst.mArea = src.mArea;
            dst.mDuration = src.mDuration;
            dst.mMagnMin = src.mMagnMin;
            dst.mMagnMax = src.mMagnMax;
        }

        void fromBinary(const EsmENAMstruct& src, ENAMstruct& dst)
        {
            dst.mEffectID = ESM::MagicEffect::indexToRefId(src.mEffectID);
            dst.mSkill = ESM::Skill::indexToRefId(src.mSkill);
            dst.mAttribute = ESM::Attribute::indexToRefId(src.mAttribute);
            dst.mRange = src.mRange;
            dst.mArea = src.mArea;
            dst.mDuration = src.mDuration;
            dst.mMagnMin = src.mMagnMin;
            dst.mMagnMax = src.mMagnMax;
        }

        template <typename T, typename U>
        void setEffectParams(const T& src, U& dst)
        {
            dst.mRange = src.mRange;
            dst.mArea = src.mArea;
            dst.mDuration = src.mDuration;
            dst.mMagnMin = src.mMagnMin;
            dst.mMagnMax = src.mMagnMax;
        }

        template <Misc::SameAsWithoutCvref<EsmENAMstruct> T>
        void decompose(T&& v, const auto& f)
        {
            f(v.mEffectID, v.mSkill, v.mAttribute, v.mRange, v.mArea, v.mDuration, v.mMagnMin, v.mMagnMax);
        }

        template <Misc::SameAsWithoutCvref<EffectParams> T>
        void decompose(T&& v, const auto& f)
        {
            f(v.mRange, v.mArea, v.mDuration, v.mMagnMin, v.mMagnMax);
        }
    }

    void EffectList::load(ESMReader& esm)
    {
        mList.clear();
        while (esm.isNextSub("ENAM"))
        {
            add(esm);
        }
    }

    void EffectList::populate(const std::vector<ENAMstruct>& effects)
    {
        mList.clear();
        for (std::size_t i = 0; i < effects.size(); i++)
            mList.push_back({ effects[i], static_cast<std::uint32_t>(i) });
    }

    void EffectList::updateIndexes()
    {
        for (std::size_t i = 0; i < mList.size(); i++)
            mList[i].mIndex = static_cast<std::uint32_t>(i);
    }

    void EffectList::add(ESMReader& esm)
    {
        ENAMstruct s;
        if (esm.getFormatVersion() <= MaxSerializeEffectRefIdFormatVersion)
        {
            EsmENAMstruct bin{};
            esm.getSubComposite(bin);
            fromBinary(bin, s);
        }
        else
        {
            EffectParams p{};
            esm.getSubComposite(p);
            setEffectParams(p, s);
            s.mEffectID = esm.getHNRefId("ENID");
            s.mSkill = esm.getHNORefId("ENSK");
            s.mAttribute = esm.getHNORefId("ENAT");
        }
        mList.push_back({ s, static_cast<std::uint32_t>(mList.size()) });
    }

    void EffectList::save(ESMWriter& esm) const
    {
        for (const IndexedENAMstruct& enam : mList)
        {
            if (esm.getFormatVersion() <= MaxSerializeEffectRefIdFormatVersion)
            {
                EsmENAMstruct bin{};
                toBinary(enam.mData, bin);
                esm.writeNamedComposite("ENAM", bin);
            }
            else
            {
                if (enam.mData.mEffectID.empty())
                    throw std::runtime_error("Cannot serialize empty effect into ENAM.");
                EffectParams p{};
                setEffectParams(enam.mData, p);
                esm.writeNamedComposite("ENAM", p);
                esm.writeHNRefId("ENID", enam.mData.mEffectID);
                esm.writeHNORefId("ENSK", enam.mData.mSkill);
                esm.writeHNORefId("ENAT", enam.mData.mAttribute);
            }
        }
    }

    bool IndexedENAMstruct::operator!=(const IndexedENAMstruct& rhs) const
    {
        return mData.mEffectID != rhs.mData.mEffectID || mData.mArea != rhs.mData.mArea
            || mData.mRange != rhs.mData.mRange || mData.mSkill != rhs.mData.mSkill
            || mData.mAttribute != rhs.mData.mAttribute || mData.mMagnMin != rhs.mData.mMagnMin
            || mData.mMagnMax != rhs.mData.mMagnMax || mData.mDuration != rhs.mData.mDuration;
    }

} // end namespace
