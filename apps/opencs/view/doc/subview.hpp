#pragma once

#include <string>

#include <QDockWidget>

#include <apps/opencs/model/world/universalid.hpp>

class QCloseEvent;
class QEvent;
class QObject;

namespace CSVDoc
{
    class SubView : public QDockWidget
    {
        Q_OBJECT

        CSMWorld::UniversalId mUniversalId;

    protected:
        void setUniversalId(const CSMWorld::UniversalId& id);

        bool event(QEvent* event) override;

    public:
        explicit SubView(CSMWorld::UniversalId id);
        ~SubView() override = default;
        SubView(const SubView&) = delete;
        SubView& operator=(const SubView&) = delete;

        CSMWorld::UniversalId getUniversalId() const;

        virtual void setEditLock(bool locked) = 0;

        virtual void setStatusBar(bool show);
        ///< Default implementation: ignored

        virtual void useHint(const std::string& hint);
        ///< Default implementation: ignored

        virtual std::string getTitle() const;

    private:
        void closeEvent(QCloseEvent* event) override;

    signals:

        void focusId(const CSMWorld::UniversalId& universalId, const std::string& hint);

        void closeRequest(SubView* subView);

        void updateTitle();

        void updateSubViewIndices(SubView* view = nullptr);

        void universalIdChanged(const CSMWorld::UniversalId& universalId);

    protected slots:

        void closeRequest();
    };
}
