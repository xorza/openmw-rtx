#pragma once

#include <components/esm3/loadinfo.hpp>

namespace MWDialogue
{
    class SelectWrapper
    {
        const ESM::DialogueCondition& mSelect;

    public:
        enum Type
        {
            Type_None,
            Type_Integer,
            Type_Numeric,
            Type_Boolean,
            Type_Inverted
        };

    public:
        // A condition is read through its wrapper wherever one is asked for.
        // NOLINTNEXTLINE(google-explicit-constructor)
        SelectWrapper(const ESM::DialogueCondition& select);

        ESM::DialogueCondition::Function getFunction() const;

        int getArgument() const;

        Type getType() const;

        bool selectCompare(int value) const;

        bool selectCompare(float value) const;

        bool selectCompare(bool value) const;

        std::string getName() const;
        ///< Return case-smashed name.

        std::string_view getCellName() const;

        ESM::RefId getId() const;
    };
}
