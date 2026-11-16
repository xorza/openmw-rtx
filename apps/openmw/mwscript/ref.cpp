#include "ref.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <components/interpreter/runtime.hpp>

#include "interpretercontext.hpp"

MWWorld::Ptr MWScript::ExplicitRef::operator()(Interpreter::Runtime& runtime, bool required, bool activeOnly) const
{
    ESM::RefId id = ESM::RefId::stringRefId(runtime.getStringLiteral(runtime[0].mInteger));
    runtime.pop();

    if (required)
        return MWBase::Environment::get().getWorld()->getPtr(id, activeOnly);
    else
        return MWBase::Environment::get().getWorld()->searchPtr(id, activeOnly);
}

MWWorld::Ptr MWScript::ImplicitRef::operator()(Interpreter::Runtime& runtime, bool required, bool activeOnly) const
{
    auto& context = static_cast<MWScript::InterpreterContext&>(runtime.getContext());

    return context.getReference(required);
}
