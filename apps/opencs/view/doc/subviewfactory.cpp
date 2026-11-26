#include "subviewfactory.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

#include <apps/opencs/model/world/universalid.hpp>

CSVDoc::SubViewFactoryManager::~SubViewFactoryManager()
{
<<<<<<< apps/opencs/view/doc/subviewfactory.cpp (ours)
    for (auto& subViewFactory : mSubViewFactories)
        delete subViewFactory.second;
=======
    for (auto& [_, factory] : mSubViewFactories)
        delete factory;
>>>>>>> apps/opencs/view/doc/subviewfactory.cpp (theirs)
}

void CSVDoc::SubViewFactoryManager::add(const CSMWorld::UniversalId::Type& id, SubViewFactoryBase* factory)
{
    assert(!mSubViewFactories.contains(id));

    mSubViewFactories.insert(std::make_pair(id, factory));
}

CSVDoc::SubView* CSVDoc::SubViewFactoryManager::makeSubView(const CSMWorld::UniversalId& id, CSMDoc::Document& document)
{
    auto iter = mSubViewFactories.find(id.getType());

    if (iter == mSubViewFactories.end())
        throw std::runtime_error("Failed to create a sub view for: " + id.toString());

    return iter->second->makeSubView(id, document);
}
