#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>

#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm/refid.hpp>
#include <components/interpreter/context.hpp>

#include "globalscripts.hpp"

namespace MWScript
{
    class Locals;

    class MissingImplicitRefError : public std::runtime_error
    {
    public:
        MissingImplicitRefError();
    };

    class InterpreterContext : public Interpreter::Context
    {
        Locals* mLocals;
        mutable MWWorld::Ptr mReference;
        std::shared_ptr<GlobalScriptDesc> mGlobalScriptDesc;

        /// If \a id is empty, a reference the script is run from is returned or in case
        /// of a non-local script the reference derived from the target ID.
        MWWorld::Ptr getReferenceImp(
            const ESM::RefId& id = ESM::RefId(), bool activeOnly = false, bool doThrow = true) const;

        const Locals& getMemberLocals(bool global, ESM::RefId& id) const;
        ///< \a id is changed to the respective script ID, if \a id wasn't a script ID before

        Locals& getMemberLocals(bool global, ESM::RefId& id);
        ///< \a id is changed to the respective script ID, if \a id wasn't a script ID before

        /// Throws an exception if local variable can't be found.
        std::int32_t findLocalVariableIndex(const ESM::RefId& scriptId, std::string_view name, char type) const;

    public:
        explicit InterpreterContext(std::shared_ptr<GlobalScriptDesc> globalScriptDesc);

        InterpreterContext(MWScript::Locals* locals, const MWWorld::Ptr& reference);
        ///< The ownership of \a locals is not transferred. 0-pointer allowed.

        ESM::RefId getTarget() const override;

        std::int32_t getLocalShort(std::int32_t index) const override;

        std::int32_t getLocalLong(std::int32_t index) const override;

        float getLocalFloat(std::int32_t index) const override;

        void setLocalShort(std::int32_t index, std::int32_t value) override;

        void setLocalLong(std::int32_t index, std::int32_t value) override;

        void setLocalFloat(std::int32_t index, float value) override;

        using Interpreter::Context::messageBox;

        void messageBox(std::string_view message, const std::vector<std::string>& buttons) override;

        void report(const std::string& message) override;
        ///< By default, do nothing.

        std::int32_t getGlobalShort(std::string_view name) const override;

        std::int32_t getGlobalLong(std::string_view name) const override;

        float getGlobalFloat(std::string_view name) const override;

        void setGlobalShort(std::string_view name, std::int32_t value) override;

        void setGlobalLong(std::string_view name, std::int32_t value) override;

        void setGlobalFloat(std::string_view name, float value) override;

        std::vector<std::string> getGlobals() const override;

        char getGlobalType(std::string_view name) const override;

        std::string getActionBinding(std::string_view action) const override;

        std::string_view getActorName() const override;

        std::string_view getNPCRace() const override;

        std::string_view getNPCClass() const override;

        std::string_view getNPCFaction() const override;

        std::string_view getNPCRank() const override;

        std::string_view getPCName() const override;

        std::string_view getPCRace() const override;

        std::string_view getPCClass() const override;

        std::string_view getPCRank() const override;

        std::string_view getPCNextRank() const override;

        std::int32_t getPCBounty() const override;

        std::string_view getCurrentCellName() const override;

        void executeActivation(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor);
        ///< Execute the activation action for this ptr. If ptr is mActivated, mark activation as handled.

        std::int32_t getMemberShort(ESM::RefId id, std::string_view name, bool global) const override;

        std::int32_t getMemberLong(ESM::RefId id, std::string_view name, bool global) const override;

        float getMemberFloat(ESM::RefId id, std::string_view name, bool global) const override;

        void setMemberShort(ESM::RefId id, std::string_view name, std::int32_t value, bool global) override;

        void setMemberLong(ESM::RefId id, std::string_view name, std::int32_t value, bool global) override;

        void setMemberFloat(ESM::RefId id, std::string_view name, float value, bool global) override;

        MWWorld::Ptr getReference(bool required = true) const;
        ///< Reference, that the script is running from (can be empty)

        void updatePtr(const MWWorld::Ptr& base, const MWWorld::Ptr& updated);
        ///< Update the Ptr stored in mReference, if there is one stored there. Should be called after the reference has
        ///< been moved to a new cell.
    };
}
