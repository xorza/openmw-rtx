#include "cellid.hpp"

#include <components/misc/concepts.hpp>

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{
    // Found by argument-dependent lookup, which an unnamed namespace would hide.
    // NOLINTBEGIN(misc-use-anonymous-namespace)
    template <Misc::SameAsWithoutCvref<CellId::CellIndex> T>
    static void decompose(T&& v, const auto& f)
    {
        f(v.mX, v.mY);
    }
    // NOLINTEND(misc-use-anonymous-namespace)

    void CellId::load(ESMReader& esm)
    {
        mWorldspace = esm.getHNString("SPAC");

        mIndex.mX = 0;
        mIndex.mY = 0;
        mPaged = esm.getOptionalComposite("CIDX", mIndex);
    }

    void CellId::save(ESMWriter& esm) const
    {
        esm.writeHNString("SPAC", mWorldspace);

        if (mPaged)
            esm.writeNamedComposite("CIDX", mIndex);
    }

    namespace
    {
        struct VisitCellRefId
        {
            CellId operator()(const ESM::EmptyRefId)
            {
                CellId out;
                out.mPaged = true;
                out.mIndex = {};
                return out;
            }

            CellId operator()(const ESM::StringRefId& id)
            {
                CellId out;
                out.mPaged = false;
                out.mWorldspace = id.getValue();
                out.mIndex = { 0, 0 };
                return out;
            }
            CellId operator()(const ESM::ESM3ExteriorCellRefId& id)
            {
                CellId out;
                out.mPaged = true;
                out.mIndex = { id.getX(), id.getY() };
                return out;
            }

            template <typename T>
            CellId operator()(const T& id)
            {
                throw std::runtime_error("cannot extract CellId from this Id type");
            }
        };
    }

    CellId CellId::extractFromRefId(const ESM::RefId& id)
    {
        // This is bad and that code should not be merged.

        return visit(VisitCellRefId(), id);
    }

    bool operator==(const CellId& left, const CellId& right)
    {
        return left.mWorldspace == right.mWorldspace && left.mPaged == right.mPaged
            && (!left.mPaged || (left.mIndex.mX == right.mIndex.mX && left.mIndex.mY == right.mIndex.mY));
    }

    bool operator!=(const CellId& left, const CellId& right)
    {
        return !(left == right);
    }

    bool operator<(const CellId& left, const CellId& right)
    {
        if (left.mPaged < right.mPaged)
            return true;
        if (left.mPaged > right.mPaged)
            return false;

        if (left.mPaged)
        {
            if (left.mIndex.mX < right.mIndex.mX)
                return true;
            if (left.mIndex.mX > right.mIndex.mX)
                return false;

            if (left.mIndex.mY < right.mIndex.mY)
                return true;
            if (left.mIndex.mY > right.mIndex.mY)
                return false;
        }

        return left.mWorldspace < right.mWorldspace;
    }

}
