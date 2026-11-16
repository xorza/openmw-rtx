#include "subviews.hpp"

#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/doc/subviewfactory.hpp>
#include <apps/opencs/view/doc/subviewfactoryimp.hpp>

#include "reportsubview.hpp"
#include "searchsubview.hpp"

void CSVTools::addSubViewFactories(CSVDoc::SubViewFactoryManager& manager)
{
    manager.add(CSMWorld::UniversalId::Type_VerificationResults, new CSVDoc::SubViewFactory<ReportSubView>);
    manager.add(CSMWorld::UniversalId::Type_LoadErrorLog, new CSVDoc::SubViewFactory<ReportSubView>);
    manager.add(CSMWorld::UniversalId::Type_Search, new CSVDoc::SubViewFactory<SearchSubView>);
}
