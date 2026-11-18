#pragma once

#include <cstdint>
#include <map>
#include <memory>

#include <QObject>

#include <apps/opencs/model/doc/operationholder.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <components/toutf8/toutf8.hpp>

namespace CSMWorld
{
    class Data;
}

namespace CSMDoc
{
    class Operation;
    class Document;
    struct Message;
}

namespace CSMTools
{
    class ReportModel;
    class Search;
    class SearchOperation;
    class MergeOperation;

    class Tools : public QObject
    {
        Q_OBJECT

        CSMDoc::Document& mDocument;
        CSMWorld::Data& mData;
        CSMDoc::Operation* mVerifierOperation;
        CSMDoc::OperationHolder* mVerifier;
        SearchOperation* mSearchOperation;
        CSMDoc::OperationHolder* mSearch;
        MergeOperation* mMergeOperation;
        CSMDoc::OperationHolder* mMerge;
        std::map<std::int32_t, ReportModel*> mReports;
        std::int32_t mNextReportNumber;
        std::map<std::int32_t, std::int32_t> mActiveReports; // type, report number
        ToUTF8::FromType mEncoding;

        // not implemented
        Tools(const Tools&);
        Tools& operator=(const Tools&);

        CSMDoc::OperationHolder* getVerifier();

        CSMDoc::OperationHolder* get(std::int32_t type);
        ///< Returns a 0-pointer, if operation hasn't been used yet.

        const CSMDoc::OperationHolder* get(std::int32_t type) const;
        ///< Returns a 0-pointer, if operation hasn't been used yet.

    public:
        Tools(CSMDoc::Document& document, ToUTF8::FromType encoding);

        ~Tools() override;

        /// \param reportId If a valid VerificationResults ID, run verifier for the
        /// specified report instead of creating a new one.
        ///
        /// \return ID of the report for this verification run
        CSMWorld::UniversalId runVerifier(const CSMWorld::UniversalId& reportId = CSMWorld::UniversalId());

        /// Return ID of the report for this search.
        CSMWorld::UniversalId newSearch();

        void runSearch(const CSMWorld::UniversalId& searchId, const Search& search);

        void runMerge(std::unique_ptr<CSMDoc::Document> target);

        void abortOperation(std::int32_t type);
        ///< \attention The operation is not aborted immediately.

        std::int32_t getRunningOperations() const;

        ReportModel* getReport(const CSMWorld::UniversalId& id);
        ///< The ownership of the returned report is not transferred.

    private slots:

        void verifierMessage(const CSMDoc::Message& message, std::int32_t type);

    signals:

        void progress(std::int32_t current, std::int32_t max, std::int32_t type);

        void done(std::int32_t type, bool failed);

        /// \attention When this signal is emitted, *this hands over the ownership of the
        /// document. This signal must be handled to avoid a leak.
        void mergeDone(CSMDoc::Document* document);
    };
}
