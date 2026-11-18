#include "operations.hpp"

#include <cstdint>

#include <QDockWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "operation.hpp"

namespace
{
    constexpr std::int32_t operationLineHeight = 36;
}

CSVDoc::Operations::Operations()
{
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto* widgetContainer = new QWidget(this);
    mLayout = new QVBoxLayout;
    mLayout->setContentsMargins(0, 0, 0, 0);

    widgetContainer->setContentsMargins(0, 0, 0, 0);
    widgetContainer->setLayout(mLayout);
    setWidget(widgetContainer);
    setVisible(false);
    setFixedHeight(operationLineHeight);
    setTitleBarWidget(new QWidget(this));
}

void CSVDoc::Operations::setProgress(std::int32_t current, std::int32_t max, std::int32_t type, std::int32_t threads)
{
    for (auto& eachOperation : mOperations)
        if (eachOperation->getType() == type)
        {
            eachOperation->setProgress(current, max, threads);
            return;
        }

    auto* operation = new Operation(type, this);
    connect(operation, qOverload<std::int32_t>(&Operation::abortOperation), this, &Operations::abortOperation);

    mLayout->addLayout(operation->getLayout());
    mOperations.push_back(operation);
    operation->setProgress(current, max, threads);

    auto newCount = static_cast<std::int32_t>(mOperations.size());
    setFixedHeight(operationLineHeight * newCount);

    setVisible(true);
}

void CSVDoc::Operations::quitOperation(std::int32_t type)
{
    for (auto iter(mOperations.begin()); iter != mOperations.end(); ++iter)
        if ((*iter)->getType() == type)
        {
            mLayout->removeItem((*iter)->getLayout());

            (*iter)->deleteLater();
            mOperations.erase(iter);

            auto newCount = static_cast<std::int32_t>(mOperations.size());
            if (newCount > 0)
                setFixedHeight(operationLineHeight * newCount);
            else
                setVisible(false);

            break;
        }
}
